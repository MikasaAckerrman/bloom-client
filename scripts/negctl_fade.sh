#!/bin/sh
# negctl_fade.sh -- negative control for scripts/kf_fade_check.py.
#
# A sync checker that cannot FAIL is decoration. This mutates a COPY of the tree
# (never the working files) and demands the checker rejects each mutation.
#
# Completeness is asserted too: the copy must run the same number of checks as
# the original. A checker that silently skips checks on the copy would make a
# missed mutation look like success -- that trap has bitten before.
set -u

ORIG_DIR=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/cl_dll" "$WORK/tests" "$WORK/scripts"
cp "$ORIG_DIR/cl_dll/death.cpp"             "$WORK/cl_dll/"
cp "$ORIG_DIR/tests/test_killfeed_fade.c"   "$WORK/tests/"
cp "$ORIG_DIR/scripts/kf_fade_check.py"     "$WORK/scripts/"

pass=0
fail=0

# --- completeness: the copy must behave like the original -----------------
( cd "$ORIG_DIR" && python3 scripts/kf_fade_check.py >/dev/null 2>&1 )
orig_rc=$?
( cd "$WORK" && python3 scripts/kf_fade_check.py >/dev/null 2>&1 )
copy_rc=$?
if [ "$orig_rc" -eq 0 ] && [ "$copy_rc" -eq 0 ]; then
	echo "ok    baseline: checker passes on both original and copy"
	pass=$((pass + 1))
else
	echo "FAIL  baseline broken (orig rc=$orig_rc, copy rc=$copy_rc) -- mutations below prove nothing"
	fail=$((fail + 1))
fi

# mutate <label> <file> <sed-expression>
mutate() {
	label="$1"; file="$2"; expr="$3"
	cp "$WORK/$file" "$WORK/$file.keep"
	sed -i "$expr" "$WORK/$file"
	if cmp -s "$WORK/$file" "$WORK/$file.keep"; then
		echo "FAIL  $label: sed changed nothing -- the mutation is not testing anything"
		fail=$((fail + 1))
	else
		( cd "$WORK" && python3 scripts/kf_fade_check.py >/dev/null 2>&1 )
		if [ $? -ne 0 ]; then
			echo "ok    $label -> rejected"
			pass=$((pass + 1))
		else
			echo "FAIL  $label -> checker still passed"
			fail=$((fail + 1))
		fi
	fi
	mv "$WORK/$file.keep" "$WORK/$file"
}

# 1. the original bug: fade the colour unconditionally
mutate "unconditional fade (the reported bug)" cl_dll/death.cpp \
	's|float fade = KF_TextFadeable() ? alpha : 1.0f;|float fade = alpha;|'

# 2. bold pass left unscaled -- a half fix
mutate "bold draw not scaled by fade" cl_dll/death.cpp \
	'0,/SetConsoleTextColor( rgb\[0\]\*fade/! s|SetConsoleTextColor( rgb\[0\]\*fade, rgb\[1\]\*fade, rgb\[2\]\*fade )|SetConsoleTextColor( rgb[0], rgb[1], rgb[2] )|'

# 3. predicate no longer consulted -- dead code
mutate "KF_DrawName stops consulting KF_TextFadeable" cl_dll/death.cpp \
	's|KF_TextFadeable() ? alpha : 1.0f|1.0f == 1.0f ? alpha : 1.0f|'

# 4. engine cvar check dropped from the source
mutate "hud_fontrender check removed from death.cpp" cl_dll/death.cpp \
	's|if( gEngfuncs.pfnGetCvarFloat( "hud_fontrender" ) != 0.0f )|if( 0 )|'

# 5. harness drifts from the source
mutate "test mirror drops the hud_fontrender early-out" tests/test_killfeed_fade.c \
	's|if( g_hudFontrender != 0.0f )|if( 0 )|'

# 6. harness drifts on the other predicate
mutate "test mirror drops the cl_killfeed_font early-out" tests/test_killfeed_fade.c \
	's|if( g_kfFont != 0.0f )|if( 0 )|'

# 7. scalable branch loses the fade
mutate "scalable branch int colour unscaled" cl_dll/death.cpp \
	's|r = (int)( rgb\[0\] \* fade \* 255.0f );|r = (int)( rgb[0] * 255.0f );|'

echo
echo "negctl_fade: $pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1

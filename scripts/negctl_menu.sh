#!/bin/sh
# negctl_menu.sh -- negative control for scripts/kf_menu_check.py.
#
# Mutates a COPY of the tree and demands the checker rejects every mutation.
#
# Mutations are applied by a small python helper, NOT by sed: the strings here
# contain `||`, `\n` and escaped quotes, and BusyBox sed mis-parses them ("bad
# option in substitution expression") -- which silently turns a mutation into a
# no-op. A no-op mutation that the checker "passes" looks exactly like a checker
# that failed to notice, so it is asserted explicitly below.
set -u

ORIG_DIR=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/cl_dll" "$WORK/tests" "$WORK/scripts"
cp "$ORIG_DIR/cl_dll/cdll_int.cpp"        "$WORK/cl_dll/"
cp "$ORIG_DIR/tests/test_menu_probe.c"    "$WORK/tests/"
cp "$ORIG_DIR/scripts/kf_menu_check.py"   "$WORK/scripts/"

pass=0
fail=0

( cd "$ORIG_DIR" && python3 scripts/kf_menu_check.py >/dev/null 2>&1 ); orig_rc=$?
( cd "$WORK"     && python3 scripts/kf_menu_check.py >/dev/null 2>&1 ); copy_rc=$?
if [ "$orig_rc" -eq 0 ] && [ "$copy_rc" -eq 0 ]; then
	echo "ok    baseline: checker passes on original and copy"
	pass=$((pass + 1))
else
	echo "FAIL  baseline broken (orig=$orig_rc copy=$copy_rc) -- mutations prove nothing"
	fail=$((fail + 1))
fi

# mutate <label> <file> <literal-old> <literal-new>
mutate() {
	label="$1"; file="$2"; old="$3"; new="$4"
	cp "$WORK/$file" "$WORK/$file.keep"
	MUT_OLD="$old" MUT_NEW="$new" MUT_FILE="$WORK/$file" python3 -c '
import os, sys
p = os.environ["MUT_FILE"]
old = os.environ["MUT_OLD"]
new = os.environ["MUT_NEW"]
s = open(p, encoding="utf-8").read()
if old not in s:
    sys.exit(2)
open(p, "w", encoding="utf-8").write(s.replace(old, new, 1))
'
	rc=$?
	if [ "$rc" -eq 2 ]; then
		echo "FAIL  $label: pattern not found -- mutation tests nothing"
		fail=$((fail + 1))
	elif cmp -s "$WORK/$file" "$WORK/$file.keep"; then
		echo "FAIL  $label: file unchanged -- mutation tests nothing"
		fail=$((fail + 1))
	else
		( cd "$WORK" && python3 scripts/kf_menu_check.py >/dev/null 2>&1 )
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

# 1. the original bug: no latch, message on every connect
mutate "latch removed (console spam returns)" cl_dll/cdll_int.cpp \
	'if( g_pMenu || s_menuProbed )' 'if( g_pMenu )'

# 2. latch moved above the lookup: a late-appearing menu is never picked up
mutate "latch checked before the lookup" cl_dll/cdll_int.cpp \
	'g_pMenu = GetNativeMenuExports();' 'if( s_menuProbed ) return;
	g_pMenu = GetNativeMenuExports();'

# 3. modal warning box back for an optional feature
mutate "Sys_Warn reintroduced" cl_dll/cdll_int.cpp \
	'gEngfuncs.Con_DPrintf( "Menu: no mobile API, using touch menus\n" );' \
	'gMobileAPI.pfnSys_Warn( "no menu" );'

# 4. optional case promoted to a visible line -> every user sees it
mutate "optional case uses Con_Printf" cl_dll/cdll_int.cpp \
	'Con_DPrintf( "Menu: host exports no' 'Con_Printf( "Menu: host exports no'

# 5. real mismatch demoted to developer-only -> a genuine fault goes silent
mutate "version mismatch demoted to Con_DPrintf" cl_dll/cdll_int.cpp \
	'Con_Printf( "Menu: \"MenuFactory\" does not provide' \
	'Con_DPrintf( "Menu: \"MenuFactory\" does not provide'

# 6. harness drifts: latch dropped from the mirror
mutate "harness drops the latch" tests/test_menu_probe.c \
	'if( g_pMenu || s_menuProbed )' 'if( g_pMenu )'

# 7. harness drifts: branch condition changed
mutate "harness changes a branch condition" tests/test_menu_probe.c \
	'if( !g_hostHasFactory )' 'if( 0 )'

echo
echo "negctl_menu: $pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1

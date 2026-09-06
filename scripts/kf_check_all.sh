#!/bin/sh
# Run EVERY killfeed check in one shot. Anything that can be verified without an
# Android SDK lives here; if this is green the layout math, the C<->Python mirror
# and the compile are all consistent.
#
#   sh scripts/kf_check_all.sh
#
# What it does NOT prove: how it looks in-game. :app builds only in CI (no
# Android SDK in this sandbox), so visual behaviour stays unverified until then.
set -e
cd "$(dirname "$0")/.."

fails=0

for t in test_killfeed test_killfeed_geom test_killfeed_cvars test_killfeed_slots test_killfeed_textscale test_killfeed_bounds test_killfeed_corner test_killfeed_fade test_menu_probe; do
	printf '%-26s ' "$t"
	if gcc -Wall -Wextra -I cl_dll/include -o /tmp/kf_check_bin "tests/$t.c" -lm 2>/tmp/kf_check_err; then
		if /tmp/kf_check_bin >/tmp/kf_check_out 2>&1; then
			tail -1 /tmp/kf_check_out
		else
			tail -3 /tmp/kf_check_out
			fails=$((fails+1))
		fi
	else
		head -5 /tmp/kf_check_err
		fails=$((fails+1))
	fi
done

printf '%-26s ' 'mirror C<->python'
if python3 tests/kf_mirror_check.py >/tmp/kf_check_out 2>&1; then
	tail -1 /tmp/kf_check_out
else
	tail -6 /tmp/kf_check_out
	fails=$((fails+1))
fi

for f in cl_dll/death.cpp cl_dll/draw_util.cpp cl_dll/cdll_int.cpp; do
	printf '%-26s ' "syntax $(basename "$f")"
	if sh scripts/kf_syntax_check.sh "$f" >/tmp/kf_check_out 2>&1; then
		echo OK
	else
		grep -E 'error' /tmp/kf_check_out | head -5
		fails=$((fails+1))
	fi
done

printf '%-26s ' 'weapon table'
if python3 scripts/kf_weapon_table_check.py >/tmp/kf_check_out 2>&1; then
	tail -1 /tmp/kf_check_out
else
	tail -4 /tmp/kf_check_out
	fails=$((fails+1))
fi

printf '%-26s ' 'row cap'
if python3 scripts/kf_rowcap_check.py >/tmp/kf_check_out 2>&1; then
	tail -1 /tmp/kf_check_out
else
	tail -6 /tmp/kf_check_out
	fails=$((fails+1))
fi

printf '%-26s ' 'push sequence'
if python3 scripts/kf_pushcount_check.py >/tmp/kf_check_out 2>&1; then
	tail -1 /tmp/kf_check_out
else
	tail -8 /tmp/kf_check_out
	fails=$((fails+1))
fi

printf '%-26s ' 'colours'
if python3 scripts/kf_colors_check.py >/tmp/kf_check_out 2>&1; then
	tail -1 /tmp/kf_check_out
else
	rg -n 'MISMATCH|NOT FOUND' /tmp/kf_check_out | head -6
	fails=$((fails+1))
fi

printf '%-26s ' 'border gate'
if python3 scripts/kf_gate_check.py >/tmp/kf_check_out 2>&1; then
	tail -1 /tmp/kf_check_out
else
	rg -n 'MISSING|UNMODELLED|MISMATCH|could not' /tmp/kf_check_out | head -5
	fails=$((fails+1))
fi

printf '%-26s ' 'corner harness sync'
if python3 scripts/kf_corner_check.py >/tmp/kf_check_out 2>&1; then
	tail -1 /tmp/kf_check_out
else
	grep -nE 'OUT OF SYNC|differs|count' /tmp/kf_check_out | head -5
	fails=$((fails+1))
fi

printf '%-26s ' 'apk signing stable'
if python3 scripts/kf_signing_check.py >/tmp/kf_check_out 2>&1; then
	tail -1 /tmp/kf_check_out
else
	grep -nE 'FAILED|EMPTY|missing|does not' /tmp/kf_check_out | head -5
	fails=$((fails+1))
fi

printf '%-26s ' 'signing negative control'
if sh scripts/negctl_signing.sh >/tmp/kf_check_out 2>&1; then
	tail -1 /tmp/kf_check_out
else
	grep -nE 'FAIL' /tmp/kf_check_out | head -5
	fails=$((fails+1))
fi

printf '%-26s ' 'menu probe sync'
if python3 scripts/kf_menu_check.py >/tmp/kf_check_out 2>&1; then
	tail -1 /tmp/kf_check_out
else
	grep -nE 'OUT OF SYNC|differs|latch|Sys_Warn|expected' /tmp/kf_check_out | head -5
	fails=$((fails+1))
fi

printf '%-26s ' 'menu negative control'
if sh scripts/negctl_menu.sh >/tmp/kf_check_out 2>&1; then
	tail -1 /tmp/kf_check_out
else
	grep -nE 'FAIL' /tmp/kf_check_out | head -5
	fails=$((fails+1))
fi

printf '%-26s ' 'fade harness sync'
if python3 scripts/kf_fade_check.py >/tmp/kf_check_out 2>&1; then
	tail -1 /tmp/kf_check_out
else
	grep -nE 'OUT OF SYNC|differs|expected|not scaled|missing' /tmp/kf_check_out | head -5
	fails=$((fails+1))
fi

printf '%-26s ' 'fade negative control'
if sh scripts/negctl_fade.sh >/tmp/kf_check_out 2>&1; then
	tail -1 /tmp/kf_check_out
else
	grep -nE 'FAIL' /tmp/kf_check_out | head -5
	fails=$((fails+1))
fi

printf '%-26s ' 'sprite round-trip'
GC_TGA=/tmp/minis-goldclient-20260828/out3/app/cstrike/gfx/hud/deathnotice
if [ -d "$GC_TGA" ]; then
	if python3 scripts/kf_spr_check.py \
			3rdparty/cs16client-extras/sprites/kf "$GC_TGA" >/tmp/kf_check_out 2>&1; then
		tail -1 /tmp/kf_check_out
	else
		tail -3 /tmp/kf_check_out
		fails=$((fails+1))
	fi
else
	echo 'SKIP (extracted GoldClient TGAs not present)'
fi

rm -f /tmp/kf_check_bin /tmp/kf_check_err /tmp/kf_check_out

if [ "$fails" -gt 0 ]; then
	echo "FAILED: $fails check(s)"
	exit 1
fi
echo 'ALL KILLFEED CHECKS PASSED'

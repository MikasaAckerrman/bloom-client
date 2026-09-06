#!/bin/sh
# negctl_signing.sh -- negative control for scripts/kf_signing_check.py.
#
# Mutates a COPY of the build file and demands the checker rejects each mutation.
# Mutations go through python, not sed: the patterns contain braces and quotes
# that BusyBox sed mis-parses into a silent no-op.
set -u

ORIG_DIR=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/android/app" "$WORK/scripts"
cp "$ORIG_DIR/android/app/build.gradle"          "$WORK/android/app/"
cp "$ORIG_DIR/android/app/bloom-debug.keystore"  "$WORK/android/app/"
cp "$ORIG_DIR/scripts/kf_signing_check.py"       "$WORK/scripts/"

pass=0
fail=0

( cd "$ORIG_DIR" && python3 scripts/kf_signing_check.py >/dev/null 2>&1 ); orig_rc=$?
( cd "$WORK"     && python3 scripts/kf_signing_check.py >/dev/null 2>&1 ); copy_rc=$?
if [ "$orig_rc" -eq 0 ] && [ "$copy_rc" -eq 0 ]; then
	echo "ok    baseline: checker passes on original and copy"
	pass=$((pass + 1))
else
	echo "FAIL  baseline broken (orig=$orig_rc copy=$copy_rc) -- mutations prove nothing"
	fail=$((fail + 1))
fi

mutate() {
	label="$1"; file="$2"; old="$3"; new="$4"
	cp "$WORK/$file" "$WORK/$file.keep"
	MUT_OLD="$old" MUT_NEW="$new" MUT_FILE="$WORK/$file" python3 -c '
import os, sys
p = os.environ["MUT_FILE"]
s = open(p, encoding="utf-8").read()
old, new = os.environ["MUT_OLD"], os.environ["MUT_NEW"]
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
		( cd "$WORK" && python3 scripts/kf_signing_check.py >/dev/null 2>&1 )
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

# 1. the original bug: empty debug config -> key regenerated each build
mutate "signingConfigs.debug emptied (the reported bug)" android/app/build.gradle \
	'		debug {
			storeFile = file("bloom-debug.keystore")
			storePassword = "android"
			keyAlias = "androiddebugkey"
			keyPassword = "android"
		}' '		debug {}'

# 2. keystore no longer wired to the build type
mutate "buildTypes.debug drops signingConfig" android/app/build.gradle \
	'			signingConfig = signingConfigs.debug' '			// removed'

# 3. wrong alias -> gradle would fail late, checker must catch it early
mutate "keyAlias does not exist in the keystore" android/app/build.gradle \
	'keyAlias = "androiddebugkey"' 'keyAlias = "nosuchalias"'

# 4. wrong store password
mutate "storePassword wrong" android/app/build.gradle \
	'storePassword = "android"' 'storePassword = "wrongpass"'

# 5. keystore path points at a file that is not there
mutate "storeFile points to a missing keystore" android/app/build.gradle \
	'storeFile = file("bloom-debug.keystore")' 'storeFile = file("absent.keystore")'

# 6. the keystore itself disappears from the tree
cp "$WORK/android/app/bloom-debug.keystore" "$WORK/ks.keep"
rm -f "$WORK/android/app/bloom-debug.keystore"
( cd "$WORK" && python3 scripts/kf_signing_check.py >/dev/null 2>&1 )
if [ $? -ne 0 ]; then
	echo "ok    keystore file deleted -> rejected"
	pass=$((pass + 1))
else
	echo "FAIL  keystore file deleted -> checker still passed"
	fail=$((fail + 1))
fi
cp "$WORK/ks.keep" "$WORK/android/app/bloom-debug.keystore"

# 7. two APKs with different certificates must be reported as non-upgradable
V2=/var/minis/attachments/CS16Client-killfeed-v2.apk
V3=/var/minis/attachments/CS16Client-killfeed-v3.apk
if [ -f "$V2" ] && [ -f "$V3" ]; then
	( cd "$WORK" && python3 scripts/kf_signing_check.py "$V2" "$V3" >/dev/null 2>&1 )
	if [ $? -ne 0 ]; then
		echo "ok    v2+v3 (different keys) -> rejected"
		pass=$((pass + 1))
	else
		echo "FAIL  v2+v3 have different keys but checker passed"
		fail=$((fail + 1))
	fi
else
	echo "skip  v2/v3 APKs not present, cannot test cross-APK comparison"
fi

echo
echo "negctl_signing: $pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1

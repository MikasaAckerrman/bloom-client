#!/usr/bin/env python3
"""Guard the STABLE APK signature.

WHY. `signingConfigs { debug {} }` means "use ~/.android/debug.keystore". That
file does not exist on a fresh CI runner, so Gradle mints a new certificate every
build. Android refuses to update an app whose signature changed, so each build
had to be uninstalled first. Measured on two consecutive artifacts:
    3332ef0 -> SHA256 AF:6D:CC:CF:F9:F1:FB:FF...
    82c3495 -> SHA256 C8:F9:36:00:F1:6C:26:02...
with the certificate's "valid from" equal to the validateSigning step, second for
second.

This checks the build file still pins a keystore, that the keystore is actually
present and contains the alias the build asks for, and -- when an APK is given --
that the APK really carries that certificate.

    python3 scripts/kf_signing_check.py [path/to.apk ...]

An APK argument is optional; without it only the build configuration is checked.
"""
import os
import re
import subprocess
import sys

GRADLE = 'android/app/build.gradle'

problems = []
notes = []


def read(path):
    try:
        return open(path, encoding='utf-8', errors='replace').read()
    except OSError as e:
        problems.append(f'cannot read {path}: {e}')
        return ''


def strip_comments(s):
    """Remove // and /* */ comments so a comment like `debug {}` cannot be
    mistaken for an empty signingConfigs.debug block. That exact trap is why
    this function exists: the first version of this checker failed on the
    explanation it was supposed to protect."""
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    return s


def block(text, name):
    """Body of a `name { ... }` block, brace-matched."""
    m = re.search(r'\b' + re.escape(name) + r'\s*\{', text)
    if not m:
        return None
    i = m.end() - 1
    depth, j = 0, i
    while j < len(text):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
        j += 1
    return None


g = strip_comments(read(GRADLE))
if problems:
    print('\n'.join(problems))
    sys.exit(1)

# --- 1. signingConfigs.debug must pin a keystore -------------------------
sc = block(g, 'signingConfigs')
if sc is None:
    problems.append(f'{GRADLE}: no signingConfigs block')
else:
    dbg = block(sc, 'debug')
    if dbg is None:
        problems.append(f'{GRADLE}: no signingConfigs.debug block')
    elif re.fullmatch(r'\{\s*\}', dbg):
        problems.append(f'{GRADLE}: signingConfigs.debug is EMPTY -- the signing key '
                        f'will be regenerated on every CI build and users must '
                        f'uninstall before updating')
    else:
        store = re.search(r'storeFile\s*=\s*file\(\s*"([^"]+)"\s*\)', dbg)
        alias = re.search(r'keyAlias\s*=\s*"([^"]+)"', dbg)
        spass = re.search(r'storePassword\s*=\s*"([^"]+)"', dbg)
        kpass = re.search(r'keyPassword\s*=\s*"([^"]+)"', dbg)
        for label, m in (('storeFile', store), ('keyAlias', alias),
                         ('storePassword', spass), ('keyPassword', kpass)):
            if not m:
                problems.append(f'{GRADLE}: signingConfigs.debug has no {label}')

        # --- 2. the keystore file exists and holds that alias ------------
        if store and alias:
            ks = os.path.join('android', 'app', store.group(1))
            if not os.path.isfile(ks):
                problems.append(f'{ks}: keystore is missing -- the build would fall '
                                f'back to a generated key')
            elif spass:
                r = subprocess.run(
                    ['keytool', '-list', '-keystore', ks, '-storepass', spass.group(1)],
                    capture_output=True, text=True)
                if r.returncode != 0:
                    problems.append(f'{ks}: keytool cannot open it with the configured '
                                    f'storePassword')
                elif alias.group(1).lower() not in r.stdout.lower():
                    problems.append(f'{ks}: does not contain alias '
                                    f'"{alias.group(1)}"')
                else:
                    notes.append(f'keystore {ks} holds alias "{alias.group(1)}"')

# --- 3. buildTypes.debug must use it explicitly -------------------------
bt = block(g, 'buildTypes')
if bt is None:
    problems.append(f'{GRADLE}: no buildTypes block')
else:
    dbg_t = block(bt, 'debug')
    if dbg_t is None:
        problems.append(f'{GRADLE}: no buildTypes.debug block')
    elif not re.search(r'signingConfig\s*=\s*signingConfigs\.debug', dbg_t):
        problems.append(f'{GRADLE}: buildTypes.debug does not set '
                        f'`signingConfig = signingConfigs.debug` explicitly')

# --- 4. optional: the APK really carries that certificate ---------------
def apk_sha256(path):
    r = subprocess.run(['keytool', '-printcert', '-jarfile', path],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return None
    m = re.search(r'SHA256:\s*([0-9A-F:]+)', r.stdout)
    return m.group(1) if m else None


apks = sys.argv[1:]
if apks and not problems:
    expect = None
    if store and spass:
        ks = os.path.join('android', 'app', store.group(1))
        r = subprocess.run(['keytool', '-list', '-v', '-keystore', ks,
                            '-storepass', spass.group(1)],
                           capture_output=True, text=True)
        m = re.search(r'SHA256:\s*([0-9A-F:]+)', r.stdout)
        expect = m.group(1) if m else None

    seen = {}
    for a in apks:
        if not os.path.isfile(a):
            problems.append(f'{a}: not found')
            continue
        got = apk_sha256(a)
        if got is None:
            problems.append(f'{a}: cannot read a certificate')
            continue
        seen[a] = got
        if expect and got != expect:
            problems.append(f'{a}: signed with {got[:23]}... but the configured '
                            f'keystore is {expect[:23]}...')
        else:
            notes.append(f'{os.path.basename(a)} signed with {got[:23]}...')

    # Every APK given must share one certificate, else they cannot upgrade.
    if len(set(seen.values())) > 1:
        problems.append('the given APKs do NOT share a certificate -- one cannot '
                        'upgrade the other:\n    ' +
                        '\n    '.join(f'{os.path.basename(k)} {v[:23]}...'
                                      for k, v in seen.items()))

for n in notes:
    print('  ' + n)

if problems:
    print('SIGNING CHECK FAILED')
    for p in problems:
        print('  ' + p)
    sys.exit(1)

print('signing is stable: keystore pinned, present, and wired to buildTypes.debug')

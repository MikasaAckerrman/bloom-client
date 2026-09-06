#!/usr/bin/env python3
"""Prove tests/test_killfeed_fade.c still mirrors death.cpp.

The test cannot include death.cpp (it needs the engine), so it re-implements
KF_TextScalable / KF_TextFadeable and the colour arithmetic of KF_DrawName. A
mirror that silently drifts is worse than no test: it keeps passing while the
shipped code regresses. This script fails the build if they disagree.

Checked, and why each one matters:
  1. KF_TextFadeable exists in death.cpp and is used by KF_DrawName -- otherwise
     the predicate is dead code and the fade is unconditional again.
  2. The `fade` local is picked via KF_TextFadeable, not from `alpha` directly.
  3. No `* alpha` survives in KF_DrawName's colour maths (that WAS the bug).
  4. The console branch scales by `fade` in both the plain and the bold draw --
     the bold pass re-arms the colour, and missing it there would half-fix it.
  5. Both predicates in the test agree line-for-line in structure with the source
     (same early-outs, same order).
"""
import re
import sys

SRC = 'cl_dll/death.cpp'
TEST = 'tests/test_killfeed_fade.c'

problems = []


def read(path):
    try:
        return open(path, encoding='utf-8', errors='replace').read()
    except OSError as e:
        problems.append(f'cannot read {path}: {e}')
        return ''


src = read(SRC)
test = read(TEST)
if problems:
    print('\n'.join(problems))
    sys.exit(1)


def body(text, signature):
    """Extract a function body by brace matching from its signature."""
    i = text.find(signature)
    if i < 0:
        return None
    i = text.find('{', i)
    if i < 0:
        return None
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


# --- 1. the predicate exists and is actually consulted -------------------
src_fadeable = body(src, 'static bool KF_TextFadeable( void )')
if src_fadeable is None:
    problems.append('KF_TextFadeable() is missing from death.cpp')

src_drawname = body(src, 'static void KF_DrawName(')
if src_drawname is None:
    problems.append('KF_DrawName() not found in death.cpp')
else:
    if 'KF_TextFadeable()' not in src_drawname:
        problems.append('KF_DrawName does not consult KF_TextFadeable() -- '
                        'the fade is unconditional, the bug is back')

    # --- 2/3. the colour must go through `fade`, never raw alpha ---------
    if not re.search(r'float\s+fade\s*=\s*KF_TextFadeable\(\)\s*\?\s*alpha\s*:\s*1\.0f', src_drawname):
        problems.append('KF_DrawName: expected `float fade = KF_TextFadeable() ? alpha : 1.0f`')

    # Strip comments before hunting for `* alpha`: the explanation above the code
    # legitimately mentions the old arithmetic.
    code = re.sub(r'/\*.*?\*/', '', src_drawname, flags=re.S)
    code = re.sub(r'//[^\n]*', '', code)
    stray = re.findall(r'\*\s*alpha', code)
    if stray:
        problems.append(f'KF_DrawName still scales a colour by `alpha` directly '
                        f'({len(stray)} site(s)) -- use `fade`')

    # --- 4. console branch: both draws scaled, bold included -------------
    # Count SetConsoleTextColor calls specifically. Matching `rgb[0] * fade`
    # anywhere also catches the int conversion used by the SCALABLE branch, which
    # is a different code path -- that gave a false "3 calls" reading.
    n_calls = len(re.findall(r'SetConsoleTextColor\(', code))
    n_faded = len(re.findall(r'SetConsoleTextColor\(\s*rgb\[0\]\s*\*\s*fade', code))
    if n_calls != 2:
        problems.append(f'console branch: expected 2 SetConsoleTextColor calls '
                        f'(plain + bold), found {n_calls}')
    elif n_faded != 2:
        problems.append(f'console branch: {n_calls - n_faded} SetConsoleTextColor '
                        f'call(s) not scaled by `fade`')

    # The scalable branch converts to 0..255 ints; that one must use fade too.
    if not re.search(r'r\s*=\s*\(int\)\(\s*rgb\[0\]\s*\*\s*fade\s*\*\s*255\.0f', code):
        problems.append('scalable branch: int colour is not scaled by `fade`')


# --- 5. the test's mirror matches the source's logic --------------------
def norm(s):
    if s is None:
        return None
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    s = re.sub(r'\s+', '', s)
    return s


pairs = [
    ('KF_TextScalable',
     body(src, 'static bool KF_TextScalable( void )'),
     body(test, 'static int KF_TextScalable( void )'),
     [('cl_killfeed_font&&cl_killfeed_font->value!=0.0f', 'g_kfFont!=0.0f'),
      ('g_iMobileAPIVersion!=0', 'g_mobileAPI!=0'),
      ('returnfalse', 'return0'),
      ('return', 'return')]),
    ('KF_TextFadeable',
     src_fadeable,
     body(test, 'static int KF_TextFadeable( void )'),
     [('gEngfuncs.pfnGetCvarFloat("hud_fontrender")!=0.0f', 'g_hudFontrender!=0.0f'),
      ('returnfalse', 'return0'),
      ('returntrue', 'return1')]),
]

for name, a, b, subs in pairs:
    if a is None or b is None:
        problems.append(f'{name}: missing in {"death.cpp" if a is None else TEST}')
        continue
    na, nb = norm(a), norm(b)
    for frm, to in subs:
        na = na.replace(frm, to)
    # bool/int differ by return type only
    if na != nb:
        problems.append(f'{name}: harness logic differs from death.cpp\n'
                        f'    source: {na}\n'
                        f'    test:   {nb}')

if problems:
    print('FADE HARNESS OUT OF SYNC WITH death.cpp')
    for p in problems:
        print('  ' + p)
    print('\nFix tests/test_killfeed_fade.c (or death.cpp) so the two agree.')
    sys.exit(1)

print('fade harness mirrors death.cpp: predicates and colour maths all match')

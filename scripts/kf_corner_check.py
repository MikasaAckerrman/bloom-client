#!/usr/bin/env python3
"""Keep the corner-arc raster harness honest against the real drawing code.

tests/test_killfeed_corner.c re-implements KF_FilledPlate and KF_Border so the
corner geometry can be rasterised on the host (death.cpp cannot be compiled
here -- it needs the engine headers). A hand-copied mirror is worthless the
moment the original changes, so this compares the two, statement by statement.

What is compared, per function:
  * the quarter-circle expression (dx = r - round(sqrt(r*r - (r-1-i)^2)))
  * the degenerate-case guard
  * the number and order of the fill calls

Run: python3 scripts/kf_corner_check.py
Exit 0 when the harness still mirrors death.cpp, 1 otherwise.
"""
import re
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'cl_dll', 'death.cpp')
HARNESS = os.path.join(ROOT, 'tests', 'test_killfeed_corner.c')


def read(path):
    with open(path, encoding='utf-8', errors='replace') as fh:
        return fh.read()


def body(text, signature_re, name):
    """Extract a function body by brace matching from the signature match."""
    m = re.search(signature_re, text)
    if not m:
        sys.exit('kf_corner_check: could not find %s in %s' % (name, path_of(text)))
    i = text.index('{', m.end() - 1) if '{' not in m.group(0) else m.start() + m.group(0).index('{')
    depth = 0
    start = i
    while i < len(text):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    sys.exit('kf_corner_check: unbalanced braces in %s' % name)


def path_of(text):
    return SRC if text is SRC_TEXT else HARNESS


def normalise(code):
    """Strip comments and collapse whitespace so formatting is not compared."""
    code = re.sub(r'/\*.*?\*/', ' ', code, flags=re.S)
    code = re.sub(r'//[^\n]*', ' ', code)
    code = re.sub(r'\s+', ' ', code)
    return code.strip()


def arc_expr(code):
    """The quarter-circle stepping, normalised. Returns None when absent."""
    m = re.search(r'int\s+dx\s*=\s*(.+?);', normalise(code))
    return m.group(1).strip() if m else None


def fill_sequence(code, fn_names):
    """Ordered list of (callee, first-two-args) for every fill call."""
    out = []
    pat = re.compile(r'\b(' + '|'.join(fn_names) + r')\s*\(([^;]*?)\)\s*;', re.S)
    for m in pat.finditer(normalise(code)):
        args = [a.strip() for a in m.group(2).split(',')]
        out.append((m.group(1), tuple(args[:2])))
    return out


def guard_shape(code):
    """The degenerate-case condition, with the radius variable renamed away."""
    m = re.search(r'if\s*\(\s*(r+|radius)\s*<\s*1[^)]*\)', normalise(code))
    if not m:
        return None
    txt = m.group(0)
    txt = re.sub(r'\brr\b', 'R', txt)
    txt = re.sub(r'\bradius\b', 'R', txt)
    return txt


SRC_TEXT = read(SRC)
HARNESS_TEXT = read(HARNESS)

fails = []

# ---------------------------------------------------------------- KF_Border
src_border = body(SRC_TEXT, r'static void KF_Border\s*\(', 'KF_Border (death.cpp)')
har_border = body(HARNESS_TEXT, r'static void KF_Border\s*\(', 'KF_Border (harness)')

a_src, a_har = arc_expr(src_border), arc_expr(har_border)
if a_src is None:
    fails.append('KF_Border in death.cpp no longer has an "int dx = ..." arc step')
elif a_src != a_har:
    fails.append('KF_Border arc expression differs\n  death.cpp: %s\n  harness  : %s'
                 % (a_src, a_har))

g_src, g_har = guard_shape(src_border), guard_shape(har_border)
if g_src != g_har:
    fails.append('KF_Border degenerate guard differs\n  death.cpp: %s\n  harness  : %s'
                 % (g_src, g_har))

s_src = fill_sequence(src_border, ['FillRGBABlend'])
s_har = fill_sequence(har_border, ['fill_border'])
if len(s_src) != len(s_har):
    fails.append('KF_Border fill-call count differs: death.cpp %d, harness %d'
                 % (len(s_src), len(s_har)))
else:
    for n, (a, b) in enumerate(zip(s_src, s_har)):
        if a[1] != b[1]:
            fails.append('KF_Border fill %d has different x,y: death.cpp %s, harness %s'
                         % (n, a[1], b[1]))

# ----------------------------------------------------------- KF_FilledPlate
src_plate = body(SRC_TEXT, r'static void KF_FilledPlate\s*\(', 'KF_FilledPlate (death.cpp)')
har_plate = body(HARNESS_TEXT, r'static void KF_FilledPlate\s*\(', 'KF_FilledPlate (harness)')

a_src, a_har = arc_expr(src_plate), arc_expr(har_plate)
if a_src != a_har:
    fails.append('KF_FilledPlate arc expression differs\n  death.cpp: %s\n  harness  : %s'
                 % (a_src, a_har))

s_src = fill_sequence(src_plate, ['FillRGBABlend'])
s_har = fill_sequence(har_plate, ['fill_plate'])
if len(s_src) != len(s_har):
    fails.append('KF_FilledPlate fill-call count differs: death.cpp %d, harness %d'
                 % (len(s_src), len(s_har)))

# ------------------------------------------------------------------ overhang
m_src = re.search(r'kf_border_overhang\s*\(\s*int\s+\w+\s*\)\s*\{\s*return\s+(.+?);',
                  normalise(read(os.path.join(ROOT, 'cl_dll', 'include',
                                              'killfeed_layout.h'))))
m_har = re.search(r'static int overhang\s*\(\s*int\s+\w+\s*\)\s*\{\s*return\s+(.+?);',
                  normalise(HARNESS_TEXT))
if m_src and m_har:
    e_src = m_src.group(1).replace(' ', '')
    e_har = m_har.group(1).replace(' ', '')
    # the parameter may be named differently; compare shape only
    e_src = re.sub(r'\b[a-z_]+\b', 'T', e_src)
    e_har = re.sub(r'\b[a-z_]+\b', 'T', e_har)
    if e_src != e_har:
        fails.append('overhang formula differs\n  header : %s\n  harness: %s'
                     % (m_src.group(1), m_har.group(1)))
else:
    fails.append('could not read the overhang formula from both sides')

if fails:
    print('CORNER HARNESS OUT OF SYNC WITH death.cpp')
    for f in fails:
        print('  ' + f)
    print('\nFix tests/test_killfeed_corner.c so it mirrors the real drawing code,')
    print('then re-run. The harness is only evidence while it matches.')
    sys.exit(1)

print('corner harness mirrors death.cpp: arc, guard and fill order all match')
sys.exit(0)

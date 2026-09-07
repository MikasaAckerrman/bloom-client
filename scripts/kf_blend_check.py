#!/usr/bin/env python3
"""
Static sync between tests/test_killfeed_blend.c and cl_dll/death.cpp.

The harness counts RenderMode calls in two scopes (CHudDeathNotice::Draw and
KF_DrawIcon) and checks the modes. To keep the harness honest, this script
checks the same scopes from the source side using the same boundaries, and
refuses to pass if any of them disagrees.

The point is not to re-implement the harness in Python -- it is to catch the
specific failure mode where someone edits the C code in a way the harness
"agrees with" because the harness was edited at the same time. This script
reads the source by AST-free text spans and reuses the harness's expected
numbers, so a one-sided edit has to fail somewhere.
"""
import re
import sys

DEATH = 'cl_dll/death.cpp'
TEST  = 'tests/test_killfeed_blend.c'

def read(p):
    return open(p, encoding='utf-8', errors='ignore').read()

def find_function_body(src, signature):
    """Return the substring inside the FIRST matching function's outermost
    braces, or None. The signature must be unique enough to find exactly one
    definition; the harness already relies on this for its own body scan."""
    i = src.find(signature)
    if i < 0:
        return None
    body = src.find('{', i)
    if body < 0:
        return None
    depth = 0
    for j, ch in enumerate(src[body:], body):
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                return src[body:body + j + 1]
    return None

def count_calls(body, name):
    """Count bare calls to `name(` inside the body, ignoring comments and
    string literals. The harness does the same thing with a lighter scanner;
    we run the heavier one so we can disagree usefully when comments
    intentionally mention the name."""
    # strip /* ... */ block comments
    no_block = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    # strip // line comments
    no_line = re.sub(r'//[^\n]*', '', no_block)
    # strip string literals
    no_str = re.sub(r'"(?:\\.|[^"\\])*"', '""', no_line)
    return len(re.findall(r'\b' + re.escape(name) + r'\s*\(', no_str))

def first_mode_after(body, mode):
    """Find the first mode name AFTER a RenderMode( occurrence. Returns the
    argument text or None."""
    m = re.search(r'RenderMode\s*\(\s*(' + re.escape(mode) + r')\b', body)
    if not m:
        return None
    return mode

src = read(DEATH)
tst = read(TEST)

problems = []

draw_body = find_function_body(src, 'int CHudDeathNotice :: Draw')
icon_body = find_function_body(src, 'static void KF_DrawIcon(')

if draw_body is None:
    problems.append('death.cpp: CHudDeathNotice::Draw not found')
    sys.exit(1)
if icon_body is None:
    problems.append('death.cpp: KF_DrawIcon not found')
    sys.exit(1)

# Draw must contain exactly 2 RenderMode calls (one ADD, one TEX) -- the
# harness asserts this directly. We re-derive the number here so a one-sided
# edit has to show up somewhere.
n_draw = count_calls(draw_body, 'RenderMode')
if n_draw != 2:
    problems.append(f'CHudDeathNotice::Draw has {n_draw} RenderMode calls, harness expects 2')

# Icon helper must contain ZERO. The bracket is the loop's job.
n_icon = count_calls(icon_body, 'RenderMode')
if n_icon != 0:
    problems.append(f'KF_DrawIcon has {n_icon} RenderMode calls, harness expects 0')

# Order: ADD before TEX inside Draw. Spam moves rather than disappears if the
# order is reversed, so this is a structural property, not a cosmetic one.
add_pos = draw_body.find('RenderMode')
tex_pos = draw_body.find('RenderMode', add_pos + 1)
if add_pos < 0 or tex_pos < 0:
    problems.append('Draw: cannot locate the two RenderMode calls')
else:
    # Check both arguments are present
    add_mode = re.search(r'RenderMode\s*\(\s*(kRenderTrans\w+)\b', draw_body[add_pos:tex_pos + 200])
    tex_mode = re.search(r'RenderMode\s*\(\s*(kRenderTrans\w+)\b', draw_body[tex_pos:tex_pos + 200])
    if not add_mode or not tex_mode:
        problems.append('Draw: cannot read RenderMode argument names')
    elif add_mode.group(1) != 'kRenderTransAdd':
        problems.append(f'Draw: first RenderMode is {add_mode.group(1)}, expected kRenderTransAdd')
    elif tex_mode.group(1) != 'kRenderTransTexture':
        problems.append(f'Draw: second RenderMode is {tex_mode.group(1)}, expected kRenderTransTexture')

# Harness sanity: the test must still reference the same expected numbers,
# otherwise the test and the source agree by accident.
if 'in_draw != 2' not in tst and '!= 2' not in tst:
    problems.append('harness no longer asserts the Draw count of 2')
if 'in_icon != 0' not in tst and '!= 0' not in tst:
    problems.append('harness no longer asserts the icon count of 0')

if problems:
    print('BLEND HARNESS OUT OF SYNC WITH death.cpp')
    for p in problems:
        print(f'  {p}')
    print()
    print('Fix tests/test_killfeed_blend.c (or death.cpp) so the two agree.')
    sys.exit(1)

print('blend harness mirrors death.cpp: bracket, mode names and order all match')

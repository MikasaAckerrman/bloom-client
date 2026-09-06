#!/usr/bin/env python3
"""Prove tests/test_menu_probe.c still mirrors LoadMenuInterface() in cdll_int.cpp.

cdll_int.cpp needs the engine and mainui to compile, so the probe logic is
mirrored in the test. Checks:

  1. The once-only latch exists and guards the MESSAGE, not the lookup. If the
     latch moved above `g_pMenu = GetNativeMenuExports()`, a host that starts
     exporting the object mid-session would never be picked up.
  2. The optional case goes to Con_DPrintf (developer-only) and the genuine
     version mismatch to Con_Printf -- swapping them either spams every user or
     hides a real fault.
  3. Sys_Warn is NOT used: it raises a modal box on Android, and an optional
     feature must not interrupt startup.
  4. The three branches in the source and the harness are in the same order with
     the same conditions.
"""
import re
import sys

SRC = 'cl_dll/cdll_int.cpp'
TEST = 'tests/test_menu_probe.c'

problems = []


def read(path):
    try:
        return open(path, encoding='utf-8', errors='replace').read()
    except OSError as e:
        problems.append(f'cannot read {path}: {e}')
        return ''


src_all = read(SRC)
test_all = read(TEST)
if problems:
    print('\n'.join(problems))
    sys.exit(1)


def body(text, signature):
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


def strip(s):
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    return s


src = body(src_all, 'static void LoadMenuInterface( void )')
test = body(test_all, 'static void LoadMenuInterface( void )')
if src is None:
    problems.append('LoadMenuInterface() not found in ' + SRC)
if test is None:
    problems.append('LoadMenuInterface() mirror not found in ' + TEST)

if not problems:
    src_c = strip(src)
    test_c = strip(test)

    # --- 1. latch exists, and the lookup happens BEFORE it ---------------
    for label, code in (('death.cpp' if False else SRC, src_c), (TEST, test_c)):
        if 's_menuProbed' not in code:
            problems.append(f'{label}: the once-only latch (s_menuProbed) is gone '
                            f'-- the message will be printed on every connect')
            continue
        i_lookup = code.find('GetNativeMenuExports()')
        # Look for where the latch is TESTED, not where it is declared: the
        # source declares `static bool s_menuProbed = false;` at the top of the
        # function, which is before the lookup and perfectly correct.
        m_latch = re.search(r'if\([^)]*s_menuProbed', code)
        i_latch = m_latch.start() if m_latch else -1
        if i_lookup < 0:
            problems.append(f'{label}: GetNativeMenuExports() is not called')
        elif i_latch < 0:
            problems.append(f'{label}: s_menuProbed is never tested in a condition')
        elif i_latch < i_lookup:
            problems.append(f'{label}: the latch is checked BEFORE the lookup -- a host '
                            f'that starts exporting MenuFactory later would never be '
                            f'picked up')
        # the latch must be consulted together with g_pMenu in one early-out
        if not re.search(r'if\(\s*g_pMenu\s*\|\|\s*s_menuProbed\s*\)', code):
            problems.append(f'{label}: expected `if( g_pMenu || s_menuProbed ) return;` '
                            f'after the lookup')

    # --- 2/3. which channel each branch uses ----------------------------
    n_dprint = len(re.findall(r'Con_DPrintf', src_c))
    n_print = len(re.findall(r'Con_Printf', src_c))
    if n_dprint != 2:
        problems.append(f'{SRC}: expected 2 Con_DPrintf branches (no mobile API, '
                        f'no MenuFactory), found {n_dprint}')
    if n_print != 1:
        problems.append(f'{SRC}: expected exactly 1 Con_Printf branch (interface '
                        f'mismatch), found {n_print}')
    if 'Sys_Warn' in src_c:
        problems.append(f'{SRC}: Sys_Warn is back -- that is a modal box on Android '
                        f'for an OPTIONAL feature')

    # Sys_Warn must be gone from the whole file, not just this function.
    if 'pfnSys_Warn' in strip(src_all):
        problems.append(f'{SRC}: pfnSys_Warn still referenced in the file')

    # --- 4. branch order and conditions match ---------------------------
    def branches(code):
        """Conditions of each if/else-if, with nested parentheses kept.

        A plain [^)]* stops at the first ')', which truncates a condition like
        `!gMobileAPI.pfnGetNativeObject( "MenuFactory" )` mid-way and reports a
        phantom mismatch. Match the parentheses properly instead.
        """
        out = []
        for m in re.finditer(r'(?:else\s+)?if\s*\(', code):
            i = m.end() - 1
            depth, j = 0, i
            while j < len(code):
                if code[j] == '(':
                    depth += 1
                elif code[j] == ')':
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            out.append(re.sub(r'\s+', '', code[i + 1:j]))
        return out

    # GetNativeMenuExports must be compared too. Checking only LoadMenuInterface
    # left the lookup's own early-outs unverified: a harness that dropped the
    # "host exports no MenuFactory" test still passed, because that condition
    # lives in the OTHER function. Found by scripts/negctl_menu.sh.
    src_lookup = body(src_all, 'static IGameMenuExports *GetNativeMenuExports( void )')
    test_lookup = body(test_all, 'static void *GetNativeMenuExports( void )')
    if src_lookup is None:
        problems.append(f'{SRC}: GetNativeMenuExports() not found')
    elif test_lookup is None:
        problems.append(f'{TEST}: GetNativeMenuExports() mirror not found')
    else:
        s_l = [c.replace('!g_iMobileAPIVersion||!gMobileAPI.pfnGetNativeObject',
                         '!g_mobileAPI||!g_haveGetNative')
               for c in branches(strip(src_lookup))]
        t_l = branches(strip(test_lookup))
        # The source asks the host once and null-checks the result; the harness
        # models the same two gates with flags. Compare the FIRST guard verbatim
        # and require the harness to still gate on the factory's presence.
        if not s_l or not t_l or s_l[0] != t_l[0]:
            problems.append('GetNativeMenuExports: first guard differs\n'
                            f'    source: {s_l[:1]}\n'
                            f'    test:   {t_l[:1]}')
        if 'g_hostHasFactory' not in strip(test_lookup):
            problems.append(f'{TEST}: GetNativeMenuExports mirror no longer checks '
                            f'whether the host exports the factory')
        if 'nativeFactory' not in strip(src_lookup):
            problems.append(f'{SRC}: GetNativeMenuExports no longer probes '
                            f'"MenuFactory" before casting')

    sb, tb = branches(src_c), branches(test_c)
    # normalise the source's spellings to the harness's fakes
    subs = [
        ('!g_iMobileAPIVersion||!gMobileAPI.pfnGetNativeObject', '!g_mobileAPI||!g_haveGetNative'),
        ('!gMobileAPI.pfnGetNativeObject("MenuFactory")', '!g_hostHasFactory'),
    ]
    sb_n = []
    for c in sb:
        for frm, to in subs:
            c = c.replace(frm, to)
        sb_n.append(c)
    if sb_n != tb:
        problems.append('branch structure differs between source and harness\n'
                        f'    source: {sb_n}\n'
                        f'    test:   {tb}')

if problems:
    print('MENU PROBE HARNESS OUT OF SYNC WITH cdll_int.cpp')
    for p in problems:
        print('  ' + p)
    print('\nFix tests/test_menu_probe.c (or cdll_int.cpp) so the two agree.')
    sys.exit(1)

print('menu probe harness mirrors cdll_int.cpp: latch, channels and branches match')

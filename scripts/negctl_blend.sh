#!/bin/sh
# Negative control: each mutation is applied by a python helper because BusyBox
# sed misparses `||` and backslashed newlines, silently turning some mutations
# into no-ops -- which would look like a pass. The python helper asserts the
# pattern was found and the file changed before reporting success.
set -e
PASS=0; FAIL=0
DEATH=cl_dll/death.cpp
TEST=tests/test_killfeed_blend.c
BIN=/tmp/kfb_negctl

check() {
    name="$1"; expect="$2"
    if [ "$expect" = pass ] && sh -c "$3" >/tmp/_nc.log 2>&1; then
        echo "ok    $name"; PASS=$((PASS+1))
    elif [ "$expect" = fail ] && sh -c "$3" >/tmp/_nc.log 2>&1; then
        echo "FAIL  $name (expected rejection)"; FAIL=$((FAIL+1)); grep -n FAIL /tmp/_nc.log | head -2
    elif [ "$expect" = pass ]; then
        echo "FAIL  $name"; FAIL=$((FAIL+1)); cat /tmp/_nc.log
    else
        echo "ok    $name"; PASS=$((PASS+1))
    fi
}

baseline_pass() {
    gcc -w -o "$BIN" "$TEST"
    "$BIN" >/dev/null
}

# 0. baseline: original is green
check "baseline: original passes" pass "cd $(pwd) && gcc -w -o $BIN $TEST && $BIN >/dev/null"

# 1. restore per-icon toggle (the original spam source)
cp "$DEATH" /tmp/death.bak
python3 - <<'PY'
import re
src=open('cl_dll/death.cpp').read()
i=src.find('BLEND STATE LIVES ON THE CALLER')
j=src.find('gEngfuncs.pfnSPR_DrawGeneric( 0, x, y, NULL, 1, 1, w, h );', i)
old=src[j:j+len('gEngfuncs.pfnSPR_DrawGeneric( 0, x, y, NULL, 1, 1, w, h );')]
new=("if( gEngfuncs.pTriAPI ) gEngfuncs.pTriAPI->RenderMode( kRenderTransAdd );\n\t"
     + old + "\n\t"
     + "if( gEngfuncs.pTriAPI ) gEngfuncs.pTriAPI->RenderMode( kRenderTransTexture );\n\t")
src=src.replace(old, new, 1)
assert 'RenderMode( kRenderTransAdd )' in src
open('cl_dll/death.cpp','w').write(src)
PY
check "per-icon toggle restored -> rejected" fail "cd $(pwd) && gcc -w -o $BIN $TEST && $BIN >/tmp/_nc.log 2>&1"
cp /tmp/death.bak "$DEATH"

# 2. remove the closing TEX call (asymmetric bracket)
cp "$DEATH" /tmp/death.bak
python3 - <<'PY'
src=open('cl_dll/death.cpp').read()
src=src.replace(
    'if( needBlendBracket && gEngfuncs.pTriAPI )\n\t\tgEngfuncs.pTriAPI->RenderMode( kRenderTransTexture );',
    '// removed closing bracket', 1)
assert 'removed closing bracket' in src
open('cl_dll/death.cpp','w').write(src)
PY
check "closing TEX call removed -> rejected" fail "cd $(pwd) && gcc -w -o $BIN $TEST && $BIN >/tmp/_nc.log 2>&1"
cp /tmp/death.bak "$DEATH"

# 3. swap ADD and TEX (spam moves, does not vanish)
cp "$DEATH" /tmp/death.bak
python3 - <<'PY'
src=open('cl_dll/death.cpp').read()
add='gEngfuncs.pTriAPI->RenderMode( kRenderTransAdd );'
tex='gEngfuncs.pTriAPI->RenderMode( kRenderTransTexture );'
# only swap the ones in Draw (last occurrences are the bracket pair)
# find the position right after the comment "ONE render-mode bracket"
i=src.find('ONE render-mode bracket')
j=src.find(add, i); k=src.find(tex, j)
assert i>0 and j>0 and k>0
chunk=src[j:k+len(tex)]
swapped=chunk.replace(add,'@@ADD@@').replace(tex,add).replace('@@ADD@@',tex)
src=src[:j]+swapped+src[k+len(tex):]
open('cl_dll/death.cpp','w').write(src)
PY
check "ADD and TEX swapped -> rejected" fail "cd $(pwd) && gcc -w -o $BIN $TEST && $BIN >/tmp/_nc.log 2>&1"
cp /tmp/death.bak "$DEATH"

# 4. bracket uses wrong mode name
cp "$DEATH" /tmp/death.bak
python3 - <<'PY'
src=open('cl_dll/death.cpp').read()
src=src.replace('RenderMode( kRenderTransAdd )','RenderMode( kRenderTransTexture )',1)
open('cl_dll/death.cpp','w').write(src)
PY
check "ADD replaced with wrong mode -> rejected" fail "cd $(pwd) && gcc -w -o $BIN $TEST && $BIN >/tmp/_nc.log 2>&1"
cp /tmp/death.bak "$DEATH"

# 5. KF_DrawIcon contains a RenderMode call again (the helper leaked a toggle)
cp "$DEATH" /tmp/death.bak
python3 - <<'PY'
src=open('cl_dll/death.cpp').read()
i=src.find('static void KF_DrawIcon(')
j=src.find('gEngfuncs.pfnSPR_DrawGeneric( 0, x, y, NULL, 1, 1, w, h );', i)
old=src[j:j+len('gEngfuncs.pfnSPR_DrawGeneric( 0, x, y, NULL, 1, 1, w, h );')]
new="if( gEngfuncs.pTriAPI ) gEngfuncs.pTriAPI->RenderMode( kRenderTransAdd );\n\t"+old
src=src.replace(old, new, 1)
open('cl_dll/death.cpp','w').write(src)
PY
check "KF_DrawIcon leaks a per-icon RenderMode -> rejected" fail "cd $(pwd) && gcc -w -o $BIN $TEST && $BIN >/tmp/_nc.log 2>&1"
cp /tmp/death.bak "$DEATH"

baseline_pass >/dev/null 2>&1

echo
echo "negctl_blend: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]

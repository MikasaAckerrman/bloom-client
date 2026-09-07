#!/bin/sh
# Negative control for kf_blend_check.py. Every mutation is applied by python
# because BusyBox sed misparses `||` and `\n`. Each call asserts the source
# was actually modified, otherwise the test would silently pass.
set -e
PASS=0; FAIL=0
DEATH=cl_dll/death.cpp
SCR=scripts/kf_blend_check.py

check() {
    name="$1"; expect="$2"
    if [ "$expect" = pass ] && python3 "$SCR" >/tmp/_nc.log 2>&1; then
        echo "ok    $name"; PASS=$((PASS+1))
    elif [ "$expect" = fail ] && python3 "$SCR" >/tmp/_nc.log 2>&1; then
        echo "FAIL  $name (expected rejection)"; FAIL=$((FAIL+1)); head -3 /tmp/_nc.log
    elif [ "$expect" = pass ]; then
        echo "FAIL  $name"; FAIL=$((FAIL+1)); cat /tmp/_nc.log
    else
        echo "ok    $name"; PASS=$((PASS+1)); head -1 /tmp/_nc.log
    fi
}

cp "$DEATH" /tmp/death.bak

# 1. baseline: original is green
check "baseline: original passes" pass

# 2. add a third RenderMode to Draw
python3 -c "
src=open('$DEATH').read()
i=src.find('int CHudDeathNotice :: Draw')
j=src.find('{', i)
src=src[:j+1]+'\n\tif( gEngfuncs.pTriAPI ) gEngfuncs.pTriAPI->RenderMode( kRenderTransAdd );\n'+src[j+1:]
assert 'RenderMode' in src[j+1:j+200]
open('$DEATH','w').write(src)
"
check "Draw has a third RenderMode call -> rejected" fail
cp /tmp/death.bak "$DEATH"

# 3. swap ADD and TEX
python3 -c "
src=open('$DEATH').read()
add='RenderMode( kRenderTransAdd )'; tex='RenderMode( kRenderTransTexture )'
i=src.find('ONE render-mode bracket')
ja=src.find(add, i); jt=src.find(tex, ja)
assert i>0 and ja>0 and jt>0
chunk=src[ja:jt+len(tex)]
swapped=chunk.replace(add,'@@A@@').replace(tex,add).replace('@@A@@',tex)
src=src[:ja]+swapped+src[jt+len(tex):]
open('$DEATH','w').write(src)
"
check "ADD and TEX swapped -> rejected" fail
cp /tmp/death.bak "$DEATH"

# 4. wrong mode name in opening bracket
python3 -c "
src=open('$DEATH').read()
old='if( needBlendBracket && gEngfuncs.pTriAPI )\n\t\tgEngfuncs.pTriAPI->RenderMode( kRenderTransAdd );'
new='if( needBlendBracket && gEngfuncs.pTriAPI )\n\t\tgEngfuncs.pTriAPI->RenderMode( kRenderNormal );'
assert old in src
src=src.replace(old,new,1)
open('$DEATH','w').write(src)
"
check "opening bracket uses wrong mode -> rejected" fail
cp /tmp/death.bak "$DEATH"

# 5. KF_DrawIcon gains a RenderMode (helper leaked a toggle)
python3 -c "
src=open('$DEATH').read()
i=src.find('static void KF_DrawIcon(')
j=src.find('gEngfuncs.pfnSPR_DrawGeneric( 0, x, y, NULL, 1, 1, w, h );', i)
old=src[j:j+66]
new='if( gEngfuncs.pTriAPI ) gEngfuncs.pTriAPI->RenderMode( kRenderTransAdd );\n\t'+old
src=src.replace(old,new,1)
open('$DEATH','w').write(src)
"
check "KF_DrawIcon contains a RenderMode -> rejected" fail
cp /tmp/death.bak "$DEATH"

# 6. harness drops the in_draw != 2 check (test and source agree by accident)
cp tests/test_killfeed_blend.c /tmp/h.bak
python3 -c "
src=open('tests/test_killfeed_blend.c').read()
src=src.replace('in_draw != 2','in_draw != 7',1)
open('tests/test_killfeed_blend.c','w').write(src)
"
check "harness no longer asserts Draw count of 2 -> rejected" fail
cp /tmp/h.bak tests/test_killfeed_blend.c

# 7. harness drops the in_icon != 0 check
python3 -c "
src=open('tests/test_killfeed_blend.c').read()
src=src.replace('in_icon != 0','in_icon != 9',1)
open('tests/test_killfeed_blend.c','w').write(src)
"
check "harness no longer asserts icon count of 0 -> rejected" fail
cp /tmp/h.bak tests/test_killfeed_blend.c

echo
echo "negctl_blend_check: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]

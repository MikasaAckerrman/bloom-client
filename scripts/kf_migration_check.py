#!/usr/bin/env python3
"""kf_migration_check.py -- the migration block in tests/test_killfeed_migration.c
must list the SAME keys/prev/new defaults as the block in cl_dll/death.cpp.
Catches the two copies drifting apart (the whole reason migration exists)."""
import re, sys

DEATH = 'cl_dll/death.cpp'
TEST  = 'tests/test_killfeed_migration.c'

def block_after(src, marker):
    i = src.index(marker)
    # the block ends at the closing "}" of the outer scope: find the version
    # cvar line, the migration ends right after its Cvar_Set
    j = src.index('cl_killfeed_cfgversion", "2"', i)
    return src[i:j]

def lists_from(src):
    keys  = re.findall(r'"(cl_killfeed_[a-z_]+)"', src)
    return keys

d = open(DEATH).read()
t = open(TEST).read()

dblock = block_after(d, 'KF_DEFAULT_KEYS[]')
tblock = block_after(t, 'KF_DEFAULT_KEYS[]')

dk = [k for k in lists_from(dblock) if k != 'cl_killfeed_cfgversion']
tk = [k for k in lists_from(tblock) if k != 'cl_killfeed_cfgversion']

# death.cpp lists keys 3 times (KEYS/PREV context), test once -- compare SETS
dk_set, tk_set = set(dk), set(tk)

fails = 0
only_d = dk_set - tk_set
only_t = tk_set - dk_set
if only_d:
    print('FAIL: keys only in death.cpp:', sorted(only_d)); fails += 1
if only_t:
    print('FAIL: keys only in test:', sorted(only_t)); fails += 1

# prev/new default rows must be identical
dprev = re.search(r'KF_PREV_DEFAULTS\[\]\s*=\s*\{(.*?)\}', d, re.S).group(1)
dnew  = re.search(r'KF_NEW_DEFAULTS\[\]\s*=\s*\{(.*?)\}', d, re.S).group(1)
tprev = re.search(r'KF_PREV_DEFAULTS\[\]\s*=\s*\{(.*?)\}', t, re.S).group(1)
tnew  = re.search(r'KF_NEW_DEFAULTS\[\]\s*=\s*\{(.*?)\}', t, re.S).group(1)
norm = lambda s: [x.strip().strip('"') for x in s.split(',') if x.strip()]
if norm(dprev) != norm(tprev):
    print('FAIL: PREV defaults differ'); print(' death:', norm(dprev)); print(' test :', norm(tprev)); fails += 1
if norm(dnew) != norm(tnew):
    print('FAIL: NEW defaults differ'); print(' death:', norm(dnew)); print(' test :', norm(tnew)); fails += 1

# the version stamp cvar and the <2.0 gate must exist in both
for src, name in ((d, 'death.cpp'), (t, 'test')):
    if 'cl_killfeed_cfgversion' not in src:
        print('FAIL: no version cvar in', name); fails += 1
    if '2.0f' not in src:
        print('FAIL: no version gate in', name); fails += 1

if fails:
    print('MIGRATION MIRROR: %d failure(s)' % fails); sys.exit(1)
print('migration mirror: keys/prev/new/gate match death.cpp <-> test')

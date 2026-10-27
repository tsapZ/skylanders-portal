# Group the output of list.py into runs of consecutive actions with the same bound targets.
# Usage: python3 group.py <list out.txt>
import sys, re
rows = []
for line in open(sys.argv[1], encoding='utf-8'):
    m = re.match(r'\s*(\d+)\s+(\S+)(.*)', line)
    if not m: continue
    eid, name, rest = int(m.group(1)), m.group(2), m.group(3)
    outs = re.search(r'out=(\S+)', rest); dep = re.search(r'depth=(\S+)', rest)
    key = ((outs.group(1) if outs else '-'), (dep.group(1) if dep else '-'))
    kind = name.split('::')[-1].split('(')[0]
    rows.append((eid, kind, key))
groups = []
for eid, kind, key in rows:
    if groups and groups[-1][2] == key:
        g = groups[-1]; g[1] = eid; g[3][kind] = g[3].get(kind, 0) + 1
    else:
        groups.append([eid, eid, key, {kind: 1}])
for a, b, key, kinds in groups:
    o = key[0] if len(key[0]) < 60 else key[0][:57] + '...'
    print(f"{a:5}-{b:5}  out={o:42} depth={key[1]:34} {kinds}")

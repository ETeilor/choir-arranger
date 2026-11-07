"""Every style x every test file x several levels: no errors, parts in range, statistics."""
import subprocess, sys, glob, collections
cli, root = sys.argv[1], sys.argv[2]
styles = ['znamenny', 'byzantine', 'renaissance', 'contemporary', 'gospel', 'barbershop', 'epic']
extra = {'znamenny': ['--octavists'], 'byzantine': []}
bad = 0
for st in styles:
    notes = collections.Counter(); issues = 0; parts = set(); runs = 0
    for f in sorted(glob.glob(f'{root}/input/*.mid')):
        for L in ('0', '2', '4', '6', '8', '10'):
            r = subprocess.run([cli, f, '--dump', f'--style={st}', f'--complexity={L}'] + extra.get(st, []),
                               capture_output=True, text=True)
            if r.returncode != 0:
                bad += 1
                print('FAIL', st, f, L, r.stderr.strip()[:200]); continue
            runs += 1
            cur = None
            for line in r.stdout.split('\n'):
                if line.startswith('ISSUES'): issues += int(line.split()[1])
                elif line.startswith('TRACK'):
                    cur = ' '.join(line.split()[1:-1]); parts.add(cur)
                elif line.strip() and cur:
                    a, b, p = map(int, line.split())
                    if b <= a: print('BAD NOTE', st, f, L, cur, a, b, p); bad += 1
                    notes[L] += 1
    print(f'{st:13s} runs {runs:3d} issues {issues:4d} parts {sorted(parts)}')
    print(' ' * 14, 'notes by level', dict(sorted(notes.items(), key=lambda x: int(x[0]))))
print('failures', bad)

import subprocess, sys
N = ['C','C#','D','Eb','E','F','F#','G','Ab','A','Bb','B']
nm = lambda p: N[p % 12] + str(p // 12 - 1)
cli, f, st, L = sys.argv[1:5]
out = subprocess.run([cli, f, f'--style={st}', f'--complexity={L}'] + sys.argv[5:], capture_output=True, text=True).stdout
rep = [l for l in out.split('\n') if l.startswith(('Лад', 'Аккорды', 'Фигуры', '!'))]
print(f'--- {st} L{L}:', ' / '.join(rep))
dump = subprocess.run([cli, f, '--dump', f'--style={st}', f'--complexity={L}'] + sys.argv[5:], capture_output=True, text=True).stdout
cur = None; rows = {}
for l in dump.split('\n'):
    if l.startswith('TRACK'): cur = ' '.join(l.split()[1:-1]); rows[cur] = []
    elif l.strip() and cur and not l.startswith('ISSUES'):
        a, b, p = map(int, l.split()); rows[cur].append(f'{a/480:g}:{nm(p)}')
for k, v in rows.items(): print(f'  {k:10s}', ' '.join(v))

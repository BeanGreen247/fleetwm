import random, glob, re, sys
random.seed(int(sys.argv[1]) if len(sys.argv)>1 else 7)
base = open('base.txt').read()
cases = [c.strip('\n') for c in base.split('\n') if c.strip() or True]
# group lines into the snippets above (each line a case) plus multi-line cases built from real config files
files = glob.glob('/home/kenny/.config/fleetwm/*.toml') + glob.glob('/home/kenny/git/fleetwm/config/*.toml') + glob.glob('/home/kenny/git/fleetwm/**/*.toml', recursive=True)
docs = []
for f in files:
    try: docs.append(open(f).read())
    except: pass
# snippets from tests: strings written to .toml in tests (out << "...")
for f in glob.glob('/home/kenny/git/fleetwm/tests/*.cpp'):
    t = open(f).read()
    for m in re.finditer(r'out << ((?:"(?:[^"\\]|\\.)*"\s*)+);', t):
        s = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
        s = bytes(s, 'utf-8').decode('unicode_escape', errors='ignore')
        docs.append(s)
# multi-line combos
lines = [l for l in base.split('\n') if l.strip()]
for _ in range(300):
    docs.append('\n'.join(random.sample(lines, random.randint(1, 5))))
allc = [c for c in cases if c] + docs
# mutations
muts = []
for d in allc:
    for _ in range(int(sys.argv[2]) if len(sys.argv)>2 else 6):
        if not d: continue
        s = list(d); k = random.randint(0, len(s) - 1); op = random.random()
        if op < 0.33: del s[k]
        elif op < 0.66: s.insert(k, random.choice('"\'[]{}=,.#\n\\ _-+0aAZ'))
        else: s[k] = random.choice('"\'[]{}=,.#\n\\ _-+0aAZ')
        muts.append(''.join(s))
final = allc + muts
sys.stdout.write('\x01\x01\x01\n'.join(final))
print(len(final), file=sys.stderr)

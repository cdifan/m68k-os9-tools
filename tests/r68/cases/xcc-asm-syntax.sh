# m68k-os9-xcc -a writes r68 syntax (through gastor68), as Microware's xcc -a does; -obj=gas
# writes GNU as syntax (GCC's -S), -obj=r68 r68's, and -asm=r68|gas sets both the syntax -a
# writes (when no -obj= says) and that of .a inputs, which -asm=auto (the default) detects
# (asmsyntax.c: the first decisive statement; any line end; undecided: r68). A gas .a goes to
# GCC's assembler, an r68 one to m68k-os9-r68. -obj=gas or -obj=r68 without -a is an error.
# Ultra C mode's -e=be writes it through gastor68 -mode=ucc (psect t.c); Microware's r68 takes
# -a's output (where run.sh has R68). Before, -a wrote gas syntax and every .a went to
# m68k-os9-r68.
# fixed: f527463 2026-10-18 xcc -a writes r68 syntax (gastor68); -asm= and .a inputs of either syntax
cat > t.c <<'EOF'
int g = 3;
static char buf[10];
int f(int a) { buf[0] = a; return a * g + 1; }
EOF
xcc() { m68k-os9-xcc -q "$@"; }
xcc -a t.c > a1.log 2>&1 && mv t.a r.a || why="$why xcc -a failed: $(head -1 a1.log);"
grep -q '^ psect t_c,' r.a || why="$why -a wrote no psect line;"
grep -q '%' r.a && why="$why -a wrote % registers;"
xcc -a -obj=gas t.c > a2.log 2>&1 && mv t.a g.a || why="$why xcc -a -obj=gas failed: $(head -1 a2.log);"
grep -q '^[[:space:]]*\.text' g.a || why="$why -a -obj=gas wrote no .text;"
xcc -a -asm=gas t.c > a3.log 2>&1 && grep -q '^[[:space:]]*\.text' t.a || why="$why -a -asm=gas: not gas;"
xcc -a -asm=gas -obj=r68 t.c > a4.log 2>&1 && grep -q '^ psect' t.a || why="$why -asm=gas -obj=r68: not r68;"
# .a inputs, detected; also with CR LF and with CR (OS-9's) line ends
tr '\n' '\r' < r.a > rcr.a
sed 's/$/\r/' g.a > gcrlf.a
for a in r g rcr gcrlf; do
	xcc -r $a.a > $a.log 2>&1 || why="$why xcc -r $a.a failed: $(head -1 $a.log);"
	[ "$(od -An -c -N4 $a.r 2>/dev/null | tr -d ' ')" = '177ELF' ] || why="$why $a.r isn't ELF;"
	m68k-elfos9-nm $a.r 2>/dev/null | grep -q ' T f$' || why="$why $a.r has no f;"
done
# undecided (no directive of either kind): r68
printf 'Val equ 5\n' > u.a
xcc -r u.a > u.log 2>&1 || why="$why an undecided .a wasn't taken as r68: $(head -1 u.log);"
# forced
xcc -asm=r68 -r g.a > f1.log 2>&1 && why="$why a gas .a assembled as r68;"
xcc -asm=gas -obj=rof -r g.a > f2.log 2>&1 || why="$why -asm=gas -obj=rof -r g.a failed: $(head -1 f2.log);"
[ "$(od -An -tx1 -N4 g.r | tr -d ' ')" = deadface ] || why="$why g.r isn't a ROF;"
xcc -obj=gas t.c > e1.log 2>&1 && why="$why -obj=gas without -a accepted;"
grep -q -- '-obj=gas goes with -a' e1.log || why="$why no -obj=gas error: $(head -1 e1.log);"
xcc -asm=xyz -a t.c > e2.log 2>&1 && why="$why -asm=xyz accepted;"
# Ultra C mode: -e=be writes the .a, through gastor68 -mode=ucc (Ultra C names the psect t.c)
mkdir -p ucc mw/SRC/DEFS mw/OS9/SRC/DEFS	# a fake MWOS tree: t.c includes nothing
MWOS=$PWD/mw m68k-os9-xcc -mode=ucc -e=be=ucc t.c > u1.log 2>&1 || why="$why -mode=ucc -e=be failed: $(head -1 u1.log);"
grep -q '^ psect t\.c,' ucc/t.a 2>/dev/null || why="$why -mode=ucc -e=be: no psect t.c line;"
# Microware's r68 takes -a's output (where run.sh has it, R68)
if [ -n "${R68:-}" ]; then
	cp r.a mwr.a
	mwasm mwr
fi

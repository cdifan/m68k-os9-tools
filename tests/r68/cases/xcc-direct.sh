# C 3.2's storage class direct (m68k-os9-xcc, C 3.2 mode; Ultra C's K&R mode, m68k-os9-ucc -bc,
# takes it too): accepted and left out, as it changes nothing in C 3.2. Only where it is a storage
# class, before another word: struct direct (a tag, as in Microware's dir.h), a variable named
# direct and the word in strings and comments stay. Before, GCC rejected it.
# fixed: 5856914 2026-10-23 m68k-os9-xcc: C 3.2's @ lines, #asm blocks and storage class direct
cat > dv.c <<'EOF'
struct direct { int d; };
struct direct *pd;
direct int dv;
static direct char dc;
int direct;
char *s = "direct int";	/* direct int */
int f() { struct direct x; x.d = dv + dc + direct; return x.d + s[0]; }
EOF
m68k-os9-xcc -q -r dv.c > c32.log 2>&1 || why="$why xcc: $(grep -m1 -i error c32.log || head -1 c32.log);"
${B}nm dv.r > nm.txt 2>&1
for s in " B dv" " B direct" " D s" " B pd"; do
	grep -q -- "$s\$" nm.txt || why="$why xcc: no \"$s\": $(tr '\n' ' ' < nm.txt);"
done
grep -q -a 'direct int' dv.r || why="$why xcc: the string changed;"
mkdir -p mw/SRC/DEFS mw/OS9/SRC/DEFS
MWOS=$PWD/mw m68k-os9-ucc -bc -e=as dv.c > ucc.log 2>&1 || why="$why ucc -bc: $(grep -m1 -i error ucc.log || head -1 ucc.log);"

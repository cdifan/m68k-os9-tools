# C 3.2's assembler text in C sources (m68k-os9-xcc, C 3.2 mode), as C 3.2's xcc passes it into
# its assembler output, where it stands: a line starting with @ (the rest of the line) and the
# lines between #asm and #endasm, where #include is expanded but no macro; at file level and inside
# a function (a note there: the text can't rely on C 3.2's registers or frame). A block in a
# conditional left out is left out; -a writes r68 source with the text in place; -r assembles it.
# Also C 3.2's storage class direct, accepted (it changes nothing in C 3.2). Before, GCC rejected
# all three.
# fixed: 5856914 2026-10-23 m68k-os9-xcc: C 3.2's @ lines, #asm blocks and storage class direct
cat > part.a <<'EOF'
inceq: equ 4
EOF
cat > blk.c <<'EOF'
#define SEVEN 7
@_sysedit: equ 5
direct int dv;
#asm
blkeq: equ 3
* SEVEN
#include "part.a"
#endasm
#ifndef __GNUC__
#asm
#include "missing.a"
gone: equ 1
#endasm
#endif
int f(a) int a;
{
	int r = a + dv;
@ moveq #9,d1
#asm
 moveq #11,d2
#endasm
	return r;
}
EOF
m68k-os9-xcc -a blk.c > a.log 2>&1 || why="$why -a failed: $(head -1 a.log);"
for want in "_sysedit: equ 5" "blkeq: equ 3" "inceq: equ 4" "* SEVEN" " moveq #9,d1" " moveq #11,d2"; do
	grep -q -F -x -- "$want" blk.a || why="$why -a: no line \"$want\";"
done
grep -q gone blk.a && why="$why -a: the block left out by #ifndef is there;"
[ "$(grep -c 'inside a function' a.log)" = 2 ] || why="$why not two notes for the blocks inside f: $(grep -c 'inside a function' a.log);"
# the function's text in place: after f's label, before its return
awk '/^f:/{f=1} f&&/moveq #9,d1/{print "ok"; exit}' blk.a | grep -q ok || why="$why -a: @ line not inside f;"
m68k-os9-xcc -q -r blk.c > r.log 2>&1 || why="$why -r failed: $(head -1 r.log);"
${B}nm blk.r > nm.txt 2>&1
for s in "00000005 A _sysedit" "00000003 A blkeq" "00000004 A inceq"; do
	grep -q -F -- "$s" nm.txt || why="$why -r: no \"$s\": $(tr '\n' ' ' < nm.txt);"
done
case $(section_hex blk.r .text) in *7209*740b*) ;; *) why="$why -r: the moveqs aren't in the code;";; esac
# GCC's diagnostics name the source's lines: an error after a block
sed 's/return r;/return r +;/' blk.c > bad.c
m68k-os9-xcc -q -r bad.c > bad.log 2>&1 && why="$why bad.c compiled;"
grep -q 'bad.c:22' bad.log || why="$why the error isn't at bad.c:22: $(grep -m1 error bad.log);"
# text with a label inside a function that GCC would copy at -o=2 (inlined twice; in a loop,
# loop header copying): once each
cat > dup.c <<'EOF'
int state;
static void here(n) int n;
{
	state = n;
#asm
lab1 nop
#endasm
}
int a(x) int x; { if (x) here(1); return x; }
int b(x) int x; { here(x); here(2); return x + 1; }
void c(n) int n;
{
	int i;
	for (i = 0; i < n; i++) {
		if (i & 1)
			state++;
#asm
lab2 nop
#endasm
	}
}
EOF
for o in 1 2; do
	m68k-os9-xcc -q -o=$o -a dup.c > dup$o.log 2>&1 || why="$why dup.c -o=$o: $(head -1 dup$o.log);"
	[ "$(grep -c '^lab' dup.a)" = 2 ] || why="$why dup.c -o=$o: the labels not once each: $(grep -c '^lab' dup.a);"
done
# #asm without #endasm; -a in gas syntax
printf '#asm\n nop\n' > open.c
m68k-os9-xcc -q -r open.c > open.log 2>&1 && why="$why open.c compiled;"
grep -q 'without #endasm' open.log || why="$why open.c: $(head -1 open.log);"
m68k-os9-xcc -q -a -obj=gas blk.c > gas.log 2>&1 && why="$why -a -obj=gas with blocks succeeded;"

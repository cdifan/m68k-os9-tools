# '\n' in m68k-os9-ucc's code: a CR (13) in Ultra C's default, K&R (-bc) and ANSI (-a) modes, as
# Ultra C's compiler makes it, and in m68k-os9-xcc (C 3.2); before, ucc left it a line feed. In
# the default and K&R modes '\r' is a CR too and '\l' a line feed; -a makes '\r' a line feed and
# '\l' the letter l (-mos9newline=swapped), as Ultra C's -a does. A fake MWOS tree.
# fixed: e8fb219 2026-10-23 m68k-os9-ucc: '\n' a CR in every mode (-mos9newline)
mkdir -p mw/SRC/DEFS mw/OS9/SRC/DEFS mw/OS9/68000/LIB
cat > nl.c <<'EOF'
char s[] = "ab\ncd\re\lf";
EOF
for m in "" -bc -a; do
	rm -f nl.r
	MWOS=$PWD/mw m68k-os9-ucc $m -e=as nl.c > nl.log 2>&1 ||
		why="$why ucc $m: failed: $(head -1 nl.log);"
	case "$m" in
	-a) want=61620d63640a656c6600 ;;
	*) want=61620d63640d650a6600 ;;
	esac
	case $(section_hex nl.r .data) in
	*$want*) ;;
	*) why="$why ucc $m: the escapes aren't $want: $(section_hex nl.r .data);" ;;
	esac
done
rm -f nl.r
m68k-os9-xcc -q -r nl.c > nl.log 2>&1 || why="$why xcc: failed: $(head -1 nl.log);"
case $(section_hex nl.r .data) in
*61620d63640d650a6600*) ;;
*) why="$why xcc: the escapes aren't ab CR cd CR e LF f;" ;;
esac

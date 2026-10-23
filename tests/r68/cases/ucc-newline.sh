# '\n' in m68k-os9-ucc's code: a CR (13) in Ultra C's default, K&R (-bc) and ANSI (-a) modes, as
# Ultra C's compiler makes it, and in m68k-os9-xcc (C 3.2); before, ucc left it a line feed. (Ultra
# C's -a also makes '\r' a line feed; GCC's -mos9newline leaves it a CR.) A fake MWOS tree.
# fixed: e8fb219 2026-10-23 m68k-os9-ucc: '\n' a CR in every mode (-mos9newline)
mkdir -p mw/SRC/DEFS mw/OS9/SRC/DEFS mw/OS9/68000/LIB
cat > nl.c <<'EOF'
char s[] = "ab\ncd";
EOF
for m in "" -bc -a; do
	rm -f nl.r
	MWOS=$PWD/mw m68k-os9-ucc $m -e=as nl.c > nl.log 2>&1 ||
		why="$why ucc $m: failed: $(head -1 nl.log);"
	case $(section_hex nl.r .data) in
	*61620d636400*) ;;
	*) why="$why ucc $m: \"ab\\ncd\" isn't ab CR cd: $(section_hex nl.r .data);" ;;
	esac
done
rm -f nl.r
m68k-os9-xcc -q -r nl.c > nl.log 2>&1 || why="$why xcc: failed: $(head -1 nl.log);"
case $(section_hex nl.r .data) in
*61620d636400*) ;;
*) why="$why xcc: \"ab\\ncd\" isn't ab CR cd;" ;;
esac

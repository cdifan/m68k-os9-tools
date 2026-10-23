# m68k-os9-xcc's -o levels as C 3.2's xcc runs them (xcc -bp): none (the default) and -o=1 its
# compact level, c68 and o68's peephole pass: GCC's -Os; -o=2, more in registers but no smaller:
# -O2; -o alone and -o=0 no optimizer: -O0. A later -o= wins; another level is an error, with a
# failing exit status.
cat > lv.c <<'EOF'
int sum(a, n) int *a; int n; { int s = 0; while (n--) s += *a++; return s; }
EOF
opts() {	# NAME WANT XCC-OPTIONS: GCC's command line (-bp) has WANT and no other -O
	local n=$1 want=$2; shift 2
	m68k-os9-xcc -q -bp "$@" -r lv.c > "$n.txt" 2>&1 || why="$why xcc $*: failed: $(head -1 "$n.txt");"
	grep -q -- " $want " "$n.txt" || why="$why xcc $*: no $want;"
	[ "$(grep -o -E ' -O[0-9s] ' "$n.txt" | sort -u | wc -l)" = 1 ] || why="$why xcc $*: more than one -O;"
}
opts def -Os
opts one -Os -o=1
opts two -O2 -o=2
opts zero -O0 -o=0
opts bare -O0 -o
opts upper -O2 -O=2
opts last -Os -o=2 -o=1
[ -s lv.r ] || why="$why no lv.r;"
if m68k-os9-xcc -q -o=s -r lv.c > bad.txt 2>&1; then why="$why -o=s: exit status 0;"; fi
grep -q "0, 1 or 2" bad.txt || why="$why -o=s: no message: $(head -1 bad.txt);"

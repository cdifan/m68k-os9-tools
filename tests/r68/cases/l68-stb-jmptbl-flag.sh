# m68k-os9-l68 -g -a: the symbol module flags $1000 each name that a far reference uses through the
# jump table, as Microware's l68s do (measured 2026-10-19: C 3.2's DOS L68, the native l68 eds. 50,
# 62, 64 and Ultra C's l68): a name called far gets it, an alias at the same address that is not
# used, or only from near, does not; C 3.2 style also has $2000 (referenced from another object).
# Before, no name had $1000.
# fixed: 0ba73de 2026-10-19 elf2mod -g: $1000 on the names used through the jump table
src fr <<'EOF'
 psect fr,$101,$8000,1,0,e
e bsr ta
 bsr sb
 rts
 ends
EOF
{ echo ' psect ff,0,0,0,0,0'; for i in $(seq 10000); do echo ' dc.l $4e714e71'; done; echo ' ends'; } > ff.a
src ft <<'EOF'
 psect ft,0,0,0,0,0
ta:
tb: rts
sa: bsr tb
 rts
sb: rts
 ends
EOF
for s in fr ff ft; do asm $s; done
# the entries as "name flags" (value, flags, name offset; the table at the STB header's offset)
stbflags() {
	od -An -tu1 -v "$1" | tr -s ' \n' '\n' | grep -v '^$' | awk '
		{ b[n++] = $1 + 0 }
		function w(o) { return b[o] * 256 + b[o + 1] }
		function l(o) { return w(o) * 65536 + w(o + 2) }
		END {
			x = l(0x1C); off = l(x + 6); cnt = l(x + 10)
			for (i = 0; i < cnt; i++) {
				e = off + 10 * i; no = l(e + 6); s = ""
				for (p = no; b[p] != 0; p++) s = s sprintf("%c", b[p])
				printf "%s %04x\n", s, w(e + 4)
			}
		}'
}
flag() { stbflags "$1" | awk -v n="$2" '$1 == n { print $2 }'; }
mkdir STB
for st in c32 ucc; do
	opt=; [ $st = ucc ] && opt=-t=os9_68k
	link $st fr.o ff.o ft.o -g -a $opt
	linked $st
	f=$st.stb; [ -f "$f" ] || f=STB/$st.stb
	# called far: ta and sb; tb (ta's alias) only from near; sa not at all
	for n in ta sb; do v=$(flag "$f" $n); [ $(( 0x${v:-0} & 0x1000 )) != 0 ] || why="$why $st $n flags ${v:-none}, no \$1000;"; done
	for n in tb sa; do v=$(flag "$f" $n); [ -n "$v" ] && [ $(( 0x$v & 0x1000 )) = 0 ] || why="$why $st $n flags ${v:-none};"; done
	# C 3.2's l68: $3004 (also referenced from another object); Ultra C's: $1004
	want=3004; [ $st = ucc ] && want=1004
	[ "$(flag "$f" ta)" = $want ] || why="$why $st ta flags $(flag "$f" ta), not $want;"
done

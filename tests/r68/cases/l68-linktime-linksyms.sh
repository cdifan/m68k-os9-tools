# m68k-os9-l68: a link-time expression (from a ROF) with one of l68's own symbols in it, such as
# etext plus an equate of another psect: the field holds the symbol's module value (raw: its
# address) plus the rest, as Ultra C's l68 links these sources (measured 2026-10-18), in a module
# link and in a raw one. Before, the field held ld's placeholder for the symbol (the start of
# .text) plus the rest.
# fixed: 2d21d18 2026-10-18 elf2mod: link-time expressions with linker symbols; l68 -dbgversion=
src ea <<'EOF'
 psect ea,$101,$8000,1,0,0
 dc.l etext+Q
 dc.l btext+Q
 rts
 ends
EOF
src eb <<'EOF'
 psect eb,0,0,0,0,0
P: equ $800
Q: equ $10
 ends
EOF
for s in ea eb; do
	asm $s
	m68k-os9-r68 $s.a -q -obj=rof -o=$s.r > $s.rlog 2>&1 || why="$why $s: -obj=rof failed: $(head -1 $s.rlog);"
done
link abc ea.r eb.r
linked abc
has_bytes abc "0000007c 00000010 4e75"
link raw ea.r eb.r -r=400000
linked raw
[ "$(filehex raw)" = "$(echo "0040001c 00400010 4e75 4e71 00000000 00000000 00000000 00000000" | hex)" ] || why="$why raw: $(filehex raw);"

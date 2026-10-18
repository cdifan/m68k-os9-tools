# m68k-os9-l68 -r (raw ROM link): l68 -r's linker symbols as Ultra C's l68 defines them (measured
# 2026-10-18, these sources): dsize/_dsize the data size rounded up to $100, btext the base,
# etext/_bidata the code's end and edata/_birefs the reference tables' start (after the
# initialized data block): absolute references their addresses, PC-relative ones and pointers in
# the initialized data their offsets in the image; four zero bytes more at the image's end when
# exactly one of the two reference tables has entries. Before, dsize, edata, _bidata and _birefs
# were undefined, btext and etext had their module values (0, the size), and the four bytes were
# missing. From ELF objects and from ROFs (dsize also in a link-time expression). The bytes are
# Ultra C's r68 and l68's.
# fixed: 4aa57ee 2026-10-18 l68 -r: dsize, _bidata, edata and raw addresses for the module symbols
src re <<'EOF'
 psect re,$101,$8000,1,0,0
f rts
 dc.l dsize
 dc.l btext
 lea edata(pc),a0
 lea _bidata(pc),a0
 vsect
v ds.b $1f8
p dc.l etext
q dc.l edata
 ends
 ends
EOF
src rd <<'EOF'
 psect rd,$101,$8000,1,0,0
f rts
 vsect
w dc.l f
 ends
 ends
EOF
src rx <<'EOF'
 psect rx,$101,$8000,1,0,0
 dc.l dsize+Stk+De
 move.l #dsize+Stk,d0
 vsect
v ds.b $40
 ends
 ends
EOF
src ry <<'EOF'
 psect ry,0,0,0,0,0
Stk: equ $800
De: equ $10
 ends
EOF
re="4e75 00000200 00400000 41fa0018 41fa0004 4e71 000001f8 00000008 00000014 00000024
    00000002 01f8 01fc 00000000 00000000 00000000"
rd="4e75 4e71 00000000 00000004 00000000 00000001 0000 00000000 00000000 00000000"
for s in re rd rx ry; do
	asm $s
	m68k-os9-r68 $s.a -q -obj=rof -o=$s.r > $s.rlog 2>&1 || why="$why $s: -obj=rof failed: $(head -1 $s.rlog);"
done
for k in o r; do
	link re_$k re.$k -r=400000
	linked re_$k
	[ "$(filehex re_$k)" = "$(echo "$re" | hex)" ] || why="$why re_$k: $(filehex re_$k);"
	link rd_$k rd.$k -r=400000
	linked rd_$k
	[ "$(filehex rd_$k)" = "$(echo "$rd" | hex)" ] || why="$why rd_$k: $(filehex rd_$k);"
done
link rx_r rx.r ry.r -r=400000
linked rx_r
has_bytes rx_r "00000910 203c00000900"
# not in a module link (both Microware linkers: unresolved)
link m re.o
failed_with m "dsize"

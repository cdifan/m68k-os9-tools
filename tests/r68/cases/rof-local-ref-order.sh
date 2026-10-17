# A ROF's local references as Microware's assemblers write them: those in the code first, then
# those in the remote initialized data, then those in the initialized data, each group by
# descending offset (both measured 2026-10-17 with this source; C 3.2's R68 writes this ROF as
# -dialect=c32 does now, but for the date). Before, one list by descending offset, the groups
# mixed where their offsets overlap. (The general rule, in the order the references were made,
# where the remote vsect comes first too: rof-debug-area.)
# fixed: f158cf2 2026-10-17 elf2rof: local references grouped by location
src lo <<'EOF'
 psect lo_p,0,0,1,0,0
 vsect
v1 ds.l 1
d1 dc.l c1
d2 dc.l c2
d3 dc.l c3
d4 dc.l c4
 ends
 vsect remote
r1 dc.l c2
r2 dc.l c4
 ends
c1 move.l #v1,d0
c2 move.l #d1,d0
c3 lea c1(pc),a0
c4 move.l #d2,d1
 move.l #r1,d2
 rts
 ends
EOF
# the count, then type and offset of each: 3b:18 39:12 39:8 38:2 21c:4 21c:0 1c:c 1c:8 1c:4 1c:0
refs=000a003b00000018003900000012003900000008003800000002021c00000004021c00000000
refs=${refs}001c0000000c001c00000008001c00000004001c00000000
for d in ucc c32; do
	m68k-os9-r68 lo.a -q -dialect=$d -obj=rof -o=lo-$d.r > lo-$d.log 2>&1 ||
		why="$why -dialect=$d: assembling failed: $(head -1 lo-$d.log);"
	has_bytes lo-$d.r "$refs"
done

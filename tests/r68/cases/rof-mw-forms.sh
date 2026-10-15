# m68k-os9-r68 -obj=rof (elf2rof) writes a ROF as C 3.2's R68 does (this source's ROF identical to
# its, but for the date; 2026-10-16): a psect of type/language 0 keeps its edition (3) in the
# header (before, 0); dc.w/dc.l Ext-* are the external and a negated reference to the psect's
# code ($74), not a PC-relative one; an external added to itself (\1+\1+\1+\1, 4*Ext) is four
# references to it (before, elf2rof refused it: "a multiplied external name"). The ELF object
# keeps no .r68pcm (r68togas's note of the Ext-* fields).
# fixed: 2cd9555 2026-10-16 r68togas, elf2rof, rof2elf: ROF output as Microware's (header edition, Ext-*, multiplied externals)
src rf <<'EOF'
Mul4 macro
 move.l \1+\1+\1+\1(a3),-(a7)
 endm
 psect r16,0,0,3,0,0
Tab dc.w Ext_a-*
 dc.w Ext_b-*,Ext_c-*
 dc.l Ext_d-*
 Mul4 Ext_e
 lea Ext_f(pc),a0
 rts
 ends
EOF
m68k-os9-r68 rf.a -q -obj=rof -o=rf.r > rf.log 2>&1 || why="$why -obj=rof failed: $(head -1 rf.log);"
got=$(perl -e 'local $/; $_ = <STDIN>; substr($_, 12, 6) = "\0" x 6; print unpack("H*", $_)' < rf.r)
want=deadface000000000000000900000000000000030000000000000000000000140000000000000000ffffffff0000000000000000000000007231360000000000fffefffefffffffa2f2b000041fa00004e7500064578745f610000010030000000004578745f620000010030000000024578745f630000010030000000044578745f640000010038000000064578745f6500000400300000000c00300000000c00300000000c00300000000c4578745f6600000100b0000000100004007c0000000600740000000400740000000200740000000000000000000000000000000000000000
[ "$got" = "$want" ] || why="$why the ROF differs: $got;"
asm rf
${B}objdump -h rf.o | grep -q r68pcm && why="$why rf.o keeps .r68pcm;"

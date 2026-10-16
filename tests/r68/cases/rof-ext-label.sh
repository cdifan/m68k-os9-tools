# m68k-os9-r68 -obj=rof: dc.w/dc.l Ext-Label (Label in the code, before or after) are the external
# and a negated reference to the psect's code ($74), as Ext-* (rof-mw-forms); before, a single
# PC-relative reference. This source's ROF is identical to C 3.2's R68's but for the date
# (2026-10-16; Ultra C's r68 writes the same references).
# fixed: 2026-10-16 r68togas: dc.w Ext-Label as Ext-*
src rl <<'EOF'
 psect r16b,0,0,0,0,0
 nop
Tab dc.w Ext_a-Tab
 dc.w Ext_b-Tab,Ext_c-*
 dc.l Ext_d-Fwd
 dc.w Ext_e-Tab+4
Fwd rts
 ends
EOF
m68k-os9-r68 rl.a -q -obj=rof -o=rl.r > rl.log 2>&1 || why="$why -obj=rof failed: $(head -1 rl.log);"
got=$(perl -e 'local $/; $_ = <STDIN>; substr($_, 12, 6) = "\0" x 6; print unpack("H*", $_)' < rl.r)
want=deadface000000000000000900000000000000000000000000000000000000100000000000000000ffffffff000000000000000000000000723136620000004e71fffefffefffcfffffff200024e7500054578745f610000010030000000024578745f620000010030000000044578745f630000010030000000064578745f640000010038000000084578745f6500000100300000000c000500740000000c007c0000000800740000000600740000000400740000000200000000000000000000000000000000
[ "$got" = "$want" ] || why="$why the ROF differs: $got;"

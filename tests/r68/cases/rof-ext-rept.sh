# m68k-os9-r68 -obj=rof: dc.w Ext-* and Ext-Label (rof-mw-forms, rof-ext-label) repeated by a rept
# in a macro, a macro used twice and a rept of two lines assemble (before, "symbol `.Lr68pcmN' is
# already defined": r68togas's mark after each was a label); and an external's references come
# in ascending order of offset, those fields' too (before, after the others). Both sources' ROFs
# identical to C 3.2's R68's but for the date (2026-10-16).
# fixed: 2026-10-16 r68togas, elf2rof: Ext-* in rept, references in order
src rr <<'EOF'
 psect r16r,0,0,0,0,0
Two macro
 rept \1
 dc.w Ext_a-Tab,Ext_b-*
 endr
 endm
One macro
 dc.w Ext_c-Tab
 endm
 nop
Tab Two 3
 One
 One
 rept 2
 dc.l Ext_d-Tab
 nop
 endr
 rts
 ends
EOF
src rs <<'EOF'
 psect r16s,0,0,0,0,0
 jsr Ext_a
Tab dc.w Ext_a-*
 lea Ext_a(pc),a0
 dc.l Ext_a
 dc.w Ext_a-Tab
 move.l Ext_a(a6),d0
 dc.w Ext_a-*
 jsr Ext_a
 rts
 ends
EOF
want_rr=deadface000000000000000900000000000000000000000000000000000000200000000000000000ffffffff000000000000000000000000723136720000004e71fffefffefffefffafffefff6fffefffefffffffe4e71fffffffe4e714e7500044578745f6100000300300000000200300000000600300000000a4578745f6200000300300000000400300000000800300000000c4578745f6300000200300000000e0030000000104578745f64000002003800000012003800000018000a007c00000018007c0000001200740000001000740000000e00740000000c00740000000a00740000000800740000000600740000000400740000000200000000000000000000000000000000
want_rs=deadface000000000000000900000000000000000000000000000000000000200000000000000000ffffffff000000000000000000000000723136730000004eb900000000fffa41fa000000000000fffa202e0000ffea4eb9000000004e7500014578745f6100000800380000000200300000000600b00000000a00380000000c00300000001000300000001400300000001600380000001a000300740000001600740000001000740000000600000000000000000000000000000000
for s in rr rs; do
	if m68k-os9-r68 $s.a -q -obj=rof -o=$s.r > $s.log 2>&1; then
		got=$(perl -e 'local $/; $_ = <STDIN>; substr($_, 12, 6) = "\0" x 6; print unpack("H*", $_)' < $s.r)
		eval want=\$want_$s
		[ "$got" = "$want" ] || why="$why $s: the ROF differs: $got;"
	else
		why="$why $s: -obj=rof failed: $(grep -m1 rror $s.log);"
	fi
done

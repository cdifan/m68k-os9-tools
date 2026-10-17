# m68k-os9-l68 -g: where psects have debug information (debug.b/.w/.l), OUTFILE.dbg next to the
# symbol module (in STB if there's one), as l68 -g writes it (measured 2026-10-17): C 3.2's
# $BEEFCAFE, a zero word, the module's CRC as a long, the psects' debug areas in link order; a
# reference to code holds the label's module offset, to data its a6-relative offset. Ultra C's
# (-t=): $CAFEBEEF, 3, the CRC, a zero long, the areas. The areas as C 3.2's l68 writes them for
# these sources, from ELF objects and from ROFs (through rof2elf). No .dbg without -g or without
# debug information. Before, r68 refused the lines (and rof2elf dropped a ROF's debug area).
# fixed: 55093e0 2026-10-17 debug.b/.w/.l, the ROF debug area and l68 -g's .dbg
src dbg <<'EOF'
 psect dbg_p,$101,$8000,1,0,c1
 vsect remote
rv dc.l c1
 debug.l rv
 ends
 vsect
dv dc.l c2
 debug.l dv
 ends
 debug.w 0,2
 debug.l *
 debug.b "ab",0
 debug.w 7
c1 move.l dv(a6),d0
 debug.l c2+4
 debug.w c2
 debug.l xcode
 debug.l xdata
c2 bsr xcode
 move.l xdata(a6),d1
 rts
 debug.l *
 ends
EOF
src dbgx <<'EOF'
 psect dbgx_p,0,0,0,0,0
 vsect
xdata: dc.l 3
 ends
 debug.b "x",0
 debug.l xdata
 debug.l *
xcode: rts
 ends
EOF
src nodbg <<'EOF'
 psect nodbg_p,$101,$8000,1,0,c1
c1 rts
 ends
EOF
asm dbg -dialect=c32
asm dbgx -dialect=c32
asm nodbg -dialect=c32
for k in dbg dbgx; do
	m68k-os9-r68 $k.a -q -dialect=c32 -obj=rof -o=$k.r > $k.rlog 2>&1 || why="$why $k.r failed;"
done
areas="ffff8008 ffff8000 00000002 0000004c 61620000 00070000 00540050 0000005a ffff8004 0000005a
	7800 ffff8004 0000005a"
crc() { od -An -tx1 -v -j $(($(stat -c %s "$1") - 4)) "$1" | hex; }
for o in elf rof; do
	mkdir $o
	if [ $o = elf ]; then (cd $o && link p -g ../dbg.o ../dbgx.o -n=tp)
	else (cd $o && link p -g ../dbg.r ../dbgx.r -n=tp); fi
	linked $o/p
	[ "$(filehex $o/p.dbg)" = "beefcafe0000$(crc $o/p)$(echo "$areas" | hex)" ] ||
		why="$why $o: p.dbg is $(filehex $o/p.dbg);"
done
mkdir ucc ucc/STB
(cd ucc && link p -g -t=os9_68k ../dbg.o ../dbgx.o -n=tp)
linked ucc/p
[ -f ucc/p.dbg ] && why="$why ucc: p.dbg not in STB;"
[ "$(filehex ucc/STB/p.dbg)" = "cafebeef0003$(crc ucc/p)00000000$(echo "$areas" | hex)" ] ||
	why="$why ucc: STB/p.dbg is $(filehex ucc/STB/p.dbg);"
mkdir nog nod
(cd nog && link p ../dbg.o ../dbgx.o -n=tp)
linked nog/p
[ -f nog/p.dbg ] && why="$why a .dbg without -g;"
(cd nod && link p -g ../nodbg.o -n=tp)
linked nod/p
[ -f nod/p.stb ] || why="$why no .stb;"
[ -f nod/p.dbg ] && why="$why a .dbg without debug information;"

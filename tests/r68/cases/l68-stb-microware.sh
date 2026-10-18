# m68k-os9-l68 -g's symbol modules as Microware's l68 writes them (measured 2026-10-18 with C 3.2's
# l68, Ultra C's and 424 original .stb files): the symbol module's attr/rev $8001 whatever the
# module's; _jmptbl only for modules that may have a jump table (programs, trap handlers, -i
# system modules: not a driver); l68's own symbols at the same value in Microware's order (C 3.2:
# end before btext; Ultra C: in its order of making them, _btext _bdata btext bdata); a
# data symbol in $8000..$FFFF sign-extended ($FFFF8004); a data label that a link-time expression
# with an external names (Dat+Ext(a6)) flagged $2000 as referenced (C 3.2 style). Before: the
# module's revision, _jmptbl everywhere, other orders, $00008004, and no flag there.
# fixed: 42b82e4 2026-10-18 elf2mod -g: symbol modules as Microware's l68 writes them
# an entry: value (4 bytes), type (2), name offset (4)
src prg <<'EOF'
 psect prg,$101,$8003,1,0,e
e lea Dat(a6),a0
 move.l d0,Dat5+Ext(a6)
 rts
 ends
EOF
src dat <<'EOF'
 psect dat,0,0,0,0,0
Ext: equ 4
 vsect
Dat: ds.b 26
Dat5: ds.b 26
 ends
 ends
EOF
src drv <<'EOF'
 psect drv,$E01,$8001,1,0,e
e rts
 ends
EOF
src sys <<'EOF'
 psect sys,$C01,$8001,1,0,e
e rts
 vsect
Big: ds.b $8004
 ends
 ends
EOF
for s in prg dat drv sys; do asm $s; done
link prg prg.o dat.o -g
linked prg
link drv drv.o -g
linked drv
link drvu drv.o -g -t=os9_68k
linked drvu
link sys sys.o -g
linked sys
# (a) attr/rev $8001 (the program's is $8003)
[ "$(od -An -tx1 -j20 -N2 prg.stb | hex)" = 8001 ] || why="$why prg.stb attr/rev $(od -An -tx1 -j20 -N2 prg.stb | hex);"
# (b) _jmptbl in the program's, not in the driver's
has_bytes prg.stb "$(printf '_jmptbl' | od -An -tx1 | hex)"
lacks_bytes drv.stb "$(printf '_jmptbl' | od -An -tx1 | hex)"
lacks_bytes drvu.stb "$(printf '_jmptbl' | od -An -tx1 | hex)"
# (c) at 0 in the driver: end before btext; Ultra C: _btext _bdata _enddata btext bdata end
has_bytes drv.stb "00000000 2001 ........ 00000000 2004"
has_bytes drvu.stb "00000000 2004 ........ 00000000 2001 ........ 00000000 2001 ........ 00000000 2004 ........ 00000000 2001 ........ 00000000 2001"
# (e) the system module's data end, $8004 bytes from 0: $FFFF8004
has_bytes sys.stb "ffff8004 2001"
# (f) Dat5 (only in Dat5+Ext(a6)) flagged, as Dat: two data entries with $2000
has_bytes prg.stb "ffff8000 2000 ........ ffff801a 2000"

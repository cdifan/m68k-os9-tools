# m68k-os9-l68 -g -dbgversion=N: the .dbg's word after the magic. By default the style's (C 3.2's
# l68 0, Ultra C 2.5's 3, whatever -dialect); -dbgversion= sets it in either style (older Ultra C
# files have 1). Out of range: an error.
# fixed: 2d21d18 2026-10-18 elf2mod: link-time expressions with linker symbols; l68 -dbgversion=
src dv <<'EOF'
 psect dv_p,$101,$8000,1,0,c1
 debug.w 7
c1 rts
 ends
EOF
asm dv -dialect=c32
head6() { od -An -tx1 -v -N6 "$1" | hex; }
mkdir c32 c32v ucc ucc/STB uccv uccv/STB
(cd c32 && link p -g ../dv.o)
(cd c32v && link p -g -dbgversion=1 ../dv.o)
(cd ucc && link p -g -t=os9_68k ../dv.o)
(cd uccv && link p -g -t=os9_68k -dbgversion=0x102 ../dv.o)
[ "$(head6 c32/p.dbg)" = beefcafe0000 ] || why="$why c32: $(head6 c32/p.dbg);"
[ "$(head6 c32v/p.dbg)" = beefcafe0001 ] || why="$why c32v: $(head6 c32v/p.dbg);"
[ "$(head6 ucc/STB/p.dbg)" = cafebeef0003 ] || why="$why ucc: $(head6 ucc/STB/p.dbg);"
[ "$(head6 uccv/STB/p.dbg)" = cafebeef0102 ] || why="$why uccv: $(head6 uccv/STB/p.dbg);"
mkdir bad
(cd bad && link p -g -dbgversion=65536 ../dv.o)
failed_with bad/p "dbgversion"

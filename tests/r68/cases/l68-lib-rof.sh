# m68k-os9-l68: one ROF given as a library (-l=part.r), a library of one member, as Microware's
# l68 takes it (C 3.2's L68, 2026-10-16); before, its conversion ran ranlib on rof2elf's object
# ("file format not recognized", "cannot convert").
# fixed: FIXME
src lm <<'EOF'
Prgrm equ 1
 psect lm,(Prgrm<<8)+1,$8000,1,0,e
e bsr f
 rts
 ends
EOF
src lf <<'EOF'
 psect lf,0,0,0,0,0
f: rts
 ends
EOF
asm lm
m68k-os9-r68 lf.a -q -obj=rof -o=lf.r > lf.log 2>&1 || why="$why -obj=rof failed: $(head -1 lf.log);"
link mod lm.o -l=lf.r -n=mod
linked mod
has_bytes mod "6100 0006 4e75 4e71 4e75"

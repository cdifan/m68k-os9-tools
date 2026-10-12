# A label plus an external equate (lab-ext-data) through ROF and the links: elf2rof writes the
# link-time expression as r68 does, a local and an external reference at the field, rof2elf
# reads that back into the same expression (r68's ROF too), and a link with the equate from a
# library gives the module the equate defined in the source gives.
# fixed: 2aefb81 2026-10-12 No folding of a library's equates: a label plus external equates as a link-time expression; -sysl= and rof2elf's -e gone
src lx <<'EOF'
 psect lx,$101,$8000,1,0,Start
 vsect
Pad ds.w 3
Block ds.l 4
 ends
Start move.l a0,Block+Field(a6)
 move.w Block+Field+2(a6),d0
 rts
 ends
EOF
src eqs <<'EOF'
 psect eqs,0,0,0,0,0
Field: equ $10
 ends
EOF
# the same, the equate in the source
(echo 'Field equ $10'; cat lx.a) > lxl.a
asm lx
asm lx -obj=rof
mv lx.o lx.r
asm lx
rof2elf -o lxr.o lx.r > lxr.log 2>&1 || why="$why rof2elf of our ROF failed: $(head -1 lxr.log);"
has_reloc lxr.o "r68x_e0894206+0x00000006"
has_reloc lxr.o "r68x_e0894206+0x00000008"
has_reloc lx.o "r68x_e0894206+0x00000006"
asm eqs -obj=rof
mv eqs.o eqs.l
mkdir -p a b
cp lx.o a/m.o
asm lxl
cp lxl.o b/m.o
link a/mod a/m.o -l=eqs.l
link b/mod b/m.o
linked a/mod
linked b/mod
cmp -s a/mod b/mod || why="$why the module with the library's equate differs;"

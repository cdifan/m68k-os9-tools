# r68's ROF of a label plus an external equate (lab-ext-data): rof2elf reads its local and
# external reference at the field into the link-time expression r68togas writes (the same name:
# the label's section's alias and the equate).
# fixed: pending
needs_r68
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
asm lx
has_reloc lx.o "r68x_e0894206+0x00000006"
mwasm lx
rof2elf -o lxm.o lx.r > lxm.log 2>&1 || why="$why rof2elf of r68's ROF failed: $(head -1 lxm.log);"
has_reloc lxm.o "r68x_e0894206+0x00000006"
has_reloc lxm.o "r68x_e0894206+0x00000008"

# m68k-os9-l68: absolute references from the code to bname, btext and etext (dc.w bname in a
# descriptor, through an equate too, dc.l, bname+2): their offsets in the module, as both
# Microware linkers write them (measured 2026-10-15: this source's module identical to C 3.2's
# L68's); before, elf2mod took only PC-relative ones ("only PC-relative references to bname").
# fixed: a0f6d83 2026-10-15 r68togas, l68, elf2mod: fixes from rebuilds of modules
src dd <<'EOF'
 psect dd,$0F00,$8000,1,0,0
Self_n equ bname
 dc.w bname
 dc.l bname
 dc.w Self_n
 dc.w btext
 dc.l etext
 dc.w bname+2
 ends
EOF
asm dd
link m6 dd.o -n=M6
linked m6
# the code at $30, the name after it at $40; the module $48 bytes
has_bytes m6 "0040 00000040 0040 0000 00000048 0042 4d360000"

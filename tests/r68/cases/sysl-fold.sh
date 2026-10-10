# sys.l's equates are not folded into a sum of one external and the source's own constants
# (V_Flag+OFFS): a relocation takes it (a source may define its own V_Flag).
# fixed: 8266699 2026-10-09 r68togas: no sys.l folding for a sum with the source's own constants; mnemonic macro arguments
# (sys.l of two ROFs: a library of one comes out of rof2elf as an object, which the conversion
# for -sysl= doesn't take; found 2026-10-10)
src sysl <<'EOF'
 psect sysl,0,0,0,0,0
V_Flag: equ $23
 ends
EOF
src sysl2 <<'EOF'
 psect sysl2,0,0,0,0,0
OTHER: equ $10
 ends
EOF
src fold <<'EOF'
 psect fold,0,0,0,0,0
OFFS equ 4
 move.b V_Flag+OFFS(a2),d0
 ends
EOF
asm sysl -obj=rof
asm sysl2 -obj=rof
cat sysl.o sysl2.o > sys.l
asm fold -sysl=sys.l
has_reloc fold.o "V_Flag+0x00000004"

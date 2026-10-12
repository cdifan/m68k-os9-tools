# A library of one ROF (-sysl= of a sys.l made of one psect) is converted into an archive, as one
# of more: rof2elf wrote an object for it to the front ends' temporary name, which ranlib
# refused, so the conversion failed.
# fixed: pending
src onesys <<'EOF'
 psect onesys,0,0,0,0,0
V_Flag: equ $23
 ends
EOF
src useone <<'EOF'
 psect useone,0,0,0,0,0
 move.b V_Flag(a2),d0
 ends
EOF
asm onesys -obj=rof
cp onesys.o one.l
asm useone -sysl=one.l
has_reloc useone.o "V_Flag"

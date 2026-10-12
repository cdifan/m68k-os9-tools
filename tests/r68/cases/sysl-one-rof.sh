# A library of one ROF (a library of equates made of one psect, given to the link with -l=) is
# converted into an archive, as one of more: rof2elf wrote an object for it to the front ends'
# temporary name, which ranlib refused, so the conversion failed.
# fixed: 52fe63e 2026-10-12 rof2elf: a library of one ROF is an archive unless the output is named .o
src onesys <<'EOF'
 psect onesys,0,0,0,0,0
V_Flag: equ $23
 ends
EOF
src useone <<'EOF'
 psect useone,$101,$8000,1,0,Start
Start move.b V_Flag(a2),d0
 rts
 ends
EOF
asm onesys -obj=rof
cp onesys.o one.l
asm useone
link useone useone.o -l=one.l
linked useone
has_bytes useone "102a0023"

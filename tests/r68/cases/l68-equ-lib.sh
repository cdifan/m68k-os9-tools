# m68k-os9-l68: a library member of equates only gives way to an object's definition, as in both
# Microware linkers (measured 2026-10-09); a member with code clashes, as in both.
# fixed: 19e772c 2026-10-10 m68k-os9-l68, elf2mod: equate-only library members give way; code label offsets
src root <<'EOF'
 psect root,$101,$8001,1,0,entry
V_Mine: equ $3A
entry move.w #V_Mine,d0
 move.w #PD_Xyz,d1
 rts
 ends
EOF
src io <<'EOF'
 psect io,0,0,0,0,0
PD_Xyz: equ $05
V_Mine: equ $23
 ends
EOF
src iocode <<'EOF'
 psect iocode,0,0,0,0,0
PD_Xyz: equ $05
V_Mine: equ $23
fun: rts
 ends
EOF
asm root; asm io; asm iocode
lib io.a io.o
lib iocode.a iocode.o
link mod root.o -l=io.a -n=mod
linked mod
has_bytes mod "303c003a 323c0005 4e75"
link mod2 root.o -l=iocode.a -n=mod2
failed_with mod2 "V_Mine"

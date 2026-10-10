# m68k-os9-l68: a PC-relative reference to an absolute symbol (an equate, a macro name) is an
# error, reported as both Microware linkers do (not as a call needing a jump table); absolute
# references to a macro name get 0.
# fixed: 4283498 2026-10-10 r68togas, rof2elf, m68k-os9-r68, os9link: corpus fixes; l68's PC-relative error
src defs <<'EOF'
 psect defs,0,0,0,0,0
Abs: equ $10
DoIt: macro
 nop
 endm
 ends
EOF
src root <<'EOF'
 psect root,$101,$8001,1,0,entry
entry bsr Abs
 rts
 ends
EOF
src root2 <<'EOF'
 psect root2,$101,$8001,1,0,entry
entry lea DoIt(pc),a0
 rts
 ends
EOF
src root3 <<'EOF'
 psect root3,$101,$8001,1,0,entry
entry dc.w DoIt
 dc.l DoIt
 move.w #DoIt,d0
 rts
 ends
EOF
asm defs; asm root; asm root2; asm root3
link mod root.o defs.o -n=mod
failed_with mod "relative reference to symbol 'Abs' in the absolute (equ) area"
link mod2 root2.o defs.o -n=mod2
failed_with mod2 "relative reference to symbol 'DoIt' in the absolute (equ) area"
link mod3 root3.o defs.o -n=mod3
linked mod3
has_bytes mod3 "0000 00000000 303c0000 4e75"

# An org/do offset minus an external (F_b-Ext(a2), #F_b-Ext): a link-time expression of the
# negated external with the offset as its constant, as -Ext alone (neg-ext); before, the offset
# wasn't a constant there and gas failed ("can't resolve `L0' {*ABS* section} - `Ext'"). Linked,
# the same module as from Ultra C's r68's ROF (a negative reference and the offset in the bytes).
# Also the byte immediates of an external (byte-imm-ext) linked: $FFFE for -2, as from r68's.
# fixed: a0f6d83 2026-10-15 r68togas, l68, elf2mod: fixes from rebuilds of modules
src cx <<'EOF'
 org 0
F_a do.b 4
F_b do.w 1
 psect cx,$101,$8000,1,0,0
 lea F_b-Ext(a2),a3
 move.w #F_b-Ext,d0
 move.w #F_b+2-Ext,d1
 andi #Ext_n,ccr
 btst #Ext_n,4(a0)
 rts
 ends
EOF
src cxd <<'EOF'
 psect cxd,0,0,0,0,0
Ext: equ $30
Ext_n: equ -2
 ends
EOF
asm cx; asm cxd
link mod cx.o cxd.o -n=mod
linked mod
has_bytes mod "47eaffd4 303cffd4 323cffd6 023cfffe 0828fffe0004 4e75"
needs_r68
mwasm cx; mwasm cxd
link mw cx.r cxd.r -n=mod
linked mw
cmp -s mod mw || why="$why the module differs from the one linked from r68's ROFs;"

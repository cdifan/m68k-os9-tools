# m68k-os9-l68: library members in Ultra C's l68's order (the default, -dialect=ucc): the root's
# references in name order; a member linked puts the members its unresolved references need (in
# name order, a link-time expression's terms too) on a stack, those not linked or on it already,
# the top linked next. Ultra C's l68 gives m_a m_d m_e m_c m_b here (measured 2026-10-16);
# before, ld's search of the archive, which -dialect=c32 keeps (C 3.2's L68 searches once and
# refuses this link: m_c and m_d come before m_a, which needs them).
# fixed: 97e8452 2026-10-16 m68k-os9-l68: library members in Ultra C's l68's order (-dialect=)
src root <<'EOF'
 psect root,$101,$8000,1,0,entry
entry bsr a_one
 bsr b_two
 rts
 ends
EOF
src ma <<'EOF'
 psect m_a,0,0,0,0,0
a_one: dc.w $FA0A
 bsr c_three
 bsr d_four
 rts
 ends
EOF
src mb <<'EOF'
 psect m_b,0,0,0,0,0
b_two: dc.w $FA0B
 rts
 ends
EOF
src mc <<'EOF'
 psect m_c,0,0,0,0,0
c_three: dc.w $FA0C
 rts
 ends
EOF
src md <<'EOF'
 psect m_d,0,0,0,0,0
d_four: dc.w $FA0D
 dc.l e_five+a_one
 rts
 ends
EOF
src me <<'EOF'
 psect m_e,0,0,0,0,0
e_five: dc.w $FA0E
 rts
 ends
EOF
for f in root ma mb mc md me; do asm $f; done
lib lib.a md.o mc.o mb.o ma.o me.o
order() { filehex "$1" | grep -o 'fa0[a-e]' | tr '\n' ' '; }
link mod root.o -l=lib.a -n=mod
linked mod
[ "$(order mod)" = "fa0a fa0d fa0e fa0c fa0b " ] || why="$why order $(order mod), not Ultra C's;"
link modc root.o -l=lib.a -dialect=c32 -n=modc
linked modc
[ "$(order modc)" = "fa0b fa0a fa0e fa0d fa0c " ] || why="$why -dialect=c32: order $(order modc), not ld's;"

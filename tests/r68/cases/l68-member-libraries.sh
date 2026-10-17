# m68k-os9-l68: library members from several libraries in Ultra C's l68's order (the default,
# -dialect=ucc): the libraries one by one in their order, in each the names still unresolved
# (the root's, then those of the members linked from earlier libraries), the stack rule within
# the library; a name both define comes from the library searched, a name only an earlier
# library defines stays unresolved. As Ultra C's l68 links these (measured 2026-10-17, libgen
# libraries and concatenated ROFs alike; its 1995 l68 links a program's libraries so too).
# Before, the stack rule over all libraries at once, the first definer in any library.
# fixed: 2026-10-17 m68k-os9-l68: library members library by library (-dialect=ucc)
src root <<'EOF'
 psect root,$101,$8000,1,0,entry
entry bsr a_one
 bsr z_two
 rts
 ends
EOF
src mz <<'EOF'
 psect m_z,0,0,0,0,0
z_two: dc.w $FA01
 rts
 ends
EOF
src mq <<'EOF'
 psect m_q,0,0,0,0,0
q_one: dc.w $FA03
 rts
 ends
EOF
src mr1 <<'EOF'
 psect m_r1,0,0,0,0,0
r_both: dc.w $FA04
 rts
 ends
EOF
src ma <<'EOF'
 psect m_a,0,0,0,0,0
a_one: dc.w $FA02
 bsr q_one
 bsr r_both
 rts
 ends
EOF
src ma2 <<'EOF'
 psect m_a,0,0,0,0,0
a_one: dc.w $FA02
 bsr r_both
 rts
 ends
EOF
src mr2 <<'EOF'
 psect m_r2,0,0,0,0,0
r_both: dc.w $FA05
 rts
 ends
EOF
for f in root mz mq mr1 ma ma2 mr2; do asm $f; done
lib l1.a mz.o mq.o mr1.o
lib l2.a ma.o mr2.o
lib l2b.a ma2.o mr2.o
order() { filehex "$1" | grep -o 'fa0[0-9]' | tr '\n' ' '; }
# z from the first library, then a and r_both from the second
link mod root.o -l=l1.a -l=l2b.a -n=mod
linked mod
[ "$(order mod)" = "fa01 fa02 fa05 " ] || why="$why order $(order mod), not Ultra C's;"
# the second library's member needs q_one, which only the first defines: unresolved
link mod2 root.o -l=l1.a -l=l2.a -n=mod2
failed_with mod2 q_one
# the other way round: a and r_both, then z and q_one
link mod3 root.o -l=l2.a -l=l1.a -n=mod3
linked mod3
[ "$(order mod3)" = "fa02 fa05 fa01 fa03 " ] || why="$why reversed: order $(order mod3), not Ultra C's;"

# m68k-os9-l68: which member is linked where two define a name. Ultra C's l68 (the default,
# -dialect=ucc) takes the first in the library; C 3.2's L68 (-dialect=c32), searching once, the
# first after the member referring to it (measured 2026-10-16 in vDos, both linkers).
src root <<'EOF'
 psect root,$101,$8000,1,0,entry
entry bsr x_one
 rts
 ends
EOF
src mx <<'EOF'
 psect m_x,0,0,0,0,0
x_one: dc.w $FA0C
 bsr t_two
 rts
 ends
EOF
src ms <<'EOF'
 psect m_s,0,0,0,0,0
t_two: dc.w $FA01
 rts
 ends
EOF
src mt <<'EOF'
 psect m_t,0,0,0,0,0
t_two: dc.w $FA02
 rts
 ends
EOF
src mu <<'EOF'
 psect m_u,0,0,0,0,0
t_two: move.l u_three(a6),d0
 rts
 ends
EOF
for f in root mx ms mt mu; do asm $f; done
order() { filehex "$1" | grep -o 'fa0[0-9a-f]' | tr '\n' ' '; }
lib lib.a ms.o mx.o mt.o
link mod root.o -l=lib.a -n=mod
linked mod
[ "$(order mod)" = "fa0c fa01 " ] || why="$why members $(order mod), not Ultra C's;"
link modc root.o -l=lib.a -dialect=c32 -n=modc
linked modc
[ "$(order modc)" = "fa0c fa02 " ] || why="$why -dialect=c32: members $(order modc), not C 3.2's;"
# the first definer needs what the link lacks: Ultra C's l68 fails, C 3.2's L68 links the other
lib lib2.a mu.o mx.o mt.o
link mod2 root.o -l=lib2.a -n=mod2
failed_with mod2 u_three
link mod2c root.o -l=lib2.a -dialect=c32 -n=mod2c
linked mod2c
[ "$(order mod2c)" = "fa0c fa02 " ] || why="$why -dialect=c32: members $(order mod2c), not C 3.2's;"

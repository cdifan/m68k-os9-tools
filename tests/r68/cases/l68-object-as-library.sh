# m68k-os9-l68: an object given as -l= (a library of one member, as a ROF file is to l68) with
# Ultra C's member order (the default): linked when the link needs it, left out when not.
# Before, "cannot read the archive": only ar archives were read for the member order.
# fixed: c3ff053 2026-10-16 m68k-os9-l68: an object as a library of one member
src root <<'EOF'
 psect root,$101,$8000,1,0,entry
entry bsr f_one
 rts
 ends
EOF
src mf <<'EOF'
 psect m_f,0,0,0,0,0
f_one: dc.w $FA0F
 rts
 ends
EOF
src mg <<'EOF'
 psect m_g,0,0,0,0,0
g_two: dc.w $FA0E
 rts
 ends
EOF
for f in root mf mg; do asm $f; done
link mod root.o -l=mf.o -l=mg.o -n=mod
linked mod
[ "$(filehex mod | grep -o 'fa0[ef]' | tr '\n' ' ')" = "fa0f " ] || why="$why members $(filehex mod | grep -o 'fa0[ef]' | tr '\n' ' '), not fa0f alone;"

# The lines r68togas writes for a byte immediate of an external (a .reloc of the word), dc.w Ext-*
# (a note in .r68pcm), an immediate PC-relative to an external (and -dialect=c32's short branch
# to one) stay in MRI's syntax (reloc, section), with no .mri 0 and .mri 1 around them: gas
# prepares its input ahead, a buffer at a time, in the syntax mode then, and where a buffer ended
# between the two, the lines after it were taken in the wrong mode ("bad format for ifc" in a
# large source). Here: no more .mri lines than the same source without those lines has, the notes
# and the relocations made.
# fixed: 2026-10-16 r68togas: no .mri switches in the translated lines
src mb <<'EOF'
 psect mb,0,0,0,0,0
Err macro
 move.w #\1,d1
 ori #Ext_c,ccr
 dc.w Ext_b-*
 endm
 vsect
Var ds.w 1
 ends
 Err 3
 andi.b #Ext_c,d0
 dc.w Ext_b-*,Ext_a-*
 movea.l #Ext_d-*,a2
 rts
 ends
EOF
src m0 <<'EOF'
 psect mb,0,0,0,0,0
Err macro
 move.w #\1,d1
 endm
 vsect
Var ds.w 1
 ends
 Err 3
 rts
 ends
EOF
r68togas -o mb.s mb.a > mb.tlog 2>&1 || why="$why translating mb failed;"
r68togas -o m0.s m0.a > m0.tlog 2>&1 || why="$why translating m0 failed;"
a=$(grep -c -i '^ *\.mri' mb.s); b=$(grep -c -i '^ *\.mri' m0.s)
[ "$a" = "$b" ] || why="$why $a .mri lines, $b without the forms;"
asm mb
has_reloc mb.o "R_68K_16          Ext_c"
n=$(grep -c '^ dc\.l \.Lr68pcm-' mb.s)
[ "$n" = 3 ] || why="$why $n .r68pcm notes, not 3;"
has_reloc mb.o "R_68K_PC32        Ext_d"

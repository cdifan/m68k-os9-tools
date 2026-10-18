# m68k-os9-l68: an object whose initialized data is all in a remote vsect takes its place in the
# link's order for the initialized data's pointer tables, as in Ultra C's l68 (measured
# 2026-10-19: these sources' modules by Ultra C's r68 and l68 have these tables). Before, such an
# object counted as linked after all objects with plain initialized data, so its pointers came
# first.
# fixed: 67ec61d 2026-10-19 elf2mod, l68: Ultra C's jump table and M$IRefs order; -b= pads from the section's start
src ra <<'EOF'
 psect ra,$101,$8000,1,0,entry
 vsect
a1 dc.l entry
 ends
entry rts
 ends
EOF
src rb <<'EOF'
 psect rb,0,0,0,0,0
 vsect remote
b1 dc.l bf
 ends
bf rts
 ends
EOF
src rc <<'EOF'
 psect rc,0,0,0,0,0
 vsect
c1 dc.l cf
 ends
cf rts
 ends
EOF
asm ra; asm rb; asm rc
link m1 ra.o rb.o rc.o -n=m1
linked m1
# the pointers to the code: rc's (4), rb's (8, remote), ra's (0)
has_bytes m1 "00000003 000400080000 00000000 00000000"
link m2 ra.o rc.o rb.o -n=m2
linked m2
has_bytes m2 "00000003 000800040000 00000000 00000000"

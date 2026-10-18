# m68k-os9-l68: within an object, the initialized data's pointers to global symbols that other
# objects define come after its local ones, by the symbol's name, the last first, and each
# name's by address, the last first: the reverse of the ROF's references (the externals by name,
# then the locals), as Ultra C's l68 lists them (measured 2026-10-19: these sources' modules,
# linked in both orders by Ultra C's r68 and l68, have these tables; before, each object's
# pointers were in address order).
# fixed: 67ec61d 2026-10-19 elf2mod, l68: Ultra C's jump table and M$IRefs order; -b= pads from the section's start
src ra <<'EOF'
 psect ra,$101,$8000,1,0,entry
 vsect
r1 dc.l 0
 ends
entry rts
 ends
EOF
src rb <<'EOF'
 psect rb,0,0,0,0,0
 vsect
zeta: dc.l 1
alpha: dc.l 2
mid: dc.l 3
 ends
fn: rts
gn: rts
 ends
EOF
src rc <<'EOF'
 psect rc,0,0,0,0,0
 vsect
p1 dc.l alpha
p2 dc.l zeta
p3 dc.l loc
p4 dc.l alpha
p5 dc.l lfn
p6 dc.l gn
p7 dc.l mid
p8 dc.l fn
loc dc.l p1
 ends
lfn rts
 ends
EOF
asm ra; asm rb; asm rc
link m1 ra.o rb.o rc.o -n=m1
linked m1
# code: rc's lfn (20), then gn (24), fn (2c); data: rc's locals p3 (18), loc (30), then zeta (14),
# mid (28), alpha (1c, 10)
has_bytes m1 "00000003 00200024002c 00000000 00000006 0018003000140028001c0010 00000000"
link m2 ra.o rc.o rb.o -n=m2
linked m2
has_bytes m2 "00000003 001400180020 00000000 00000006 000c00240008001c00100004 00000000"

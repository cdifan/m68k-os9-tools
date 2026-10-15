# m68k-os9-l68: the initialized data's pointer tables (M$IRefs: to the code, then to the data)
# list the pointers object by object, the last object linked first, each object's in address
# order, as both Microware linkers (measured 2026-10-15: these sources' modules, linked in both
# orders, identical to C 3.2's L68's); before, all in address order. elf2mod takes the objects
# from GNU ld's map (--ld-map).
# fixed: a0f6d83 2026-10-15 r68togas, l68, elf2mod: fixes from rebuilds of modules
src ra <<'EOF'
 psect ra,$101,$8000,1,0,entry
 vsect
rp1 dc.l entry
rd1: dc.l rp1
rp2 dc.l fn1
 ends
entry rts
fn1 rts
 ends
EOF
src rb <<'EOF'
 psect rb,0,0,0,0,0
 vsect
ap1 dc.l afn
ad1 dc.l ap1
 ends
afn rts
 ends
EOF
src rc <<'EOF'
 psect rc,0,0,0,0,0
 vsect
bp1 dc.l bfn
bd1 dc.l bp1
bd2 dc.l rd1
 ends
bfn rts
 ends
EOF
asm ra; asm rb; asm rc
link m1 ra.o rb.o rc.o -n=m1
linked m1
# code: rc's 14, rb's 0c, ra's 00 08; data: rc's 18 1c, rb's 10, ra's 04
has_bytes m1 "00000004 0014000c00000008 00000000 00000004 0018001c00100004 00000000"
link m2 ra.o rc.o rb.o -n=m2
linked m2
has_bytes m2 "00000004 0018000c00000008 00000000 00000004 001c00100014 0004 00000000"

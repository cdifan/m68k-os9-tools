# m68k-os9-l68: within an object, the pointer tables (M$IRefs) list the pointers vsect by vsect in
# the order of the source, each vsect's in address order: here the remote vsects' pointers (0c,
# 10) between the plain ones' (00, 04), as both Microware linkers list them (measured 2026-10-17:
# this source with Ultra C's r68 and l68; a C 3.2 C program declaring remote pointers first with
# C 3.2's l68). Before, each object's in address order. From r68's ELF (its .r68ord records) and
# from a ROF (rof2elf's records).
# fixed: 2026-10-17 elf2mod, rof2elf: pointers vsect by vsect in an object
src irm <<'EOF'
 psect irm,$101,$8000,1,0,entry
 vsect remote
rp dc.l fa
 ends
 vsect
pp dc.l fb
 ends
 vsect remote
rq dc.l fb
 ends
 vsect
pq dc.l fa
pd dc.l rq
 ends
entry rts
fa rts
fb rts
 ends
EOF
asm irm
asm irm -obj=rof
mv irm.o irm.r
asm irm
# code: rp 0c, pp 00, rq 10, pq 04; data: pd 08
tables="0000 0004 000c 0000 0010 0004 0000 0000 0000 0001 0008 0000 0000"
link m_elf irm.o -n=irm
linked m_elf
has_bytes m_elf "$tables"
link m_rof irm.r -n=irm
linked m_rof
has_bytes m_rof "$tables"

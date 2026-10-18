# m68k-os9-l68 -b=4 (Ultra C's style): each psect's data padded to a multiple of 4 from the start
# of its section, not aligned in the address space, as Ultra C's l68 pads them (measured
# 2026-10-19: this link by Ultra C's r68 and l68 is byte-identical). Here the remote data starts
# right after a one-entry jump table, at 2 mod 4 ($0A), and its psects follow every 4 bytes.
# Before, the remote data and each psect's were aligned to 4 ($0C, $10, $14).
# fixed: 67ec61d 2026-10-19 elf2mod, l68: Ultra C's jump table and M$IRefs order; -b= pads from the section's start
src ja <<'EOF'
 psect ja,$101,$8000,1,0,e
 vsect
d1 dc.l 1
 ends
e bsr zb
 lea r1(a6),a0
 lea r2(a6),a0
 lea r3(a6),a0
 rts
 ends
EOF
{ echo ' psect jf,0,0,0,0,0'; for i in $(seq 10000); do echo ' dc.l $4e714e71'; done; echo ' ends'; } > jf.a
src jb <<'EOF'
 psect jb,0,0,0,0,0
 vsect remote
r1: dc.w 1
 ends
zb: rts
 ends
EOF
src jc <<'EOF'
 psect jc,0,0,0,0,0
 vsect remote
r2: dc.w 2
 ends
 ends
EOF
src jd <<'EOF'
 psect jd,0,0,0,0,0
 vsect remote
r3: dc.w 3
 ends
 ends
EOF
for s in ja jf jb jc jd; do asm $s; done
link m -dialect=ucc -a -b=4 ja.o jf.o jb.o jc.o jd.o -r=400000
linked m
has_bytes m "4eae0004 41ee000a 41ee000e 41ee0012 4e75"

# m68k-os9-l68 -a: the jump table's entries in Ultra C's l68 order: by the object of a target's
# first far reference, in link order; among one object's new targets, those of the files given
# first, then those of the -l= libraries in their order; then by name (measured 2026-10-19: this
# link by Ultra C's r68 and l68 is byte-identical; before, by object and name only, the entries
# were xa ya zb aab).
# fixed: 67ec61d 2026-10-19 elf2mod, l68: Ultra C's jump table and M$IRefs order; -b= pads from the section's start
src ja <<'EOF'
 psect ja,$101,$8000,1,0,e
e bsr xa
 bsr zb
 bsr ya
 rts
 ends
EOF
src jc <<'EOF'
 psect jc,0,0,0,0,0
c1: bsr aab
 bsr ya
 rts
 ends
EOF
{ echo ' psect jf,0,0,0,0,0'; for i in $(seq 10000); do echo ' dc.l $4e714e71'; done; echo ' ends'; } > jf.a
src jb <<'EOF'
 psect jb,0,0,0,0,0
zb: rts
 ends
EOF
src l1a <<'EOF'
 psect l1a,0,0,0,0,0
ya: rts
 ends
EOF
src l1b <<'EOF'
 psect l1b,0,0,0,0,0
aab: rts
 ends
EOF
src l2a <<'EOF'
 psect l2a,0,0,0,0,0
xa: rts
 ends
EOF
for s in ja jc jf jb l1a l1b l2a; do asm $s; done
lib l1.a l1a.o l1b.o
lib l2.a l2a.o
link m ja.o jc.o jf.o jb.o -a -l=l1.a -l=l2.a -r=400000
linked m
# zb (the file's), ya (the first library's), xa (the second's), then jc's aab
has_bytes m "4ef9 00009c5c 4ef9 00009c60 4ef9 00009c68 4ef9 00009c64"

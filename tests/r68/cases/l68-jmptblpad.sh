# m68k-os9-l68 -jmptblpad=<n>: with a jump table (-a), n unused entries (jmp 0) after the used
# ones, listed among the pointers to code as the used ones are, as some of Ultra C's l68 -a links
# have them (the rule for how many is unknown: an option for byte-identical rebuilds). Without a
# jump table it changes nothing; -jmptblpad=x is an error.
# fixed: 1f1377c 2026-10-18 l68: -jmptblpad= (elf2mod --jmptbl-spare): unused jump table entries
# A far call: 40K of code between it and its target.
src ja <<'EOF2'
 psect ja,$101,$8000,1,0,e
e bsr X
 rts
 ends
EOF2
{ echo ' psect jf,0,0,0,0,0'; for i in $(seq 10000); do echo ' dc.l $4e714e71'; done; echo ' ends'; } > jf.a
src jb <<'EOF2'
 psect jb,0,0,0,0,0
X: rts
 ends
EOF2
for s in ja jf jb; do asm $s; done
link m0 ja.o jf.o jb.o -a -r=400000
linked m0
link m2 ja.o jf.o jb.o -a -jmptblpad=2 -r=400000
linked m2
n0=$(wc -c < m0); n2=$(wc -c < m2)
# two entries (12 bytes) and their two table entries (4 bytes) more
[ $((n2 - n0)) = 16 ] || why="$why m2 is $n2 bytes, m0 $n0;"
# the used entry, then the two spare ones
has_bytes m2 "4ef9 00009c48 4ef9 00000000 4ef9 00000000"
lacks_bytes m0 "4ef9 00009c48 4ef9 00000000"
# without far calls: no table, nothing added
src s <<'EOF'
 psect s,$101,$8000,1,0,e
e rts
 ends
EOF
asm s
link s0 s.o -a -r=400000
linked s0
link s2 s.o -a -jmptblpad=2 -r=400000
linked s2
cmp -s s0 s2 || why="$why -jmptblpad changed a link without a jump table;"
link e1 s.o -a -jmptblpad=x
failed_with e1 "-jmptblpad=x: expected a number of entries"
# in a module too: the spare entry jumps to the module's start (offset 0, relocated at load)
link mm ja.o jf.o jb.o -a -jmptblpad=1
linked mm
has_bytes mm "4ef9 00000000"
link mn ja.o jf.o jb.o -a
linked mn
lacks_bytes mn "4ef9 00000000"

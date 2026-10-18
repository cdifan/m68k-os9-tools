# m68k-os9-l68 -g: symbols at the same value in the order Microware's l68 writes them (measured
# 2026-10-18 with C 3.2's l68 and Ultra C's, data labels at the end of the data area tying with
# end): C 3.2's lists, in reverse order of definition, the symbols of the psects after the root,
# then its own (end), then the root psect's; Ultra C's the program's in link order, then its own.
# Before, C 3.2 style had the program's first, and Ultra C style sorted them by name. The order of
# the names in the symbol module is that of the entries.
src ra <<'EOF'
 psect ra,$E01,$8001,1,0,e
e rts
 vsect
x ds.b 8
Za:
 ends
 ends
EOF
src rb <<'EOF'
 psect rb,$E01,$8001,1,0,e
e rts
 vsect
x ds.b 8
 ends
 ends
EOF
src sb <<'EOF'
 psect sb,0,0,0,0,0
 vsect
Zb:
 ends
cb rts
 ends
EOF
src sd <<'EOF'
 psect sd,0,0,0,0,0
 vsect
Yd ds.b 4
Zd:
 ends
cd rts
 ends
EOF
for s in ra rb sb sd; do asm $s; done
names() { printf '%s\0' "$@" | od -An -tx1 -v | hex; }
link c5 ra.o sd.o -g
linked c5
has_bytes c5.stb "$(names btext Zd end Za bname etext)"
link c6 rb.o sd.o sb.o -g
linked c6
has_bytes c6.stb "$(names btext Zb Zd end bname etext)"
link u6 rb.o sd.o sb.o -g -t=os9_68k
linked u6
has_bytes u6.stb "$(names _btext _bdata btext bdata Zd Zb _enddata end _bname bname _etext etext)"

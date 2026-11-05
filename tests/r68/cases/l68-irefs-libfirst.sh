# m68k-os9-l68: an object's pointers to other objects' globals in M$IRefs: Ultra C's l68 lists
# those to names that -l= libraries' members define first, then those to the given files' names,
# each by name descending (measured 2026-11-05: these sources' link by Ultra C's r68 and l68 is
# byte-identical; and all 49 such objects of a 260 KB program's link); C 3.2's l68 lists them
# all by name descending (-dialect=c32). Before, both dialects listed them as C 3.2's l68.
# fixed: 6c3fe06 2026-11-05 l68: commons placed for Ultra C's l68 too; elf2mod: its pointer table order
src ra <<'EOF'
 psect ra,$101,$8000,1,0,e
 vsect
p1 dc.l zf
p2 dc.l la
p3 dc.l zg
p4 dc.l lb
 ends
e rts
 ends
EOF
src rz <<'EOF'
 psect rz,0,0,0,0,0
zf: rts
zg: rts
 ends
EOF
src rl <<'EOF'
 psect rl,0,0,0,0,0
la: rts
lb: rts
 ends
EOF
for s in ra rz rl; do asm $s; done
lib l.a rl.o
link mu ra.o rz.o -l=l.a -n=m
linked mu
# lb la (the library's), then zg zf (the file's)
has_bytes mu "0000 0004 000c 0004 0008 0000 0000 0000"
link mc ra.o rz.o -l=l.a -dialect=c32 -n=m
linked mc
# zg zf lb la
has_bytes mc "0000 0004 0008 0000 000c 0004 0000 0000"

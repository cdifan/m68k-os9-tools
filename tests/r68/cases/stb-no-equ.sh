# The symbol module (-g) lists no equates, as both l68s: neither a library's nor the program's own
# global ones, nor the link-time expressions and section aliases of a label plus an external
# equate.
# fixed: 2026-10-12 elf2mod: no equates, r68x_ or r68l_ symbols in the symbol module
src stbequs <<'EOF'
 psect stbequs,0,0,0,0,0
Fieldq: equ 4
Otherq: equ $77
 ends
EOF
src stbprog <<'EOF'
 psect stbprog,$101,$8000,1,0,Start
Ownq: equ $1234
 vsect
Blockq ds.l 4
 ends
Start: move.l a0,Blockq+Fieldq(a6)
 rts
 ends
EOF
asm stbequs -obj=rof
cp stbequs.o stbequs.l
asm stbprog
link stbprog stbprog.o -l=stbequs.l -g
linked stbprog
[ -s stbprog.stb ] || why="$why no stbprog.stb;"
has_bytes stbprog.stb "$(printf Start | od -An -tx1)"
for n in Fieldq Otherq Ownq r68x_ r68l_; do lacks_bytes stbprog.stb "$(printf $n | od -An -tx1)"; done

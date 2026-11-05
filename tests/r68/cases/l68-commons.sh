# m68k-os9-l68: common symbols placed as both Microware linkers place them (C 3.2's l68 measured
# 2026-11-05 on a 301-object link's map and on small links, Ultra C's on a 260 KB program's link
# and on these sources): in link order, each object's block is its vsect, then the commons it is
# the first to define, in its order, each at the next multiple of 4 from the block's start, a
# common defined again with a larger size taking the largest; the block ends even; with -b= in
# Ultra C's format the block, commons included, is padded. A static of a common's name elsewhere
# is another symbol, a common is defined by a global definition. Before, all commons came after
# all objects' vsects (GNU ld's place); then, from ab7c27c, so only with -dialect=c32. C 3.2's
# l68 has a bug here: when an object has a common that a later object defines (c4 in cb), it puts
# that object's other new commons at the block's offset 1, over its vsect, and the block keeps
# the vsect's size (these sources: c3 at 25); Ultra C's l68 and ours don't.
# fixed: ab7c27c 2026-11-05 l68: C 3.2's place for common symbols; elf2mod: a jump table entry per name
# fixed: 6c3fe06 2026-11-05 l68: commons placed for Ultra C's l68 too; elf2mod: its pointer table order
src ca <<'EOF'
 psect ca,$101,$8000,1,0,e
 vsect
v1: ds.b 3
c1: com.w 1
c2: com.l 2
 ends
e lea v1(a6),a0
 lea c1(a6),a0
 lea c2(a6),a0
 lea c3(a6),a0
 lea v2(a6),a0
 lea v3(a6),a0
 lea c4(a6),a0
 rts
 ends
EOF
src cb <<'EOF'
 psect cb,0,0,0,0,0
 vsect
v2: ds.w 1
c2: com.l 4
c3: com.b 1
c4: com.l 1
 ends
 ends
EOF
src cc <<'EOF'
 psect cc,0,0,0,0,0
 vsect
v3: ds.l 1
c3 ds.w 1
 ends
 lea c3(a6),a0
 rts
 ends
EOF
src cd <<'EOF'
 psect cd,0,0,0,0,0
 vsect
c4: ds.w 1
 ends
 ends
EOF
for s in ca cb cc cd; do asm $s -dialect=c32; done
link m ca.o cb.o cc.o cd.o -dialect=c32 -n=m
linked m
# ca: v1 0, c1 4, c2 8 (16 bytes: cb's size), to 24; cb: v2 24, c3 28 (c2 is ca's, c4 cd's
# definition), to 30; cc: v3 30, its static c3 34; cd: c4 36
has_bytes m "41ee8000 41ee8004 41ee8008 41ee801c 41ee8018 41ee801e 41ee8024 4e75 41ee8022 4e75"
# Ultra C's l68 (the default dialect): the same
link u ca.o cb.o cc.o cd.o -n=u
linked u
has_bytes u "41ee8000 41ee8004 41ee8008 41ee801c 41ee8018 41ee801e 41ee8024 4e75 41ee8022 4e75"
# with -b=4 in Ultra C's format: cb's block (6 bytes) and cc's padded to 8: v3 32, c3 36, c4 40
link b ca.o cb.o cc.o cd.o -b=4 -t=os9_68k -n=b
linked b
has_bytes b "41ee8000 41ee8004 41ee8008 41ee801c 41ee8018 41ee8020 41ee8028 4e75"
has_bytes b "41ee8024 4e75"

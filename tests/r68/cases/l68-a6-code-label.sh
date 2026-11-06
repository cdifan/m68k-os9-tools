# A code label as an a6-relative operand: as a dc.w it gets its module offset, which as an operand
# means nothing, so the link warns, one line each, and goes on; a dc.w in the code doesn't warn,
# also where the labels before it read like an instruction, and neither does a code label as an
# immediate.
# fixed: 216de38 2026-11-06 elf2mod: warn about a code label as an a6-relative operand
# fixed: f6dcab1 2026-11-06 elf2mod: the a6 warning sees relocated words and more reads
src ca <<'EOF'
 psect ca,$101,$8000,1,0,e
e pea tab(a6)
 lea tab(a6),a0
 move.b tab(a6),d0
 cmpi.b #1,tab(a6)
 cmpi.l #1,tab(a6)
 andi.w #1,tab(a6)
 andi.w #tab,tab(a6)
 cmpi.l #tab,tab(a6)
 bclr #1,tab(a6)
 bset #1,tab(a6)
 btst d0,tab(a6)
 movem.l tab(a6),d0-d1
 movem.w tab(a6),d0-d1
 rts
tab dc.b 1,2
 ends
EOF
# the first table's first entry is $006E, ori.w #x,d16(a6), so that its third looked like the
# field; the second's $00AE, ori.l, so that its fourth did
src cd <<'EOF'
 psect cd,$101,$8000,1,0,e
e dc.w t6,0,e2
 dc.l 0,0,0,0,0,0,0,0,0,0,0,0,0
 dc.l 0,0,0,0,0,0,0,0,0,0,0,0,0
t6: dc.w 0,0
 dc.w ta,0,0,e2
 dc.l 0,0,0,0,0,0,0,0,0,0,0,0,0
ta: rts
e2 move.w #e,4(a6)
 rts
 ends
EOF
asm ca
asm cd
link ca ca.o -n=ca
linked ca
n=$(grep -c 'warning: .*(a6) at \.text+' ca.log)
[ "$n" = 13 ] || why="$why $n warnings, not 13: $(head -3 ca.log | cut -c1-90);"
# movep with a symbol keeps its relocation only in r68.exe's object: gas leaves it out
if [ -n "${R68:-}" ]; then
	src cm <<'EOF'
 psect cm,$101,$8000,1,0,e
e movep.w tab(a6),d0
 movep.l tab(a6),d0
 rts
tab dc.b 1,2
 ends
EOF
	mwasm cm
	link cm cm.r -n=cm
	linked cm
	n=$(grep -c 'warning: .*(a6) at \.text+' cm.log)
	[ "$n" = 2 ] || why="$why $n warnings for movep, not 2: $(head -2 cm.log | cut -c1-90);"
fi
link cd cd.o -n=cd
linked cd
if grep -q 'warning:' cd.log; then why="$why a warning for dc.w: $(head -2 cd.log | cut -c1-90);"; fi
${B}nm cd.elf | grep -q '0*40006e [a-zA-Z] t6$' || why="$why t6 isn't at \$6E: the test no longer tests;"
${B}nm cd.elf | grep -q '0*4000ae [a-zA-Z] ta$' || why="$why ta isn't at \$AE: the test no longer tests;"

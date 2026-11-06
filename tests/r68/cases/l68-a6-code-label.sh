# A code label as an a6-relative operand: as a dc.w it gets its module offset, which as an operand
# means nothing, so the link warns, one line each, and goes on; a dc.w in the code doesn't warn.
# fixed: 216de38 2026-11-06 elf2mod: warn about a code label as an a6-relative operand
src ca <<'EOF'
 psect ca,$101,$8000,1,0,e
e pea tab(a6)
 lea tab(a6),a0
 move.b tab(a6),d0
 cmpi.b #1,tab(a6)
 cmpi.l #1,tab(a6)
 rts
tab dc.b 1,2
 ends
EOF
src cd <<'EOF'
 psect cd,$101,$8000,1,0,e
e rts
tbl dc.w e,e2
e2 rts
 ends
EOF
asm ca
asm cd
link ca ca.o -n=ca
linked ca
n=$(grep -c 'warning: .*(a6) at \.text+' ca.log)
[ "$n" = 5 ] || why="$why $n warnings, not 5: $(head -3 ca.log | cut -c1-90);"
link cd cd.o -n=cd
linked cd
if grep -q 'warning:' cd.log; then why="$why a warning for dc.w: $(head -2 cd.log | cut -c1-90);"; fi

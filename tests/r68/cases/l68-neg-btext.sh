# m68k-os9-l68: a lone negated reference to a symbol of the code (dc.l -btext, 0-btext, as a ROF
# writes it: one negated external reference) in a ROM linked at $400000 is -$400000 ($FFC00000),
# as Microware's L68 links it (seen in a ROM image of 1995); before, $00400000: ld keeps -sym of a
# section-relative symbol relative to its section, so the link-time expression's terms that are
# negated or multiplied are ABSOLUTE() now. -Ext of an equate and Fun-btext as before.
# fixed: 364e1e2 2026-10-16 r68x_note: negated terms of link-time expressions as absolute values
src na <<'EOF'
 psect na,$101,$8000,1,0,e
e rts
 dc.l -Ext
 dc.l -btext
 dc.l Fun-btext
 dc.l 0-btext
 ends
EOF
src nb <<'EOF'
 psect nb,0,0,0,0,0
Ext: equ $1234
Fun: rts
 ends
EOF
for s in na nb; do
	m68k-os9-r68 $s.a -q -obj=rof -o=$s.r > $s.log 2>&1 || why="$why $s: -obj=rof failed: $(head -1 $s.log);"
done
link rom na.r nb.r -r=400000
linked rom
has_bytes rom "4e75 ffffedcc ffc00000 00000014 ffc00000"

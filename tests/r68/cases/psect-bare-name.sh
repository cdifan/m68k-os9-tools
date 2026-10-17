# A psect without name or operands: named "program" in the ROF, as both Microware assemblers name
# it (C 3.2's R68 and Ultra C's r68, measured 2026-10-17); before, the source file's base name.
# fixed: 2026-10-17 r68togas: a bare psect named program
src bare <<'EOF'
 psect
 rts
 ends
EOF
for d in ucc c32; do
	m68k-os9-r68 bare.a -q -dialect=$d -obj=rof -o=bare-$d.r > bare-$d.log 2>&1 ||
		why="$why -dialect=$d: assembling failed: $(head -1 bare-$d.log);"
	has_bytes bare-$d.r "70726f6772616d00"
	lacks_bytes bare-$d.r "6261726500"
done

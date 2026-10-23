# Psect statements in both branches of a conditional: the ROF is named by the psect assembled,
# as both Microware assemblers name it (C 3.2's R68 and Ultra C's r68); before, by the last psect
# statement in the source, assembled or not.
# fixed: 4bd611a 2026-10-23 r68togas: the ROF name of the psect statement assembled
src cond <<'EOF'
 ifdef X
 psect Aaa,0,0,1,0,0
 else
 psect Bbb,0,0,1,0,0
 endc
 moveq #1,d0
 ends
EOF
for d in ucc c32; do
	for a in "-a=X" ""; do
		m68k-os9-r68 cond.a -q -dialect=$d -obj=rof $a -o=cond.r > cond.log 2>&1 ||
			why="$why -dialect=$d $a: assembling failed: $(head -1 cond.log);"
		if [ -n "$a" ]; then
			has_bytes cond.r "41616100"; lacks_bytes cond.r "42626200"
		else
			has_bytes cond.r "42626200"; lacks_bytes cond.r "41616100"
		fi
	done
done
if [ -n "${R68:-}" ]; then
	crlf cond.a "$mw/cond.a"
	r68exe cond -a=X
	has_bytes "$mw/cond.r" "41616100"
fi

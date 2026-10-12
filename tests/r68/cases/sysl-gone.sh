# -sysl= (the library whose equates were folded in) is no longer an option: an error naming it,
# not r68's option letters (it read -s, then -y: "-y ... isn't supported").
# fixed: 2aefb81 2026-10-12 No folding of a library's equates: a label plus external equates as a link-time expression; -sysl= and rof2elf's -e gone
src t <<'EOF'
 psect t,0,0,0,0,0
 nop
 ends
EOF
if m68k-os9-r68 t.a -q -sysl=none -o=t.o > t.log 2>&1 < /dev/null; then
	why="$why -sysl= accepted;"
elif ! grep -q -- '-sysl= is no longer an option' t.log; then
	why="$why no error naming -sysl=: $(head -1 t.log);"
fi

# gastor68 on r68togas's output: an inlined use file is its use statement again; its text's sum
# (r68togas's comment, insensitive to layout: line ends, runs of blanks, blanks at a line's end,
# blank lines) is checked against the file: no warning for the same text in another layout, a
# warning for another text.
# fixed: 08be2bc 2026-10-19 r68togas, gastor68: round trip closer to the source; -bt with spanreg; -mspanreg
src defs <<'EOF'
Aa equ 1
Bb  equ   2
EOF
src u <<'EOF'
 psect u,0,0,0,0,0
 use "defs.a"
 move.w #Aa+Bb,d0
 ends
EOF
m68k-os9-r68 u.a -q -obj=gas -o=u.s > u.log 2>&1 || why="$why -obj=gas failed: $(head -1 u.log);"
back() {	# NAME: gastor68 u.s, its messages in NAME.log
	gastor68 -o "$1.a" u.s > "$1.log" 2>&1 || why="$why gastor68 failed ($1): $(head -1 "$1.log");"
}
back same
grep -q -i 'use "defs.a"' same.a || why="$why no use line: $(grep -i use same.a | head -1);"
grep -q 'warning' same.log && why="$why a warning for the same file;"
printf 'Aa\tequ 1   \r\n\r\nBb equ 2\r\n' > defs.a
back layout
grep -q 'warning' layout.log && why="$why a warning for another layout: $(head -1 layout.log);"
printf 'Aa equ 1\nBb equ 3\n' > defs.a
back other
grep -q 'warning: use file .*defs.a has changed' other.log || why="$why no warning for another text ($(head -1 other.log));"

# dc.w Ext-* and Ext-Label (rof-mw-forms, rof-ext-label) leave a macro's \@ numbers as they are: a
# label L\@ of the first expansion after them is L0, as without them (before, r68togas's note of
# each such line used a macro of its own, which gas counted: L2 here).
# fixed: 2026-10-16 r68togas: .r68pcm notes without a macro
src an <<'EOF'
 psect an,0,0,0,0,0
L macro
L\@: nop
 endm
Tbl dc.w Ext_a-Tbl
 dc.w Ext_b-*
 L
 ends
EOF
asm an
${B}nm -g an.o 2> /dev/null | grep -q " L0$" || why="$why no L0: $(${B}nm -g an.o 2> /dev/null | grep ' L' | tr '\n' ' ');"

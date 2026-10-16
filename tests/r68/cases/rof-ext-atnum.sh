# dc.w Ext-* and Ext-Label (rof-mw-forms, rof-ext-label) and byte immediates of externals (word
# relocations, byte-imm-ext) leave a macro's \@ numbers as they are: a label L\@ of the first
# expansion after them is L0, as without them (before, r68togas wrote each through a macro of its
# own, which gas counted: L2 after the dc.w lines, L5 with the immediates). Each immediate keeps
# its own value through the symbol they share, in a rept too.
# fixed: 2026-10-16 r68togas: byte immediates without a macro
src an <<'EOF'
 psect an,0,0,0,0,0
L macro
L\@: nop
 endm
Tbl dc.w Ext_a-Tbl
 dc.w Ext_b-*
 andi #Ext_c,ccr
 ori.b #Ext_d+2,d1
 btst #Ext_e,d0
 L
 rept 2
 andi.b #Ext_c-1,d2
 endr
 ends
EOF
asm an
${B}nm -g an.o 2> /dev/null | grep -q " L0$" || why="$why no L0: $(${B}nm -g an.o 2> /dev/null | grep ' L' | tr '\n' ' ');"
r=$(${B}objdump -r an.o 2> /dev/null | awk '$2 == "R_68K_16" {print $1, $3}' | tr '\n' ' ')
[ "$r" = "00000006 Ext_c 0000000a Ext_d+0x00000002 0000000e Ext_e 00000014 Ext_c-0x00000001 00000018 Ext_c-0x00000001 " ] ||
	why="$why word relocations: $r;"

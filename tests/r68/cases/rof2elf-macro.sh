# rof2elf: ROF's symbol kind 7, a macro name defined with a colon (PaintIt: macro), which both
# Microware assemblers export with the value 0: an absolute global.
# fixed: 4283498 2026-10-10 r68togas, rof2elf, m68k-os9-r68, os9link: corpus fixes; l68's PC-relative error
needs_r68
src mdef <<'EOF'
 psect mdef,0,0,0,0,0
PaintIt: macro
 nop
 endm
 PaintIt
 ends
EOF
mwasm mdef
rof2elf -o mdef.o mdef.r > rof2elf.log 2>&1 || why="$why rof2elf failed: $(head -1 rof2elf.log);"
has_sym mdef.o "00000000 A PaintIt"

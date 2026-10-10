# rof2elf: a constant less the code's own address in code (dc.w K-*), a ROF reference with only a
# negative code part: PC-relative to the absolute zero.
# fixed: 4283498 2026-10-10 r68togas, rof2elf, m68k-os9-r68, os9link: corpus fixes; l68's PC-relative error
needs_r68
src negc <<'EOF'
 psect negc,0,0,0,0,0
 nop
 dc.w 100-*
 ends
EOF
mwasm negc
rof2elf -o negc.o negc.r > rof2elf.log 2>&1 || why="$why rof2elf failed: $(head -1 rof2elf.log);"
has_reloc negc.o "R_68K_PC16"

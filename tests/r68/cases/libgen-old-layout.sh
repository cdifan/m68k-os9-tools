# rof2elf and m68k-os9-l68: a library in Ultra C 1.x libgen's older layout ($2D00D5BB: a 42-byte
# header, 62-byte psect entries, reference entries with a 2-byte count), as in OS-9 V3.0's
# libraries; before, rof2elf refused it ("not a ROF"). The library below was made by
# m68k-os9-libgen -obj=ucc from two members (fun_a: move.w #Ext_v,d0, bsr fun_b, rts; fun_b:
# moveq #3,d1, rts) and converted to the older layout, its hash table left out (unused here).
# fixed: FIXME
perl -e 'print pack("H*", "2d00d5bbff017e0a0f0e0514000100000000000000000000002c000000240000007c0000000e0000000e00040000000000000009ffffffffffffffff000000000004000000000000001effffffffffffffff000000016e65772e6c006d610066756e5f61004578745f760066756e5f62006d620066756e5f62000009000000000000000000000000000000000000000c0000000000000000000001120000012a0000000000000000000000000000000000000006000000000009000000000000000000000000000000000000000400000000000000000000013c000001400000000000000000ffffffffffffffff0000001b0000000100010000011effffffff00000001000100000124ffffffff0000000f303c0000610000004e754e7100b00000000600300000000200000000000000000000000000000000000072034e75000000000000000000000000000000000000")' > old.l
rof2elf -o old.o old.l > rof2elf.log 2>&1 || why="$why rof2elf failed: $(head -1 rof2elf.log);"
has_sym old.o "00000000 T fun_a"
has_sym old.o "00000000 T fun_b"
has_reloc old.o "R_68K_16          Ext_v"
has_reloc old.o "R_68K_PC16        fun_b"
src root <<'EOF'
 psect root,$101,$8000,1,0,entry
Ext_v: equ $1234
entry bsr fun_a
 rts
 ends
EOF
asm root
link mod root.o -l=old.l -n=mod
linked mod
has_bytes mod "303c1234 6100"
has_bytes mod "7203 4e75"

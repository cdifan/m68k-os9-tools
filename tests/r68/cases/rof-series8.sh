# -obj=rof8: a ROF of series 8, as the R68 of 1987 wrote it: series 8 in the header and no debug
# size field (4 bytes) before the name; otherwise as series 9 (-obj=rof). rof2elf reads it back to
# the same code. elf2rof -s 8 does the conversion.
# fixed: 6d5575e 2026-10-17 elf2rof -s 8, m68k-os9-r68 -obj=rof8: ROF series 8
src s8 <<'EOF'
 psect s8_p,0,0,1,0,0
Ent: lea Ext_a(pc),a0
 move.w #5,d0
 rts
 ends
EOF
for k in rof rof8; do
	m68k-os9-r68 s8.a -q -dialect=c32 -obj=$k -o=s8-$k.r > s8-$k.log 2>&1 ||
		why="$why -obj=$k failed: $(head -1 s8-$k.log);"
done
h9=$(filehex s8-rof.r); h8=$(filehex s8-rof8.r)
[ "${h8:20:4}" = 0008 ] || why="$why series ${h8:20:4}, not 0008;"
[ "${h9:20:4}" = 0009 ] || why="$why -obj=rof: series ${h9:20:4}, not 0009;"
# the same but for the series, the date and the 4 bytes at offset 52
[ "${h8:0:20}${h8:36:68}${h8:104}" = "${h9:0:20}${h9:36:68}${h9:112}" ] ||
	why="$why series 8 isn't series 9 without the debug size field;"
rof2elf -o back.o s8-rof8.r > back.log 2>&1 || why="$why rof2elf of series 8 failed: $(head -1 back.log);"
rof2elf -o back9.o s8-rof.r > /dev/null 2>&1
[ "$(${B}objdump -s -j .text back.o | tail -n +4)" = "$(${B}objdump -s -j .text back9.o | tail -n +4)" ] ||
	why="$why the code read back differs;"

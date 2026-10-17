# An absolute reference from code to code (move.l #label,d1; also word-sized) in a ROF: a local
# reference to code located in code ($3C long, $34 word) with the label's code offset in the
# field, which l68 resolves to the label's offset in the module, as both Microware assemblers
# write it and both Microware linkers resolve it (measured 2026-10-17: C 3.2's R68 writes this ROF
# as -dialect=c32 does now, but for the date; the modules' code is the same as ours). Before,
# elf2rof refused the long one ("not position-independent"); the ELF link already did as l68.
# fixed: 2026-10-17 elf2rof: absolute long references from code to code
src ac <<'EOF'
 psect ac_p,$101,$8000,1,0,c1
 nop
c1 move.l #c3,d1
c2 move.w #c3,d2
 move.l #c1+6,d3
c3 lea c1(pc),a0
 rts
 ends
EOF
# the count, then type and offset of each: 3c:e 34:a 3c:4; the code with the code offsets
refs=0003003c0000000e00340000000a003c00000004
for d in ucc c32; do
	m68k-os9-r68 ac.a -q -dialect=$d -obj=rof -o=ac-$d.r > ac-$d.log 2>&1 ||
		why="$why -dialect=$d: assembling failed: $(head -1 ac-$d.log);"
	has_bytes ac-$d.r "$refs"
	has_bytes ac-$d.r "4e71223c00000012343c0012263c00000008"
done
# linked by l68, from the ROF and from the ELF object: the offsets in the module
m68k-os9-r68 ac.a -q -dialect=c32 -o=ac.o > ac-elf.log 2>&1 ||
	why="$why assembling the ELF object failed: $(head -1 ac-elf.log);"
for o in ac.o ac-c32.r; do
	m68k-os9-l68 $o -o=mod-${o%.*} -n=ac > l68-${o%.*}.log 2>&1 ||
		why="$why linking $o failed: $(head -1 l68-${o%.*}.log);"
	has_bytes mod-${o%.*} "4e71223c0000005e343c005e263c00000054"
done

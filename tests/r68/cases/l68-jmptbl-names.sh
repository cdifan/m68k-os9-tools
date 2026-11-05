# m68k-os9-l68 -a: a jump table entry per name, also for two names of one address (both
# Microware linkers, measured 2026-11-05: these sources' link by Ultra C's r68 and l68 has two
# entries with the same target); before, one entry per address, shared by both names.
# fixed: ab7c27c 2026-11-05 l68: C 3.2's place for common symbols; elf2mod: a jump table entry per name
src ja <<'EOF'
 psect ja,$101,$8000,1,0,e
e bsr fa
 bsr fb
 bsr fa
 rts
 ends
EOF
{ echo ' psect jf,0,0,0,0,0'; for i in $(seq 10000); do echo ' dc.l $4e714e71'; done; echo ' ends'; } > jf.a
src jb <<'EOF'
 psect jb,0,0,0,0,0
fa:
fb: rts
 ends
EOF
for s in ja jf jb; do asm $s; done
for d in ucc c32; do
	link m-$d ja.o jf.o jb.o -a -dialect=$d -n=m
	linked m-$d
	grep -q '_jmptbl: 2 of 2 entries used, for 3 far references' m-$d.log ||
		why="$why $d: $(grep _jmptbl m-$d.log);"
	# the calls: jsr to the first entry, the second, the first again
	has_bytes m-$d "4eae8000 4eae8006 4eae8000 4e75"
done

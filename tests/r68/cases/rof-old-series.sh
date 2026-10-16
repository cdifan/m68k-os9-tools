# rof2elf: ROFs of the older series 8 (no debug size after the remote initialized data) and 6
# (one long after the trap entry), as in the libraries of 1987-88; before, "unsupported ROF
# series". Made here from a series-9 ROF (-obj=rof) by leaving those fields out; each converted,
# its code and global symbol as from the series-9 one, and linked as a library.
# fixed: 2eb0dbc 2026-10-16 rof2elf: ROF series 8 and 6
src ol <<'EOF'
 psect ol,0,0,5,0,0
fun_o: moveq #7,d0
 rts
 ends
EOF
m68k-os9-r68 ol.a -q -obj=rof -o=ol9.r > ol.log 2>&1 || why="$why -obj=rof failed: $(head -1 ol.log);"
# series word at 10; the debug size at $34 (series 8: out), remote data and debug ($30..$37,
# series 6: out)
perl -e 'local $/; $_ = <STDIN>; substr($_, 10, 2) = "\0\x08"; substr($_, 0x34, 4) = ""; print' < ol9.r > ol8.r
perl -e 'local $/; $_ = <STDIN>; substr($_, 10, 2) = "\0\x06"; substr($_, 0x30, 8) = ""; print' < ol9.r > ol6.r
for s in 8 6; do
	rof2elf -o ol$s.o ol$s.r > ol$s.log 2>&1 || why="$why series $s: $(head -1 ol$s.log);"
	has_sym ol$s.o "00000000 T fun_o"
	has_bytes ol$s.o "7007 4e75"
done
src om <<'EOF'
 psect om,$101,$8000,1,0,e
e bsr fun_o
 rts
 ends
EOF
asm om
cp ol6.r ol6.l
link mod om.o -l=ol6.l -n=mod
linked mod
has_bytes mod "7007 4e75"

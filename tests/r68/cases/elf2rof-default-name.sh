# elf2rof without -n names the psect after the object's file symbol if that has no extension (our
# assembler writes the psect's name there), else after the file (GCC's file symbol is the source's
# name). Before, always after the file.
# fixed: 2026-10-17 elf2rof: the default name from the file symbol
src nm <<'EOF'
 psect nm_p,0,0,1,0,0
 rts
 ends
EOF
m68k-os9-r68 nm.a -q -dialect=c32 -obj=elf -o=other.o > nm.log 2>&1 ||
	why="$why assembling failed: $(head -1 nm.log);"
elf2rof -o other.r other.o > r1.log 2>&1 || why="$why elf2rof failed: $(head -1 r1.log);"
has_bytes other.r "6e6d5f7000"
lacks_bytes other.r "6f7468657200"
# a file symbol with an extension, as GCC writes it: the file's name
printf ' .file "src.c"\n .text\n rts\n' > g.s
${B}as g.s -o gobj.o > as.log 2>&1 || why="$why gas failed: $(head -1 as.log);"
elf2rof -o gobj.r gobj.o > r2.log 2>&1 || why="$why elf2rof of the gas object failed: $(head -1 r2.log);"
has_bytes gobj.r "676f626a00"
lacks_bytes gobj.r "7372632e6300"
# -n still wins
elf2rof -n given -o given.r other.o > r3.log 2>&1 || why="$why elf2rof -n failed: $(head -1 r3.log);"
has_bytes given.r "676976656e00"
lacks_bytes given.r "6e6d5f7000"

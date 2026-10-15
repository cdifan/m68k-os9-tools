# r68togas -s: each use file translated into a file of its own next to the output, named after
# it (vals.d gives vals.d.s), and included; assembled with gas -I and the outputs' directory, the
# code is the inline translation's. A use after byte data of odd length stays inline (gas aligns
# at an include there). No existing file is written over, and a refused run leaves nothing; -o
# naming a directory; -x. Before, -s and -x were unknown options.
# fixed: a9ea4bb 2026-10-13 r68togas: %-registers, -r, -y, the separate-files mode (-s, -x); docs brought up to date
mkdir defs out
cat > defs/vals.d <<'EOF'
Val1 equ 5
mymac macro
 moveq #\1,d2
 endm
 use "nest.d"
EOF
cat > defs/nest.d <<'EOF'
Val3 equ 7
EOF
cat > defs/more.d <<'EOF'
Val2 equ 6
EOF
cat > defs/twice.d <<'EOF'
 nop
 moveq #Val3,d3
EOF
src main <<'EOF'
 psect separate,0,0,0,0,0
 use defs/vals.d
 move.w #Val1,d0
 mymac 3
 use defs/twice.d
name dc.b "abc"
 use defs/more.d
 move.w #Val2,d1
 use defs/twice.d
 ends
EOF
r68togas -o inline.s main.a > inline.log 2>&1 || why="$why inline translation failed;"
r68togas -s -o out main.a > s.log 2>&1 || why="$why -s failed: $(head -1 s.log);"
for f in main.s vals.d.s nest.d.s twice.d.s; do
	[ -f out/$f ] || why="$why no out/$f;"
done
[ -f out/more.d.s ] && why="$why more.d (after odd byte data) not inline;"
grep -q '^ include vals.d.s' out/main.s || why="$why no include of vals.d.s;"
grep -q '^ \.mri 1' out/vals.d.s || why="$why vals.d.s without .mri 1;"
${B}as inline.s -o i.o > as.log 2>&1 || why="$why inline output not assembled: $(head -2 as.log | tail -1);"
${B}as -I out out/main.s -o s.o > as2.log 2>&1 || why="$why -s output not assembled: $(head -2 as2.log | tail -1);"
${B}objcopy -O binary -j .text i.o i.bin 2> /dev/null
${B}objcopy -O binary -j .text s.o s.bin 2> /dev/null
cmp -s i.bin s.bin || why="$why -s code differs from inline;"
has_bytes i.bin "303c0005 7403 4e71 7607 616263 00 323c0006 4e71 7607"
# no output over an existing file, the use files' outputs either
r68togas -s -o out main.a > again.log 2>&1 && why="$why written over out/main.s;"
grep -q 'exists' again.log || why="$why no \"exists\" ($(head -1 again.log));"
mkdir out2
: > out2/vals.d.s
r68togas -s -o out2/top.s main.a > over.log 2>&1 && why="$why written over out2/vals.d.s;"
# and what the refused run had written removed again
[ "$(ls -A out2)" = vals.d.s ] || why="$why left in out2: $(ls -A out2 | tr '\n' ' ');"
# -x: the extension of the outputs named after their sources
mkdir out3
r68togas -s -x asm -o out3 main.a > x.log 2>&1 || why="$why -x failed: $(head -1 x.log);"
[ -f out3/main.asm ] && [ -f out3/vals.d.asm ] || why="$why -x names: $(ls out3 | tr '\n' ' ');"

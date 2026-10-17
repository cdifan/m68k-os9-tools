# The ELF conversion of a ROF is made again when rof2elf is newer than it (its output changes
# with rof2elf), not only when the ROF is; a conversion newer than both is used as it is. Before,
# only the ROF's date counted: a newer rof2elf's changes never reached ROFs converted before.
# fixed: 819c09f 2026-10-17 os9conv: a conversion older than rof2elf made again
src cc <<'EOF'
 psect cc,$101,$8000,1,0,entry
entry rts
 ends
EOF
asm cc -obj=rof
mv cc.o cc.r
# the front end with its rof2elf and elf2mod in a directory of their own, rof2elf's date ours
l68=$(command -v m68k-os9-l68)
mkdir -p tools
for f in m68k-os9-l68 rof2elf elf2mod; do cp -p "$(dirname "$l68")/$f" tools/; done
export M68K_OS9_ELF=save=cache,cache=$PWD/ccache
touch -d '1999-01-01' cc.r
tools/m68k-os9-l68 cc.r -o=m1 -n=m1 > m1.log 2>&1 || why="$why linking m1 failed: $(head -1 m1.log);"
conv=$(find ccache -type f -name '*.o' | head -1)
[ -n "$conv" ] || why="$why no conversion in the cache;"
if [ -n "$conv" ]; then
	# a conversion older than rof2elf (and newer than the ROF): made again
	touch -d '2000-01-01' "$conv"
	touch -d '2001-01-01' tools/rof2elf
	tools/m68k-os9-l68 cc.r -o=m2 -n=m2 > m2.log 2>&1 || why="$why linking m2 failed: $(head -1 m2.log);"
	[ "$(date -r "$conv" +%Y)" -gt 2001 ] || why="$why the conversion older than rof2elf was used;"
	# newer than both: used
	touch -d '2000-01-01' tools/rof2elf
	touch -d '2002-01-01' "$conv"
	tools/m68k-os9-l68 cc.r -o=m3 -n=m3 > m3.log 2>&1 || why="$why linking m3 failed: $(head -1 m3.log);"
	[ "$(date -r "$conv" +%Y)" = 2002 ] || why="$why the conversion newer than both was made again;"
fi

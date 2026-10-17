# m68k-os9-xcc under its short name xcc, in a directory with another r68 (a stand-in for
# Microware's: Microware's options only, a ROF out) and our l68 as l68: xcc runs that r68 with
# r68's own options only (-obj= goes to ours through M68K_OS9_ELF), converts the ROF it writes,
# and links through l68 (-dialect=c32; -a for the jump table unless -j). The GNU tools through
# -gnu-prefix=, with PATH holding none of our tools.
# fixed: 18afa06 2026-10-16 Front ends: finding the tools; xcc links through l68
r68=$(command -v m68k-os9-r68)
gnu=$(dirname "$(command -v ${B}gcc)")
mkdir mixed
for p in m68k-os9-xcc:xcc m68k-os9-l68:l68 rof2elf:rof2elf elf2rof:elf2rof elf2mod:elf2mod; do
	cp "$(command -v "${p%%:*}")" "mixed/${p#*:}"
done
printf '#!/bin/sh\nfor a; do case $a in -obj*|-dialect*) echo "not an r68 option: $a" >&2; exit 2;; esac; done\necho "$*" >> "%s/r68.log"\nexec "%s" "$@" -obj=rof\n' \
	"$PWD" "$r68" > mixed/r68
chmod +x mixed/r68
src root <<'EOF'
 psect root,$101,$8000,1,0,entry
entry bsr callme
 rts
 ends
EOF
cat > part.c <<'EOF'
int twice(x) int x; { return x + x; }
int callme() { return twice(21); }
EOF
PATH=$PWD/mixed:/usr/bin:/bin mixed/xcc -q -gnu-prefix="$gnu/${B%-}" -r root.a > root.log 2>&1 || why="$why xcc -r root.a failed: $(head -1 root.log);"
grep -q -- "root.a -o=" r68.log || why="$why the stand-in r68 didn't run;"
grep -q -- "-obj" r68.log && why="$why r68 given -obj;"
od -An -c -N 4 root.r | grep -q "E   L   F" || why="$why root.r isn't ELF;"
PATH=$PWD/mixed:/usr/bin:/bin GRPUSER=0.0 mixed/xcc -q -bp -s -gnu-prefix="$gnu/${B%-}" -cs=root.r -nl part.c \
	-f=mod > mod.log 2>&1
echo $? > mod.status
linked mod
grep -q "/l68 .*-a -dialect=c32" mod.log || why="$why no l68 command with -a -dialect=c32 ($(grep l68 mod.log | head -1));"
has_bytes mod "4e75"

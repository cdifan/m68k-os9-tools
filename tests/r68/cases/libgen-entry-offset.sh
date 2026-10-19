# m68k-os9-libgen -c -obj=ucc of a ROF whose psect has an entry point that isn't at offset 0: the
# header's entry and trap offsets are both skipped. Before, a non-zero entry offset left the trap
# offset unread, so the remote sizes, the debug size, the psect name and the definitions were
# read four bytes off (the member's name and symbols came out wrong).
# fixed: 939d7e0 2026-10-19 m68k-os9-libgen: the entry and trap offsets read unconditionally
src em <<'EOF'
 psect emember,$101,$8000,1,0,go
 nop
go: rts
fz: rts
 ends
EOF
m68k-os9-r68 em.a -q -obj=rof -o=em.r > em.log 2>&1 || why="$why r68 failed: $(head -1 em.log);"
m68k-os9-libgen -c -obj=ucc -o=em.l em.r > lg.log 2>&1 || why="$why libgen -c failed: $(head -1 lg.log);"
m68k-os9-libgen -lu em.l > lu.txt 2>&1 || why="$why libgen -lu failed: $(head -1 lu.txt);"
grep -q -w emember lu.txt || why="$why psect name not listed: $(tr '\n' ' ' < lu.txt | cut -c1-80);"
m68k-os9-libgen -ln em.l > ln.txt 2>&1 || why="$why libgen -ln failed: $(head -1 ln.txt);"
for n in go fz; do grep -q -w "$n" ln.txt || why="$why symbol $n not listed: $(tr '\n' ' ' < ln.txt | cut -c1-80);"; done

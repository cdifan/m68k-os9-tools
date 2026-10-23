# '@' in global and external names (legal in r68): the object has the names with '@', as the
# Microware assemblers write them and rof2elf reads them, so objects from either link together
# (r68togas writes '@' as _at_, which gas needs, and m68k-os9-r68 gives the names back). A name
# with _at_ of its own in a source without '@' names keeps it.
# fixed: 90e8f01 2026-10-23 m68k-os9-r68: names with @ given back (global and undefined symbols)
src ata <<'EOF'
 psect ata,0,0,0,0,0
Lab@1: bsr Ext@2
 rts
 ends
EOF
src atb <<'EOF'
 psect atb,0,0,0,0,0
Ext@2: moveq #1,d0
 rts
 ends
EOF
src plain <<'EOF'
 psect plain,0,0,0,0,0
Keep_at_it: rts
 ends
EOF
for s in ata atb plain; do
	m68k-os9-r68 $s.a -q -o=$s.o > $s.log 2>&1 || why="$why $s: assembling failed: $(head -1 $s.log);"
done
${B}nm ata.o > ata.nm
grep -q ' T Lab@1$' ata.nm || why="$why no global Lab@1: $(tr '\n' ' ' < ata.nm);"
grep -q ' U Ext@2$' ata.nm || why="$why no external Ext@2;"
grep -q '_at_' ata.nm && why="$why _at_ left in ata.o;"
${B}nm atb.o | grep -q ' T Ext@2$' || why="$why no global Ext@2 in atb.o;"
${B}nm plain.o | grep -q ' T Keep_at_it$' || why="$why Keep_at_it renamed;"
# the two objects link: the reference resolved
${B}ld -r -o ab.o ata.o atb.o > ld.log 2>&1 || why="$why ld -r failed: $(head -1 ld.log);"
${B}nm ab.o | grep -q ' U ' && why="$why ab.o has undefined symbols;"
if [ -n "${R68:-}" ]; then
	# Microware's object of atb, read by rof2elf, links with ours of ata
	crlf atb.a "$mw/atb.a"
	r68exe atb
	rof2elf -o atbmw.o "$mw/atb.r" > rof.log 2>&1 || why="$why rof2elf failed: $(head -1 rof.log);"
	${B}nm atbmw.o | grep -q ' T Ext@2$' || why="$why rof2elf's object has no Ext@2;"
	${B}ld -r -o abmw.o ata.o atbmw.o > ldmw.log 2>&1 || why="$why ld -r with r68's object failed: $(head -1 ldmw.log);"
	${B}nm abmw.o | grep -q ' U ' && why="$why abmw.o has undefined symbols;"
fi

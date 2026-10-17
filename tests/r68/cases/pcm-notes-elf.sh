# A table of externals less a code label of the source (dc.w n,Ext-Label) assembled into an ELF
# object, then converted to ROF by elf2rof: written as the Microware assemblers write it, -Label
# in the words and a negated reference to the psect's code (the .r68pcm notes stay in the ELF
# object for that); and the notes change nothing in a link.
# Before, m68k-os9-r68 removed the notes from ELF objects: elf2rof wrote the words PC-relative
# (the field's distance from the label, 0002, 0006), with fewer references.
# fixed: 2026-10-17 m68k-os9-r68: .r68pcm notes kept in ELF objects
src tab <<'EOF'
 psect tab_p,0,0,0,0,0
 nop
 nop
Tab: dc.w 2,ext_a-Tab
 dc.w 5,ext_b-Tab
 dc.w 0
 ends
EOF
src exts <<'EOF'
 psect exts_p,0,0,0,0,0
ext_a: dc.w $FA0A
ext_b: dc.w $FA0B
 ends
EOF
src root <<'EOF'
 psect root,$101,$8000,1,0,entry
entry lea Tab(pc),a0
 rts
 ends
EOF
for f in tab exts root; do asm $f; done
elf2rof -o tab.rof tab.o > rof.log 2>&1 || why="$why elf2rof failed: $(head -1 rof.log);"
has_bytes tab.rof "4e71 4e71 0002 fffc 0005 fffc 0000"
[ "$(filehex tab.rof | grep -o 0074 | wc -l)" -ge 2 ] || why="$why tab.rof lacks the negated code references;"
# the same link with the notes removed
${B}objcopy --remove-section=.r68pcm --remove-section=.rela.r68pcm tab.o tab-bare.o
link mod root.o tab.o exts.o -n=mod
linked mod
link modb root.o tab-bare.o exts.o -n=mod
linked modb
cmp -s mod modb || why="$why the notes change the module;"

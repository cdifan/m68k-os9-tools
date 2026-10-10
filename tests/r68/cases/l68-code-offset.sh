# m68k-os9-l68: an absolute reference from code to a code label (dc.w Init in a driver's entry
# table) holds the label's module offset, as l68 writes it, without a jump table.
# fixed: 19e772c 2026-10-10 m68k-os9-l68, elf2mod: equate-only library members give way; code label offsets
src drv <<'EOF'
 psect drv,$e01,$8000,1,0,Entry
Entry dc.w Init,Term
Init moveq #1,d0
 rts
Term moveq #2,d0
 rts
 ends
EOF
asm drv
link drv drv.o -n=drv
linked drv
p=$(filehex drv | grep -o -b 70014e75 | head -1 | cut -d: -f1)
if [ -n "$p" ]; then has_bytes drv "$(printf '%04x%04x' $((p / 2)) $((p / 2 + 4)))70014e75"
else why="$why no Init in drv;"; fi

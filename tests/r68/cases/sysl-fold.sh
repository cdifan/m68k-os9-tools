# A sum of one external and the source's own constants (V_Flag+OFFS): a relocation takes it
# (a source may define its own V_Flag). It once guarded the folding of sys.l's equates, gone
# since (no library's equates are folded in: -sysl= is no longer an option).
# fixed: 8266699 2026-10-09 r68togas: no sys.l folding for a sum with the source's own constants; mnemonic macro arguments
src fold <<'EOF'
 psect fold,0,0,0,0,0
OFFS equ 4
 move.b V_Flag+OFFS(a2),d0
 ends
EOF
asm fold
has_reloc fold.o "V_Flag+0x00000004"

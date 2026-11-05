# m68k-os9-l68 -a: a short branch (bsr.s) to another object out of its reach in a link that needs
# a jump table fails with elf2mod's message about it; before, the link failed showing only the
# far calls' "relocation truncated" messages, which the jump table resolves. (Ultra C's l68
# refuses such a branch too: "operand size error".)
# fixed: c917272 2026-11-05 os9link: a link needing a jump table shows elf2mod's error
src ja <<'EOF'
 psect ja,$101,$8000,1,0,e
e bsr X
 bsr.s Y
 rts
 ends
EOF
{ echo ' psect jf,0,0,0,0,0'; for i in $(seq 10000); do echo ' dc.l $4e714e71'; done; echo ' ends'; } > jf.a
src jb <<'EOF'
 psect jb,0,0,0,0,0
X: rts
Y: rts
 ends
EOF
for s in ja jf jb; do asm $s; done
link m ja.o jf.o jb.o -a -n=m
failed_with m "R_68K_PC8"
failed_with m "doesn't fit: Y"

# An OS-9 absolute path of the machine a source was written on (use /dd/defs/oskdefs.d,
# /h0/defs/...) that isn't on this one: its file name in the -u directories, as if they were
# /dd/defs.
# fixed: a1941e2 2026-10-11 r68togas: OS-9 absolute use paths in the -u directories
mkdir -p defs
cat > defs/x.d <<'X'
X equ 7
X
cat > t.a <<'X'
 psect t,0,0,0,0,0
 use /dd/defs/x.d
 dc.w X
 ends
X
asm t -u=defs
has_bytes t.o "0007"

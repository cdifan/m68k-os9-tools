# OS-9's multi-dot path components in use (use .../defs/x.d: the parent's parent; .... one
# more up), as OS-9's own r68 got them from the OS-9 path syntax (Ultra C's r68.exe refuses
# them: "can't open file").
# fixed: 0ef2610 2026-10-11 r68togas: OS-9 multi-dot paths in use
mkdir -p a/b/c defs
cat > defs/x.d <<'EOF'
X equ 7
EOF
cat > y.d <<'EOF'
Y equ 9
EOF
cat > a/b/c/t.a <<'EOF'
 psect t,0,0,0,0,0
 use .../../defs/x.d
 use ..../y.d
 dc.w X,Y
 ends
EOF
cd a/b/c
asm t
has_bytes t.o "0007 0009"

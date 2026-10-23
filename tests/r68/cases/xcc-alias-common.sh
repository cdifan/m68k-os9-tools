# m68k-os9-xcc (C 3.2 mode) compiles with -fno-strict-aliasing, as C 3.2's compiler assumes nothing
# from pointer types: a value read through a pointer of another type after a store is read again,
# not taken from before the store. And without -fcommon: an uninitialized global is defined, not
# common, so two files defining it clash at the link, as with C 3.2's l68 ("duplicate symbol names").
# fixed: ef2add5 2026-10-23 m68k-os9-xcc: -fno-strict-aliasing in C 3.2 mode; -fcommon not needed
cat > al.c <<'EOF'
int get(i, f) int *i; short *f; { *i = 1; *f = 2; return *i; }
EOF
m68k-os9-xcc -q -bp -r al.c > al.txt 2>&1 || why="$why xcc -r al.c failed: $(head -1 al.txt);"
grep -q -- "-fno-strict-aliasing" al.txt || why="$why no -fno-strict-aliasing;"
# with strict aliasing GCC returns the constant 1 (moveq #1,d0 at the end); without, it reads *i
${B}objdump -d al.r > al.dis 2>&1
grep -q -E 'movel %a[0-9]@,%d0' al.dis || why="$why get() doesn't read *i back: $(grep -c . al.dis) lines;"
cat > cm.c <<'EOF'
int shared;
int use() { return shared; }
EOF
m68k-os9-xcc -q -bp -r cm.c > cm.txt 2>&1 || why="$why xcc -r cm.c failed: $(head -1 cm.txt);"
grep -q -- "-fcommon" cm.txt && why="$why -fcommon given;"
${B}nm cm.r | grep -q -E ' [BbDd] shared$' || why="$why shared isn't defined: $(${B}nm cm.r | grep shared);"

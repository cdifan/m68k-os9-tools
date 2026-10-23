# m68k-os9-xcc (C 3.2 mode) compiles with -mos9regfunc: register on a function declared in a block
# (register char *p, *find();), which C 3.2's compiler ignores, is accepted, and the call is an
# ordinary call of the external function; before, GCC refused the declaration.
cat > rf.c <<'EOF'
char *first(s) char *s; { register char *p, *find(); p = find(s, 'x'); return p; }
EOF
m68k-os9-xcc -q -bp -r rf.c > rf.txt 2>&1 || why="$why xcc -r rf.c failed: $(head -1 rf.txt);"
grep -q -- "-mos9regfunc" rf.txt || why="$why no -mos9regfunc;"
${B}nm rf.r > rf.nm 2>&1
grep -q -E ' U find$' rf.nm || why="$why find isn't an undefined symbol: $(tr '\n' ' ' < rf.nm);"

# The ELF conversion of a ROF goes to the cache by default (save=cache): nothing is written into
# the ROF's directory, the conversion lands below the cache's root (M68K_OS9_ELF's cache= or
# -elf-cache=); save=beside still writes beside it. Before, the default was beside.
# fixed: 06b45ff 2026-10-23 os9conv: converted ROF files go to the cache by default (save=cache)
src dc <<'EOF'
 psect dc,$101,$8000,1,0,entry
entry rts
 ends
EOF
asm dc -obj=rof
mkdir -p rofs
mv dc.o rofs/dc.r
before=$(ls rofs | tr '\n' ' ')
# the default, with only the cache's root given
M68K_OS9_ELF=cache=$PWD/c1 m68k-os9-l68 rofs/dc.r -o=m1 -n=m1 > m1.log 2>&1 || why="$why default: linking failed: $(head -1 m1.log);"
[ "$(ls rofs | tr '\n' ' ')" = "$before" ] || why="$why default: written into the ROF's directory: $(ls rofs | tr '\n' ' ');"
[ -n "$(find c1 -type f -name 'dc.o' 2>/dev/null)" ] || why="$why default: no conversion in the cache;"
# the option instead of the variable
M68K_OS9_ELF= m68k-os9-l68 -elf-cache=$PWD/c2 rofs/dc.r -o=m2 -n=m2 > m2.log 2>&1 || why="$why -elf-cache=: linking failed: $(head -1 m2.log);"
[ -n "$(find c2 -type f -name 'dc.o' 2>/dev/null)" ] || why="$why -elf-cache=: no conversion in the cache;"
[ "$(ls rofs | tr '\n' ' ')" = "$before" ] || why="$why -elf-cache=: written into the ROF's directory;"
# beside on request
M68K_OS9_ELF=save=beside m68k-os9-l68 rofs/dc.r -o=m3 -n=m3 > m3.log 2>&1 || why="$why save=beside: linking failed: $(head -1 m3.log);"
[ -f rofs/dc.o ] || why="$why save=beside: no rofs/dc.o;"

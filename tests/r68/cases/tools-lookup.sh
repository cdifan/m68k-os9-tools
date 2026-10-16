# Finding the GNU tools (m68k-os9-xcc.md, "Finding the tools"), through m68k-os9-r68's as: the
# -gnu-prefix= forms (a directory of short names, a name prefix with or without its "-"),
# M68K_OS9_GNU_PREFIX, m68k-elfos9-as beside the front end, the short name beside it only when
# it runs under its short name; and m68k-os9-gcc run as gcc doesn't take itself for GCC.
# Stand-ins log their name and run the real tool.
r68=$(command -v m68k-os9-r68)
gcc_real=$(command -v ${B}gcc)
stub() {	# PATH: a stand-in for as that logs its name to $PWD/log and runs the real as
	mkdir -p "$(dirname "$1")"
	printf '#!/bin/sh\necho %s >> "%s/log"\nexec "%s" "$@"\n' "$(basename "$1")" "$PWD" \
		"$(command -v ${B}as)" > "$1"
	chmod +x "$1"
}
others() {	# DIR PREFIX: the other GNU tools r68 runs, as PREFIX<tool> in DIR
	for o in readelf nm objcopy objdump; do ln -s "$(command -v ${B}$o)" "$1/$2$o"; done
}
src t <<'EOF'
 psect t,0,0,0,0,0
 nop
 ends
EOF
used() {	# WHAT EXPECTED: the stand-ins that ran (log), as expected
	local got=$(cat log 2> /dev/null | tr '\n' ' ')
	[ "$got" = "$2" ] || why="$why $1: ran \"$got\", not \"$2\";"
	rm -f log
}
stub short/as
others short ""
stub pfx/my-as
others pfx my-
"$r68" t.a -q -gnu-prefix="$PWD/short" -o=t1.o; used "-gnu-prefix=DIR" "as "
"$r68" t.a -q -gnu-prefix="$PWD/short/" -o=t2.o; used "-gnu-prefix=DIR/" "as "
"$r68" t.a -q -gnu-prefix="$PWD/pfx/my" -o=t3.o; used "-gnu-prefix=PREFIX" "my-as "
"$r68" t.a -q -gnu-prefix="$PWD/pfx/my-" -o=t4.o; used "-gnu-prefix=PREFIX-" "my-as "
M68K_OS9_GNU_PREFIX="$PWD/short" "$r68" t.a -q -o=t5.o; used "M68K_OS9_GNU_PREFIX" "as "
M68K_OS9_GNU_PREFIX="$PWD/short" "$r68" t.a -q -gnu-prefix="$PWD/pfx/my" -o=t6.o
used "-gnu-prefix= over M68K_OS9_GNU_PREFIX" "my-as "
# beside the front end: the full name; the short name only under the front end's short name
mkdir full own
cp "$r68" full/m68k-os9-r68
stub full/m68k-elfos9-as
full/m68k-os9-r68 t.a -q -o=t7.o; used "m68k-elfos9-as beside" "m68k-elfos9-as "
cp "$r68" own/m68k-os9-r68
cp "$r68" own/r68
stub own/as
own/m68k-os9-r68 t.a -q -o=t8.o; used "as beside m68k-os9-r68" ""
own/r68 t.a -q -o=t9.o; used "as beside r68" "as "
for n in 1 2 3 4 5 6 7 8 9; do [ -s t$n.o ] || why="$why t$n.o not made;"; done
# m68k-os9-gcc as gcc, alone in its directory: GCC from PATH, not itself
mkdir g
cp "$(command -v m68k-os9-gcc)" g/gcc
v=$(timeout 20 g/gcc --version 2>&1 | head -1)
[ "$v" = "$("$gcc_real" --version | head -1)" ] || why="$why gcc as m68k-os9-gcc: \"$v\";"

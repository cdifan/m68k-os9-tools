#!/bin/bash
# run.sh [CASE...]: the gastor68 test cases (gastor68.md, "Tests"): each cases/*.s, gas
# source as GCC writes it, is translated by gastor68 and checked against its "| expect:" lines
# (each a text that a line of the translation contains, spaces collapsed); then the translation
# assembled by m68k-os9-r68 (and by Ultra C's r68.exe when R68 is set, converted by rof2elf)
# must give the code, data and relocations that gas gives for the source. "| cpu: 68020"
# makes a case 68020 code with the 68881 (-m2 for the r68s); "| mode: ucc" translates it with
# gastor68 -mode=ucc and assembles it with -b (as Ultra C's xcc does: it sizes the unsized
# branches); "| fixed:" names the commit that fixed what a case tests (as in tests/r68).
# Settings: TOOLS (a directory put first in PATH: m68k-os9-r68, r68togas, rof2elf), GASTOR68
# (default gastor68 from PATH), TPREFIX (the binutils' prefix, m68k-elfos9-), R68 (Ultra C's
# r68.exe, WSL) and R68WORK (a directory on a Windows drive for r68.exe's files, /mnt/c/...;
# default the temporary directory, which r68.exe can't use). Everything is written in temporary
# directories. The result is one line per case, PASS or FAIL (with the reasons), then the
# counts; the exit status is 0 when no case failed.
here=$(cd "$(dirname "$0")" && pwd)
[ -n "${TOOLS:-}" ] && export PATH="$TOOLS:$PATH"
G=${GASTOR68:-gastor68}
B=${TPREFIX:-m68k-elfos9-}
t=$(mktemp -d)
mw=$t/mw
[ -n "${R68WORK:-}" ] && mw=$(mktemp -d "$R68WORK/g68test.XXXXXX")
mkdir -p "$mw"
trap 'rm -rf "$t"; [ "$mw" = "$t/mw" ] || rm -rf "$mw"' EXIT
export M68K_OS9_ELF=save=cache,cache=$t/cache
pass=0 fail=0 failed=

header() { tr -d '\r' < "$2" | sed -n "s/^| $1: *//p"; }
collapse() { tr -d '\r' | tr -s ' \t' '  ' | sed 's/^ //; s/ $//'; }
# the code and data of OBJ, and its relocations (offset, type, value), into OUT.*
contents() {
	local s
	for s in .text .data .bss; do
		: > "$2$s"
		${B}objcopy -O binary -j $s "$1" "$2$s.bin" 2> /dev/null && od -An -tx1 -v "$2$s.bin" | tr -d ' \n' > "$2$s"
		echo >> "$2$s"
		[ $s = .text ] || ${B}objdump -h "$1" | awk -v s=$s '$2 == s {print "size", $3}' >> "$2$s"
	done
	# the code's and data's (not r68togas's .r68ord records, for elf2rof)
	${B}objdump -r -j .text -j .data "$1" 2> /dev/null | grep '^[0-9a-f]\{8\} ' > "$2.rel"
}
# A's contents against B's (B's .text may have up to 3 bytes of padding more: nop or trapf)
same() {
	local s d=
	for s in .text .data .bss; do
		if [ $s = .text ]; then
			local a=$(head -1 "$1$s") b=$(head -1 "$2$s")
			case ${b#"$a"} in ""|4e71|51fc|00|0000|004e71) ;; *) d="$d $s;" ;; esac
		else
			cmp -s "$1$s" "$2$s" || d="$d $s;"
		fi
	done
	cmp -s "$1.rel" "$2.rel" || d="$d relocations;"
	echo "$d"
}

run_case() {	# SOURCE
	local src=$1 name=$(basename "$1" .s) why= e
	local m=0 cpu="-m68000"
	[ "$(header cpu "$src")" = 68020 ] && { m=2; cpu="-m68020 -m68881"; }
	local mode= b=; [ -n "$(header mode "$src")" ] && mode=-mode=$(header mode "$src")
	[ "$mode" = -mode=ucc ] && b=-b
	if ! (cd "$t" && "$G" $mode -o "$name.a" "$src") > "$t/$name.g.log" 2>&1; then
		why=" gastor68 failed: $(head -1 "$t/$name.g.log");"
	else
		while read -r e; do [ -z "$e" ] && continue
			collapse < "$t/$name.a" | grep -q -F -- "$(echo "$e" | collapse)" || why="$why no line \"$e\";"
		done <<< "$(header expect "$src")"
		${B}as $cpu --pcrel -o "$t/$name.gas.o" "$src" > "$t/$name.as.log" 2>&1 || why="$why gas failed;"
		contents "$t/$name.gas.o" "$t/$name.gas"
		if (cd "$t" && m68k-os9-r68 "$name.a" -q -m$m $b -o="$name.o") > "$t/$name.log" 2>&1 < /dev/null; then
			contents "$t/$name.o" "$t/$name.our"
			e=$(same "$t/$name.gas" "$t/$name.our")
			[ -n "$e" ] && why="$why m68k-os9-r68 differs from gas:$e"
		else
			why="$why m68k-os9-r68 failed: $(head -1 "$t/$name.log");"
		fi
		if [ -n "${R68:-}" ]; then
			sed 's/\r*$/\r/' "$t/$name.a" > "$mw/$name.a"
			local o=; [ $m = 2 ] && o=-m2
			(cd "$mw" && "$R68" "$name.a" -q $o $b -o="$name.r") > "$t/$name.r68.log" 2>&1 < /dev/null
			if [ -s "$mw/$name.r" ] && rof2elf -o "$t/$name.mw.o" "$mw/$name.r" > /dev/null 2>&1; then
				contents "$t/$name.mw.o" "$t/$name.mw"
				e=$(same "$t/$name.gas" "$t/$name.mw")
				[ -n "$e" ] && why="$why r68 differs from gas:$e"
			else
				why="$why r68 or rof2elf failed;"
			fi
		fi
	fi
	if [ -z "$why" ]; then pass=$((pass + 1)); echo "PASS: $name"
	else fail=$((fail + 1)); failed="$failed $name"; echo "FAIL: $name:$why"; fi
}

cases=("$@")
shopt -s nullglob
[ ${#cases[@]} = 0 ] && cases=("$here"/cases/*.s)
for c in "${cases[@]}"; do
	[ -f "$c" ] || c=$here/cases/$c
	[ -f "$c" ] || { echo "run.sh: no case $c" >&2; continue; }
	run_case "$(cd "$(dirname "$c")" && pwd)/$(basename "$c")"
done
echo "$pass passed, $fail failed${failed:+:$failed}"
[ $fail = 0 ]

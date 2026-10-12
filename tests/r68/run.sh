#!/bin/bash
# run.sh [CASE...]: the r68 test cases (README.md): each cases/*.a assembled by m68k-os9-r68 and
# checked against its "* expect" lines, each cases/*.sh run with the helpers below. Settings:
# TOOLS (a directory put first in PATH: m68k-os9-r68, r68togas, rof2elf, elf2rof, m68k-os9-l68,
# elf2mod; the binutils are taken from PATH), TPREFIX (the binutils' prefix, m68k-elfos9-), R68
# (Ultra C's r68.exe: each assembler case without -dialect=c32 or an expected error is also
# assembled by it and its code and data compared with ours; the cases/*.sh that need it run
# only then) and R68WORK (a directory on a Windows drive for r68.exe's files, /mnt/c/...;
# default the temporary directory). Everything is written in temporary directories.
here=$(cd "$(dirname "$0")" && pwd)
[ -n "${TOOLS:-}" ] && export PATH="$TOOLS:$PATH"
B=${TPREFIX:-m68k-elfos9-}
t=$(mktemp -d)
mw=$t/mw
[ -n "${R68WORK:-}" ] && mw=$(mktemp -d "$R68WORK/r68test.XXXXXX")
trap 'rm -rf "$t"; [ "$mw" = "$t/mw" ] || rm -rf "$mw"' EXIT
export M68K_OS9_ELF=save=cache,cache=$t/cache
pass=0 fail=0 known=0 skip=0 failed=

hex() { tr -d ' \t\r\n' | tr 'A-F' 'a-f'; }
section_hex() {	# OBJ SECTION: its bytes in hex
	: > "$t/sec.bin"
	${B}objcopy -O binary -j "$2" "$1" "$t/sec.bin" 2> /dev/null
	od -An -tx1 -v "$t/sec.bin" | hex
}
header() { tr -d '\r' < "$2" | sed -n "s/^\* $1: *//p"; }
expect() { header "expect $1" "$2"; }
# r68.exe NAME OPTS...: NAME.a in $mw assembled by Ultra C's r68 into NAME.r there
r68exe() { local n=$1; shift; (cd "$mw" && "$R68" "$n.a" -q "$@" -o="$n.r") > "$mw/$n.log" 2>&1 < /dev/null; }
# crlf SRC DEST: r68.exe needs CR LF line ends (rept)
crlf() { sed 's/\r*$/\r/' "$1" > "$2"; }

asm_case() {	# SOURCE
	local src=$1 name=$(basename "$1" .a) why=
	local opts=$(header options "$src")
	local sysl=-sysl=none
	grep -q -i -- '-sysl=' <<< "$opts" && sysl=
	local o=$t/$name.o
	(cd "$t" && m68k-os9-r68 "$src" -q $sysl $opts -o="$o") > "$t/$name.log" 2>&1 < /dev/null
	local status=$?
	local err=$(expect error "$src")
	if ! grep -q '^\* expect [a-z]*:' "$src"; then
		why=" no \"* expect\" line: nothing checked;"
	elif [ -n "$err" ]; then
		[ $status != 0 ] && grep -q -F -- "$err" "$t/$name.log" || why=" no error \"$err\";"
	elif [ $status != 0 ] || [ ! -s "$o" ]; then
		why=" assembling failed: $(grep -v '^Assembler messages' "$t/$name.log" | head -1);"
	else
		local e a s v ty nm
		while read -r e; do [ -z "$e" ] && continue
			a=$(section_hex "$o" .text); [ "$a" = "$(echo "$e" | hex)" ] || why="$why .text $a, not $(echo "$e" | hex);"
		done <<< "$(expect text "$src")"
		while read -r e; do [ -z "$e" ] && continue
			a=$(section_hex "$o" .data); [ "$a" = "$(echo "$e" | hex)" ] || why="$why .data $a, not $(echo "$e" | hex);"
		done <<< "$(expect data "$src")"
		while read -r s v; do [ -z "$s" ] && continue
			a=$(${B}objdump -h "$o" | awk -v s="$s" '$2 == s {print $3}')
			[ $((16#${a:-0})) = $((16#$v)) ] || why="$why $s size ${a:-none}, not $v;"
		done <<< "$(expect size "$src")"
		while read -r s v; do [ -z "$s" ] && continue
			a=$(${B}objdump -h "$o" | awk -v s="$s" '$2 == s {print $7}')
			[ "$a" = "2**$v" ] || why="$why $s alignment ${a:-none}, not 2**$v;"
		done <<< "$(expect align "$src")"
		while read -r ty nm v; do [ -z "$ty" ] && continue
			${B}nm -g "$o" | grep -q -x -F "$v $ty $nm" || why="$why no symbol \"$v $ty $nm\";"
		done <<< "$(expect sym "$src")"
		while read -r nm; do [ -z "$nm" ] && continue
			${B}nm "$o" | awk '{print $NF}' | grep -q -x -F "$nm" && why="$why symbol $nm there;"
		done <<< "$(expect nosym "$src")"
		while read -r e; do [ -z "$e" ] && continue
			${B}objdump -r "$o" | grep -q -F -- "$e" || why="$why no relocation \"$e\";"
		done <<< "$(expect reloc "$src")"
		if [ -n "${R68:-}" ] && ! grep -q -i 'dialect=c32' <<< "$opts" && [ -z "$(header 'not r68' "$src")" ]; then
			crlf "$src" "$mw/$name.a"
			r68exe "$name" $opts
			if [ -s "$mw/$name.r" ] && rof2elf -o "$t/$name.mw.o" "$mw/$name.r" > /dev/null 2>&1; then
				[ "$(section_hex "$t/$name.mw.o" .text)" = "$(section_hex "$o" .text)" ] || why="$why .text differs from r68's;"
				[ "$(section_hex "$t/$name.mw.o" .data)" = "$(section_hex "$o" .data)" ] || why="$why .data differs from r68's;"
			else
				why="$why r68 or rof2elf failed;"
			fi
		fi
	fi
	result "$name" "$why" "$(header known "$src")"
}

result() {	# NAME WHY (empty: passed) [KNOWN: the case is a known failure, why]
	if [ "$2" = " skipped" ]; then skip=$((skip + 1)); echo "SKIP: $1"
	elif [ -n "$3" ]; then
		if [ -n "$2" ]; then known=$((known + 1)); echo "KNOWN: $1: $3"
		else fail=$((fail + 1)); failed="$failed $1"; echo "FAIL: $1: passes, but is marked known ($3)"; fi
	elif [ -z "$2" ]; then pass=$((pass + 1)); echo "PASS: $1"
	else fail=$((fail + 1)); failed="$failed $1"; echo "FAIL: $1:$2"; fi
}

# Helpers for the cases/*.sh (run in a directory of their own; a failed check adds to $why):
#   src NAME <<'EOF'        a source NAME.a from the here-document
#   asm NAME [OPTS...]      NAME.a assembled into NAME.o (m68k-os9-r68 -sysl=none unless given)
#   lib LIB OBJ...          an ar archive LIB of the objects
#   link OUT ARGS...        m68k-os9-l68 ARGS -o=OUT (its messages in OUT.log)
#   has_bytes FILE HEX      FILE contains the bytes HEX
#   lacks_bytes FILE HEX    FILE doesn't
#   linked OUT              the link made OUT
#   failed_with OUT TEXT    the link failed with TEXT among its messages
#   has_sym OBJ "V T NAME"  OBJ has the global symbol (a line of nm -g)
#   has_reloc OBJ TEXT      a line of OBJ's relocations (objdump -r) contains TEXT
#   needs_r68               the rest only with R68 (else the case is skipped)
#   mwasm NAME [OPTS...]    NAME.a assembled by Ultra C's r68 into NAME.r
src() { cat > "$1.a"; }
asm() {
	local n=$1 sysl=-sysl=none; shift
	case " $* " in *" -sysl="*) sysl= ;; esac
	m68k-os9-r68 "$n.a" -q $sysl "$@" -o="$n.o" > "$n.log" 2>&1 < /dev/null || why="$why assembling $n failed: $(head -1 "$n.log");"
}
lib() { local l=$1; shift; rm -f "$l"; ${B}ar rc "$l" "$@"; }
link() { local o=$1; shift; GRPUSER=0.0 m68k-os9-l68 "$@" -o="$o" > "$o.log" 2>&1 < /dev/null; echo $? > "$o.status"; }
filehex() { od -An -tx1 -v "$1" | hex; }
has_bytes() { [ -f "$1" ] && filehex "$1" | grep -q "$(echo "$2" | hex)" || why="$why $1 lacks $2;"; }
lacks_bytes() { if [ -f "$1" ] && filehex "$1" | grep -q "$(echo "$2" | hex)"; then why="$why $1 has $2;"; fi; }
linked() { [ "$(cat "$1.status")" = 0 ] && [ -s "$1" ] || why="$why linking $1 failed: $(head -1 "$1.log");"; }
failed_with() { [ "$(cat "$1.status")" != 0 ] && grep -q -F -- "$2" "$1.log" || why="$why no error \"$2\" ($(head -1 "$1.log"));"; }
has_sym() { ${B}nm -g "$1" 2> /dev/null | grep -q -x -F "$2" || why="$why $1 has no symbol \"$2\";"; }
has_reloc() { ${B}objdump -r "$1" 2> /dev/null | grep -q -F -- "$2" || why="$why $1 has no relocation \"$2\";"; }
needs_r68() { [ -n "${R68:-}" ] || { echo " skipped" > "$t/$case/why"; exit 0; }; }
mwasm() { local n=$1; shift; crlf "$n.a" "$mw/$n.a"; r68exe "$n" "$@"; cp "$mw/$n.r" . 2> /dev/null || why="$why r68 failed on $n: $(head -3 "$mw/$n.log" | tail -1);"; }

sh_case() {	# SCRIPT
	case=$(basename "$1" .sh)
	mkdir -p "$t/$case"
	why=
	(cd "$t/$case" && { . "$1"; echo "$why" > "$t/$case/why"; }) > "$t/$case.out" 2>&1
	[ -f "$t/$case/why" ] || echo " the script failed: $(tail -1 "$t/$case.out")" > "$t/$case/why"
	result "$case" "$(cat "$t/$case/why")" "$(header known "$1")"
}

cases=("$@")
shopt -s nullglob
[ ${#cases[@]} = 0 ] && cases=("$here"/cases/*.a "$here"/cases/*.sh)
for c in "${cases[@]}"; do
	[ -f "$c" ] || c=$here/cases/$c
	[ -f "$c" ] || { echo "run.sh: no case $c" >&2; continue; }
	c=$(cd "$(dirname "$c")" && pwd)/$(basename "$c")
	case $c in *.a) asm_case "$c" ;; *.sh) sh_case "$c" ;; esac
done
echo "$pass passed, $fail failed${failed:+:$failed}, $known known failures, $skip skipped"
[ $fail = 0 ]

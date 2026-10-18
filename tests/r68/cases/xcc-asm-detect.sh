# m68k-os9-xcc's detection of a .a input's syntax (asmsyntax.c), rule by rule: which assembler
# xcc runs for it (-bp shows the commands): GCC's (-x assembler) for gas, m68k-os9-r68 for r68
# and for undecided files. A % register decides gas, a binary %1010 and a "%d0" in a string not;
# a known gas directive decides gas (also after a label), a dotted r68 name (.foo equ 1) not;
# GCC's #NO_APP/#APP and cpp line markers decide gas; a Microware directive (psect, use, nam,
# ...) decides r68; comment lines and plain instructions decide nothing; the first decisive line
# wins. Any line end (LF, CR LF, CR).
# fixed: f527463 2026-10-18 xcc -a writes r68 syntax (gastor68); -asm= and .a inputs of either syntax
which() {	# NAME: the assembler xcc ran for NAME.a (gas, r68 or none)
	m68k-os9-xcc -bp -r "$1.a" > "$1.bp" 2>&1
	if grep -q -- '-x assembler' "$1.bp"; then echo gas
	elif grep -q -- "r68 $1.a\|r68.* $1.a" "$1.bp"; then echo r68
	else echo none; fi
}
expect() {	# NAME SYNTAX TEXT...: NAME.a holds the lines TEXT, decided as SYNTAX
	local name=$1 syntax=$2; shift 2
	printf '%s\n' "$@" > "$name.a"
	local got=$(which "$name")
	[ "$got" = "$syntax" ] || why="$why $name: $got, not $syntax ($(head -1 "$name.bp"));"
}
expect pctreg gas ' move.l %d0,%d1'
expect pctsp gas 'lab: move.l (%sp)+,%a6'
expect pctbin r68 ' move.b #%1010,d0'
expect pctstr r68 ' dc.b "%d0",0'
expect gasdir gas ' .long 5'
expect gaslab gas 'lab: .long 5'
expect gascfi gas ' .cfi_startproc'
expect dotname r68 '.foo equ 1' ' dc.w .foo'
expect noapp gas '#NO_APP' ' move.l d0,d1'
expect app gas '#APP' ' move.l d0,d1'
expect marker gas '# 1 "x.c"' ' move.l d0,d1'
expect psect r68 ' psect x,0,0,0,0,0' ' rts' ' ends'
expect use r68 ' use <oskdefs.d>'
expect nam r68 ' nam x'
expect comment r68 '* .text and %d0 in a comment' '; .globl x' ' rts'
expect plain r68 ' move.l d0,d1' ' rts'
expect firstr68 r68 ' psect x,0,0,0,0,0' ' .long 5' ' ends'
expect firstgas gas ' .text' ' psect x,0,0,0,0,0'
expect late gas ' move.l d0,d1' ' nop' ' nop' ' .text'
# line ends: CR LF and CR (OS-9's), the deciding line not the first
printf ' rts\r\n .text\r\n' > crlf.a
[ "$(which crlf)" = gas ] || why="$why crlf: not gas;"
printf ' rts\r psect x,0,0,0,0,0\r ends\r' > cr.a
[ "$(which cr)" = r68 ] || why="$why cr: not r68;"
printf ' rts\r .long 5\r' > crgas.a
[ "$(which crgas)" = gas ] || why="$why crgas: not gas;"

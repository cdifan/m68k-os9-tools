# m68k-os9-xcc in Ultra C mode (-mode=ucc, or the name m68k-os9-ucc): the commands it runs (-h:
# shown, not run), as Ultra C's xcc runs its phases (measured with xcc -h -b): GCC with Ultra
# C's convention and predefined names (ANSI mode: printf, scanf, fopen and freopen called by the
# names Ultra C's compiler gives them; K&R mode: C 3.2's placement, which the library's printf
# takes), the MWOS tree's headers; l68 with acstart.r, the default
# libraries of the CPU's directories, -x=/-b=, -c for -a, -ol='s options, .l files whole; the
# module named after the one input file, else "output". A fake MWOS tree of empty files.
mkdir -p mw/SRC/DEFS mw/OS9/SRC/DEFS mw/OS9/68000/LIB mw/OS9/68020/LIB bin
for f in acstart.r cstart.r clib.l os_lib.l sys.l csl.l os_csl.l sys_clib.l; do : > mw/OS9/68000/LIB/$f; done
for f in clib.l os_lib.l; do : > mw/OS9/68020/LIB/$f; done
ln -s "$(command -v m68k-os9-xcc)" bin/m68k-os9-ucc
echo 'int main(void) { return 0; }' > hi.c
echo 'int two(void) { return 2; }' > two.c
M=$PWD/mw
cmds() {	# NAME ARGS: the commands of m68k-os9-ucc ARGS into NAME.txt, MWOS's path as M
	local n=$1; shift
	MWOS=$M bin/m68k-os9-ucc -h "$@" 2>&1 | sed "s|$M|M|g" > "$n.txt"
}
has() {	# NAME TEXT: a command of NAME has TEXT
	grep -q -F -- "$2" "$1.txt" || why="$why $1: no \"$2\";"
}
lacks() { if grep -q -F -- "$2" "$1.txt"; then why="$why $1: has \"$2\";"; fi; }

cmds def hi.c
has def "-mos9call=ucc -Dprintf=__ansi_printf -Dscanf=__ansi_scanf -Dfopen=__ansi_fopen -Dfreopen=__ansi_freopen -mbuiltin=os9call -std=gnu89 -w -mos9stkchk -m68000 -msoft-float -O2 -D_UCC -D_MAJOR_REV=2 -D_MINOR_REV=5 -D_SPACE_FACTOR=1 -D_TIME_FACTOR=1 -D_OSK -D_MPF68000 -D_MPF68K -D_FPF881 -D_BIG_END"
has def "-IM/SRC/DEFS -IM/OS9/SRC/DEFS"
has def "l68 -t=os9_68k M/OS9/68000/LIB/acstart.r -o=hi -f=orowoe -x=2 -b=2 -l=M/OS9/68000/LIB/clib.l -l=M/OS9/68000/LIB/os_lib.l -l=M/OS9/68000/LIB/sys.l"
lacks def "-mos9newline"
lacks def " -a "

cmds cpu -tp=68020 -g -r hi.c
has cpu "-m68020"
has cpu "-D_MPF68020"
lacks cpu "-mos9stkchk"
has cpu "-x=16 -b=16 -g -l=M/OS9/68020/LIB/clib.l -l=M/OS9/68020/LIB/os_lib.l -l=M/OS9/68000/LIB/sys.l"

cmds kr -bc hi.c
# K&R mode: the library's printf takes its first argument from d1, under its own name
has kr "-mos9call -mbuiltin"
lacks kr "__ansi_"
has kr "-DOSK"
has kr "-Dmc68000"
has kr "M/OS9/68000/LIB/cstart.r"
has kr "-l=M/OS9/68000/LIB/sys_clib.l -l=M/OS9/68000/LIB/clib.l -l=M/OS9/68000/LIB/os_lib.l -l=math.l -l=M/OS9/68000/LIB/sys.l"

cmds ansi -a=err -i -o=3 -s=5 hi.c
has ansi "-std=c89 -pedantic-errors"
has ansi "-Os"
has ansi "-D_SPACE_FACTOR=5"
has ansi "-o=hi -c -f=orowoe"
has ansi "-l=M/OS9/68000/LIB/csl.l -l=M/OS9/68000/LIB/os_csl.l"

cmds misc -k -ol=a -fd=data -l=my.l hi.c two.c x.l
lacks misc "acstart.r"
has misc "-O=data"
has misc "-l=my.l -a"
has misc "x.l"
lacks misc "-l=x.l"

cmds two hi.c two.c
has two " -o=output "

cmds opt -o hi.c
has opt " -O0 "

# -mode=ucc under the C 3.2 name, -mode=compat under the Ultra C name
MWOS=$M m68k-os9-xcc -mode=ucc -h hi.c > m1.txt 2>&1
grep -q -- "-mos9call=ucc" m1.txt || why="$why -mode=ucc: not Ultra C's mode;"
CLIB=$M/OS9/68000/LIB MWOS=$M bin/m68k-os9-ucc -mode=compat -h hi.c > m2.txt 2>&1
grep -q -- "-mos9call -" m2.txt || why="$why -mode=compat: not C 3.2's mode;"

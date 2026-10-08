# m68k-os9-tools

Tools for building OS-9/68000 (and CD-i) programs with GCC and the GNU binutils, and for working
with Microware's own objects, libraries and build files. They come with the OS-9 toolchain of
[cdifan/toolchaincdi](https://github.com/cdifan/toolchaincdi) (GCC, binutils and newlib for
`m68k-elfos9`), and grew out of a fork of Murachue's
[elf2mod](https://github.com/murachue/elf2mod), which is one of them.

## Front ends

Drop-ins for Microware's tools, so that existing makefiles build with GCC and the GNU tools: they
take Microware's options and find the user's own Microware headers and libraries as Microware's
tools do, converting them as needed. Microware's files are not included.

- [m68k-os9-xcc](m68k-os9-xcc.md): a drop-in for Microware C 3.2's compiler driver
  `xcc`/`cc`, and as `m68k-os9-ucc` for Ultra C's `xcc`.

      m68k-os9-xcc -r=RELS pp.c            # RELS/pp.r, an ELF object (-obj=rof for ROF)
      m68k-os9-xcc RELS/pp.r -f=pp -m=8    # links with cstart.r and clibn.l, math.l, sys.l

- [m68k-os9-l68](m68k-os9-l68.md): a drop-in for Microware's linker `l68`, for every
  module type.

      m68k-os9-l68 cstart.r prog.r -l=clib.l -l=sys.l -o=prog -g    # prog and prog.stb

- [m68k-os9-r68](m68k-os9-r68.md): a drop-in for Microware's assembler `r68`.

      m68k-os9-r68 drv.a -a=TARGET -o=drv.r     # an ELF object (-obj=rof: a ROF)

- [m68k-os9-libgen](m68k-os9-libgen.md): a drop-in for Ultra C's librarian `libgen`, writing
  `ar` archives, concatenated ROFs or `libgen`'s format, and its listings.

      m68k-os9-gcc -Os -o hello hello.c -Wm,-g     # hello and hello.stb

Each prints its options with `-?`. Under the short names `xcc`, `cc`, `ucc`, `l68`, `r68` and
`libgen` they run as well, and find each other (see "Finding the tools" in
[m68k-os9-xcc.md](m68k-os9-xcc.md)).

## Converters and translators

- [elf2mod](elf2mod.md): a linked ELF program into an OS-9 module, with the symbol module and the
  source debugger's file; Murachue's, heavily extended.
- [rof2elf](rof2elf.md): Microware ROF objects and libraries (also `libgen`'s) into ELF objects and
  `ar` archives.
- [elf2rof](elf2rof.md): ELF objects and archives into ROF objects and libraries, for Microware's
  linker.
- [r68togas](r68togas.md): r68 assembler sources into GNU `as` syntax, as `m68k-os9-r68` uses it.
- [gastor68](gastor68.md): GCC's assembler output into r68 syntax, as `m68k-os9-xcc -a` uses it.

## Build and install

- Prerequisites: the m68k-elfos9 BFD library (binutils built with `--enable-install-libbfd`), which
  only `elf2mod` needs; libiberty for the front ends (`libiberty-dev` on Debian and Ubuntu,
  `LIBIBERTY_CFLAGS` in the Makefile for its headers); otherwise a C compiler.
- Set `BFDPATH` in the Makefile if you are not on Linux on x86-64.
- `make && make install`; `DESTDIR=` sets the prefix. `make DESTDIR=/usr install` installs the
  programs in `/usr/bin`, with `m68k-os9-` links to the `m68k-elfos9-` binutils (`ar`, `as`, `ld`,
  ...) and `m68k-os9-ucc` to `m68k-os9-xcc`.
- Windows: the same Makefile builds them with MinGW-w64 (`EXEEXT=.exe`, `LN=cp`), natively or as a
  cross build.

## License

MIT; see the source files.

# r68togas

`r68togas` translates a Microware r68 assembler source (`.a`) into a source for the GNU
assembler (`gas`), so that r68 sources assemble into ELF objects and ROFs with the GNU tools and
give the bytes Microware's assemblers give. It translates syntax only; it neither assembles nor
links. [m68k-os9-r68](m68k-os9-r68.md) is the front end with r68's options that runs it, then
`gas`, and post-processes `gas`'s object (names `gas` can't write, symbols `gas` leaves out); `gas`
alone gives an object that is right only where none of that applies. `m68k-os9-r68 -obj=gas`
stops after the translation. The reverse, for GCC's output, is [gastor68](gastor68.md).

## Usage

    r68togas [-a <sym>[=<val>]]... [-m <n>] [-d ucc|c32|ucc95] [-b [-B] | -bt] [-r] [-y] [-s]
             [-x <ext>] [-u <dir>]... [-o <output>] <source>

| Option | Meaning |
|---|---|
| `-a <sym>[=<val>]` | define a symbol (1 without a value), as r68's `-a=`; written into the output as a `set` line |
| `-m <n>` | the CPU, as r68's `-m` (0 the 68000 ... 6 the 68060); written as an `.arch` directive; it also chooses the padding (see "Dialects") |
| `-b` | r68's `-b`: branches to the source's own labels sized by `gas`; `-B` also those to its global labels |
| `-r` | r68's `-r`: registers only with `%` (`%d0`); a plain `d0` is a symbol |
| `-u <dir>` | a directory to search for `use` files, as r68's `-u` |

`-b`, `-bt`, `-r` and `-y` are options of Ultra C's `r68` only; C 3.2's `r68` has none of them.
With `-a` and `-m` in the output, it needs no options: `m68k-os9-as <output>` assembles it, and
so does the GCC driver (`m68k-elfos9-gcc -c x.s`).

**No overwriting:** writing over an input file is an error, and so is writing over any other
existing file that `-o` doesn't name, `-s`'s outputs included. So the round trip `gastor68
foo.s` (giving `foo.a`), then `r68togas foo.a` (which would give `foo.s`, GCC's original) stops
unless `-o` names the file. A run that stops on an error removes the files it has written.

A construct the translation doesn't cover is an error naming it, never silently different code.

## The output

### MRI mode

- **Register names are symbols** in MRI mode: a symbol named like a `gas` register (`val`,
  `mask`, `acc`, also `d0` in a source with `-r`) is written `NAME__r68`, and `m68k-os9-r68` gives
  the object's symbols their names back.
- **MRI mode aligns** at any directive after byte data of odd length, which r68 doesn't: hence the
  delayed line markers, the empty macros for statements `gas` must not see, macro definitions
  moved to the file's start, and section switches written only where the section changes.
- **MRI macros** treat `<...>` and `!` specially: macro arguments are escaped accordingly.

### Line numbers

Every message `gas` gives names the source's file and line. The output has three parts, each
behind a line marker:

1. **The prelude** (`# 1 "r68.prelude"`): macros for r68's own statements (`os9`, `tcall`, the
   offset counter, sections, data in vsects, `fail`, and the rest below).
2. **The source** (`# 1 "<source>"`), its lines rewritten in place, never removed, so line numbers
   stay 1:1. An inlined `use` file is behind its own line marker, followed by one that sets the
   line number back to the including file's.
3. **The epilogue** (`# 1 "r68.epilogue"`): what one pass over the source learned, such as an
   `xdef` for each global label and generated symbols.

A message naming `r68.prelude` or `r68.epilogue` is the translator's, not a line of the source.
Two cases are a few lines off: after byte data of odd length a line marker would itself make
`gas` align, so it is written before the next statement that aligns anyway (an instruction, a
`dc.w`), and lines added before a macro call inside a macro body get no line marker.

### Separate files

With `-s`, each file the source `use`s is translated into an output file of its own, and `use
<file>` becomes `include <file's output>` in place (unquoted: in MRI mode quotes become part of
the name). The outputs go next to the top-level output, never into the source tree. It is for
looking at a translation file by file; `m68k-os9-r68` uses the inline output.

- Each `use` file's output starts with `.mri 1` and the macros it defines; the top-level one gets
  the prelude and the one epilogue, with the `xdef`s of all files.
- A `use` stays inline after byte data of odd length (an `include` there would align in MRI
  mode) and in a macro body (`gas` would include the file at each expansion).

### Annotations

Where a statement is changed in a way the result doesn't show, the translated line carries the
original as a comment, after the operands and before the line's own comment: `;[r68: <original
statement>]`, or the original operation alone where nothing else changed (`;[r68: addi.w]`).
Each translated file says so at its start. [gastor68](gastor68.md) uses the notes to give the
source back. Annotated, among others:

- link-time expressions (`#r68x_46d5c7a2` for `#Dir_+Write_`);
- `trap #symbol`, written `dc.b $4e,$40+(symbol)`;
- `addi`/`subi` written `add`/`sub`, `ori`/`andi`/`eori` `.w` to `ccr` written `.b`;
- `move.l #n,Dn` with `+r68__0` in the immediate, `move.b` with a negative immediate masked `&$ff`;
- a number or constant before `(pc)`, written `(*+2+n)(pc)`, and `*` in a `dc` list's later
  elements, written `(*-offset)`;
- `psect`, whose fields are spread over the output; `fail` and a `use` file not found;
  `label equ *`, written as a label alone; a folded `rept` of one `dc` line;
- `dc` strings in single quotes or with a backslash, macro arguments whose quoting the `<...>`
  form loses, `dc` lines in a macro body split around `"\N"` strings, renamed macros' calls.

## Dialects

Where Microware's assemblers differ, the output follows one of them byte for byte (`-d`,
`m68k-os9-r68 -dialect=`):

- **`ucc`, the default:** Ultra C's `r68`. Where `gas`'s own choices differ from it (`moveq` for
  `move.l #n,Dn`, no displacement for `0(An)`, short absolute addresses), the translation keeps
  Ultra C's: code written for r68 can depend on instruction sizes (tables of fixed-size entries,
  sizes computed from labels).
- **`c32`:** Microware C 3.2's assemblers, `r68` (68000) and `r68020`, which give the same
  objects for the sources both take. They differ from Ultra C's in some immediate encodings (see
  "Immediates") and pad to 2.

**Padding:** each section of the psect (code, data, uninitialized data, remote data) is padded as
the Microware assemblers pad it, so that a link simply concatenates them:

| | to | code filled with |
|---|---|---|
| `-d c32` | 2 | a zero byte where the size is odd |
| `-d ucc` with `-m0` or `-m1` | 2 | a zero byte where the size is odd |
| `-d ucc` without `-m` | 4 | `nop`, after a zero byte where the size is odd |
| `-d ucc` with `-m2` to `-m6` | 4 | `trapf`, likewise |

`m68k-os9-r68` gives the sections an alignment of 2 where the padding is 2 (`gas` makes 4), so
that a link places the next psect right after. A psect without `ends` (some descriptors end that
way): Ultra C's `r68` fills an odd-sized code section with zeros to 4, an even one as with
`ends`; the translation does the same.

## Translation

### Instructions

Most instruction lines need no change. Where the same text means something else to `gas`:

- **`jsr label` and `lea label,a0`** to a code label: r68 makes them absolute long, `gas`
  PC-relative; written `(label).l`.
- **`(pcr)`**, r68's spelling of PC-relative: `(pc)`.
- **The instructions r68 chooses:** `addi` and `subi` with #1 to #8 are `addq` and `subq` (written
  `add` and `sub`, for which `gas` chooses the same); `move.l #n,Dn` stays `move.l` (where `gas`
  would use `moveq`); `d(An)` stays 16 bits, also when 0 (written `(d).w(An)`); `ori.w #x,ccr` is
  `ori.b`.
- **`moveq #128..255`:** r68 takes the byte; written sign-extended where it isn't a plain small
  number, and a number plus an external (`moveq #$80+Ext,d1`) as the number less 256.
- **`movem` with a mask** instead of a register list: r68 reads a mask in register order and
  reverses it for `-(An)` (`#$113` gives `$C880`; `gas` stores `$0113`). With `-(An)` the operand
  becomes a generated symbol (`<symbol>__rev`, or `r68__mask<n>` for an expression), which the
  epilogue defines as the bit reversal of the original expression.
- **`cinv` and `cpush`:** the cache operands (`bc`, `dc`, `ic`, `nc`) are kept as they are.

### Immediates

- **`move.b` with a negative immediate:** r68 writes `$00nn` (`andi.b` and the others `$FFnn`, as
  `gas`); the value is masked.
- **`-d c32`:** C 3.2's `r68` writes `add`, `sub`, `and`, `or` and `cmp` with an immediate and a
  data register destination as the `<ea>,Dn` form (`and.l #2,d0` is `$C0BC 00000002`), and `add`
  and `sub` with an immediate to an address register as `ADDA`/`SUBA` also for #1 to #8, where
  Ultra C's `r68` and `gas` make `ANDI` and `ADDQ`; with `-d c32` such lines are written as words.

### PC-relative forms on the 68020

- **`-(expr)(An)`** (`lea -(End-Start)(a4),a4`) is a negated displacement, not a predecrement.

### Branches

- **Global labels:** `gas` keeps a relocation for a reference to a global symbol, which an 8-bit
  field can't take; r68 resolves it. PC-relative references and branches to a global label of
  the source refer to a local twin `NAME__r68pc` instead, which the epilogue defines as `NAME`.
  The same for global labels made by macro arguments.

### The psect and vsects

- **`psect <name>,<type/lang>,<attr/rev>,<edition>,<stack>,<entry>[,<trap entry>]`** ...
  `ends` (or `endsect`): `.text`, the psect's name as the file symbol, and for a psect with a type
  the module header symbols `__os9_typelang`, `__os9_attrev`, `__os9_edition`, `__os9_stack`,
  `__os9_entry` and `__os9_trapent`, as [rof2elf](rof2elf.md) makes them, so that ELF from a ROF
  and from r68togas link the same. The fields may be expressions (`(5<<8)+1`, equates); a number
  as the entry is an offset into the code. Where conditionals choose between psect statements,
  the header symbols and the name are those of the psect assembled. `ds` is illegal in a psect,
  as in r68.
- **`vsect` / `vsect remote` ... `ends`:** `ds` into `.bss` / `.remote.bss`, `dc` and `dz` into
  `.data` / `.remote.data`, addressed a6-relative. A label alone on a line in a vsect takes the
  initialized data's counter, not that of a following `ds`, as in r68; `align` in a vsect aligns
  both counters. `ds.b` is written as `fill`, and a section switch only where the section
  changes, since a section statement after byte data of odd length aligns in MRI mode.

### Labels and symbols

- **A label followed by `:` is global** (r68 has no `xdef`/`xref`), others local; the global ones
  get an `xdef` in the epilogue. Outside a psect (in definition files used before it) no label is
  exported. References to undefined names are external, as in r68.
- **`label equ *`** is a plain label.
- **A macro defined with a colon** (`Mac: macro`): both Microware assemblers export the name as a
  global symbol of value 0; written as `NAME equ 0`.
- **`set` symbols used before their first `set`** take the value they had at the end of r68's
  first pass (where symbols not set yet are 0): `NAME__final` in the epilogue, from a
  `NAME__p1` set before each `set`.

### The offset counter

`org <n>`, `do.b/w/l <n>` (counting up), `lo.b/w/l <n>` (counting down) and `.` (the counter's
value: `MemSize: equ .`). In r68 `org` sets only this counter; code after `org` carries on in the
psect. MRI's `offset` doesn't fit (code after it fails), so the counter is a `set` symbol in the
prelude's macros, the label passed as an argument: `V_A do.b 1` becomes a call that sets `V_A` to
the counter and advances it. `do.w`/`do.l` align the counter to even first; `lo.w` and `lo.l`
align it down to even after counting.

### Expressions

- **Precedence:** r68 evaluates `<<` and `>>` after `+` and `-`; MRI before: `1<<8+1` is `$200` in
  r68 and `$101` in `gas`. Every expression is written fully parenthesized by r68's precedence.
- **`>>`** shifts a 32-bit signed value in both Microware assemblers (`$80000000>>4` is
  `$f8000000`), `gas` logically on 64 bits: written as a division of the sign-extended value.
- **`|`** is OR in r68 and starts a comment in `gas`: written `!`. Unary **`^`** (not) is `~`.
  C-style hex **`0x112`** is `$112`. **`$` without a hex digit** takes the value after it.
- **Strings:** `"..."` is r68's string, written `'...'` in string contexts (`dc.b`, string
  operands of `dc.w`/`dc.l`). In r68 a backslash takes the next character literally (`\"` is
  `"`, `\t` is `t`); the translation writes the characters r68 means. The quotes of `use "file"`
  and quoted macro arguments are kept.
- **Junk after a `set` or `equ` value** (`x set x+size*2(a0),a0`): C 3.2's `r68` takes the value up
  to the junk; with `-d c32` the rest is dropped (annotated). Ultra C's `r68` refuses the line, and
  so does the default dialect.

### Externals and link-time expressions

r68 leaves a library's equates (`F$Link`, `E$Abort`, `PD_Opt`) external, and `l68` takes their
values from `sys.l` when linking. The translation does the same wherever one ELF relocation
(symbol plus addend) can take the reference: an expression whose only symbol is the equate, added
(`F$Link`, `E$Abort+1`, `T_VctIRQ-(4*4)`), in any field r68 relocates, bytes included. `os9` and
`tcall` (`trap #0` and `trap #n` with the call's number) keep the call numbers external the same
way.

**Macro arguments:** a macro body's expression whose parameters need a link-time expression
whatever the arguments are (`4*\1`, `\1+\2`, `-\1`) refers to an alias, `.Lr68xm<k>`, which each
call sets for its arguments on a line before the call (the link-time expression, or the value
where the arguments make none). A call inside another macro's body with that macro's parameter
sets the alias to an expression of the outer macro's alias.

### Macros

- `\1` ... `\9` work as they are; `\#` (the argument count) is `NARG`.
- **`\L<n>`** (an argument's length) has no MRI equivalent: a macro using it is called through the
  prelude's `r68L`, which passes the lengths (counting a renamed `@`, `_at_`, as one character).
- **Quoted arguments** (`"(a1,d1.w)"`) are passed without their quotes, as `<...>`; `!`, `<` and
  `>` inside are escaped with `!`. An escaped quote outside quotes (`\""ab> \""`) is not the start
  of a string.
- **Character constants of parameters** (`cmpi.b #'\1',d0`): each call sets a symbol to the
  argument's value as r68 takes it (quotes stripped, `\'` a quote, the last four characters).
- **Macro names** are case-sensitive in r68, not in `gas`: a macro whose name only the case tells
  from another (a macro `OS9` beside r68's `os9`) is defined and called as `NAME__r68m<n>`. A
  macro whose name has a dot (`DO_IT.w`) is called by that name, not as `DO_IT` with a size.
- **Labels a macro makes** (`_X\1:`): a global one is exported by each expansion. The pre-scan
  expands macro calls, nested ones too, to know the names their bodies make, for `-b`, the twins
  and the externals; names a body defines by `equ` (`m\1 equ 1<<.`) are written so that their
  values are known where `moveq` or `move.b` need the byte.
- **`dc`, `ds` and `dz` in a macro body** go where the macro is called, in the code or a vsect:
  each is written for all three places and chosen when the body expands.
- **Conditionals a macro body leaves open** (an `iflt ... else` without `endc`): r68 keeps them
  open after the call, with the rest of the source in them; emulated.
- **`dc.b` strings of a parameter alone** (`"\2"`) with an empty argument make no byte, as in r68.
- A macro called with a size (`MAC.w`) is an error in r68 and stays one.

### Directives

- **`use <file>`:** the file translated and written inline behind its own line marker (or a file of
  its own with `-s`). A plain `use x` is relative to the working directory, `use "x"` to the
  source's directory, `use <x>` searches the `-u` directories, then `$MWOS/OS9/SRC/DEFS`. Lookup
  ignores case. OS-9's multi-dot path components work (`use .../DEFS/oskdefs.d`), which Ultra C's
  `r68` refuses; an OS-9 absolute path of another machine (`use /dd/defs/oskdefs.d`) that doesn't
  exist here is looked up by its file name in the `-u` directories; a DOS path (`use
  c:\os9\defs\oskdefs.d`) as WSL or MSYS mounts the drive. A `use` in a false conditional is
  skipped, so a file not found is an error only where the line is assembled.
- **`os9 <call>`** (a system call: `trap #0`, then the call's code as `dc.w`) and **`tcall
  <n>,<call>`** (a call to trap handler `<n>`: `trap #<n>`, then `dc.w <call>`): the prelude's
  macros, the lines kept as they are. The call's code (`F$Exit`, a trap library's function code)
  stays external, resolved at link time from `sys.l` or the trap library's equates, as in r68.
  `tcall`'s trap instruction is written `dc.b $4e,$40+(<n>)`, so that a trap number given as an
  equate (`tcall T$Math,...`) is a byte relocation, as r68 makes it; the same for `trap #symbol`
  in the source, while `trap #<number>` stays as it is.
- **Listing directives:** `ttl`, `page` and `spc` work in `gas`; `nam`, `pag`, `opt` and `end` are
  calls of the prelude's empty macros.
- **`fail <text>`:** the prelude's `r68error`, which keeps the text.
- **Trailing comments** are stripped where `gas` would read them as operands (`rept 2 two nops`).
- **`rept N` around one `dc.b` line:** MRI mode aligns before a `rept` after byte data of odd
  length, r68 doesn't; the list is written N times on its line.
- **`com`** (common blocks, `NAME: com.s n`): an ELF common symbol, as `rof2elf` makes a ROF's;
  refused in a remote vsect. **`dz`** is `dcb.b <n>,0`. **`iflt`/`ifle`** and the other
  conditionals work as they are; a conditional left open at the end of the source is closed.
- **Ultra C's compiler output:** `=` before a name only marks it as a label; `spanreg` names
  `-bt`'s register; FPU instructions are taken for any CPU.

## Microware compatibility

C 3.2's two assemblers give identical objects for the sources both take (`r68020 -m0` against
`r68`, and `-m2` too); between Ultra C's `r68 -m2` and C 3.2's `r68020 -m2`, every difference
found is one the dialects cover.

The reference manuals are the *OS-9 Assembler/Linker User Manual* (Microware 77165106) for
C 3.2's assembler and *Using Ultra C/C++* for Ultra C's `r68`. Where they and the assemblers
differ, the assemblers are followed.

## Differences and limitations

- **`@` and `_at_` in one source:** where a source has names with `@`, a global name with `_at_`
  of its own comes out with `@` too.
- **Unreferenced externals:** an external named only by an `equ` (`A equ Ext`), or by the twin of
  a label a conditional leaves out, stays in `gas`'s object as an undefined symbol no relocation
  uses, which would make a link take in a library member for it. `m68k-os9-r68` removes such
  symbols; `r68togas` alone doesn't.
- **A macro calling itself** with link-time expression aliases would see the inner call's values
  after it.
- **More lenient than Microware's assemblers** (these are taken where one or both refuse them):
  constructs C 3.2's `r68` refuses and Ultra C's takes, with either dialect (`or.w
  #imm,d16(a0)`, registers with `%`); expressions of externals with `*` or `/`; `set` of an
  external name; a second `ends`; `dc.b 'xy'` (kept as a string); `rept` with LF line ends (Ultra
  C's `r68` needs CR LF there).
- **68020 and later:** the sources measured are mostly 68000 code; 68000 code at `-m2` gives the
  same objects as both Microware assemblers, but the 68020 instructions and addressing modes are
  checked on fewer sources.

## Tests

- **`tests/r68`** (see its README): a small constructed source or script per behaviour above,
  with the expected values and the commit that made a fix; each assembler case is also compared
  with Ultra C's `r68` when `R68` is set, and translated back by gastor68 (see
  [gastor68.md](gastor68.md)).

      TOOLS=<dir> [R68=<r68.exe> R68WORK=/mnt/c/...] bash tests/r68/run.sh [CASE...]

- **`make check-r68`** in `test/abi-exec`: tests whose assembly side is written in r68 syntax
  (`r68tests/`), translated, assembled and run with their C side.

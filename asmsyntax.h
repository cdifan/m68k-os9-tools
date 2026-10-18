/* asmsyntax.h: which syntax an m68k assembler source is written in, Microware's r68 or GNU as
 * (gas), from its statements. Plain C, no dependencies, for reuse (m68k-os9-xcc's .a inputs;
 * perhaps a compiler driver one day). */
#ifndef ASMSYNTAX_H
#define ASMSYNTAX_H

#include <stddef.h>

enum asm_syntax {
	ASM_UNDECIDED,	/* nothing decisive (equates, macros, plain instructions) */
	ASM_R68,	/* a Microware directive: psect, vsect, ends, use, nam, ttl, opt, spc, pag */
	ASM_GAS		/* a gas directive (.text, .globl, .long, ...), a % register, #APP/#NO_APP */
};

/* What LINE (LEN chars, no line end) decides: a caller reading its own way (a buffer, a pipe)
 * loops over the lines up to the first one that isn't ASM_UNDECIDED. */
enum asm_syntax asm_syntax_of_line(const char *line, size_t len);

/* The syntax of source TEXT (LEN bytes): the first decisive statement's. */
enum asm_syntax asm_syntax_of_text(const char *text, size_t len);

/* The same for the file at PATH; ASM_UNDECIDED too where it can't be read. */
enum asm_syntax asm_syntax_of_file(const char *path);

#endif

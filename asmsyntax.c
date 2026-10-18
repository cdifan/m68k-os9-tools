/* asmsyntax.c: which syntax an m68k assembler source is written in (asmsyntax.h).
 *
 * The statements are read in order until one decides:
 * - r68: a Microware directive in the operation field (psect, vsect, ends, use, nam, ttl, opt,
 *   spc, pag), which gas doesn't have;
 * - gas: a known gas directive in the operation field (a known one: r68 names may contain and
 *   start with dots, `.foo equ 1`), a register written with % in an operand (%d0, %a6, %sp;
 *   in r68, % starts a binary number, %1010), or GCC's #APP/#NO_APP and cpp line markers.
 * Plain instructions, labels, equ, dc.b and comments read alike in both and decide nothing. */
#include "asmsyntax.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int word_in(const char *word, size_t len, const char *const *list)
{
	for (; *list != NULL; list++) {
		size_t k = strlen(*list);
		if (k == len) {
			size_t i;
			for (i = 0; i < len && tolower((unsigned char)word[i]) == (*list)[i]; i++)
				;
			if (i == len)
				return 1;
		}
	}
	return 0;
}

static const char *const r68_directives[] = {
	"psect", "vsect", "ends", "use", "nam", "ttl", "opt", "spc", "pag", NULL
};

static const char *const gas_directives[] = {
	".text", ".data", ".bss", ".section", ".previous", ".globl", ".global", ".local", ".weak",
	".long", ".word", ".short", ".byte", ".quad", ".int", ".ascii", ".asciz", ".string",
	".align", ".balign", ".balignw", ".p2align", ".even", ".file", ".ident", ".comm", ".lcomm",
	".set", ".equ", ".equiv", ".macro", ".endm", ".rept", ".endr", ".irp", ".irpc", ".include",
	".if", ".ifdef", ".ifndef", ".else", ".endif", ".type", ".size", ".loc", ".mri", ".skip",
	".space", ".fill", ".zero", ".org", ".swbeg", ".cfi_startproc", ".cfi_endproc", NULL
};

/* % and a register name in operands TEXT (LEN chars). */
static int percent_register(const char *text, size_t len)
{
	static const char *const regs[] = {
		"d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a0", "a1", "a2", "a3", "a4", "a5",
		"a6", "a7", "sp", "fp", "pc", "sr", "ccr", "usp", "zpc", "fp0", "fp1", "fp2", "fp3",
		"fp4", "fp5", "fp6", "fp7", "fpcr", "fpsr", "fpiar", NULL
	};
	char quote = 0;
	for (size_t i = 0; i < len; i++) {
		/* not in a string ("%d0" is text) */
		if (quote != 0) {
			if (text[i] == quote)
				quote = 0;
			continue;
		}
		if (text[i] == '"' || text[i] == '\'') {
			quote = text[i];
			continue;
		}
		if (text[i] != '%')
			continue;
		size_t k = i + 1;
		while (k < len && isalnum((unsigned char)text[k]))
			k++;
		if (word_in(text + i + 1, k - i - 1, regs))
			return 1;
	}
	return 0;
}

enum asm_syntax asm_syntax_of_line(const char *line, size_t len)
{
	if (len == 0)
		return ASM_UNDECIDED;
	if (line[0] == '#') {
		/* GCC's #APP/#NO_APP, a cpp line marker (# 1 "file") */
		if ((len >= 4 && strncmp(line, "#APP", 4) == 0) || (len >= 7 && strncmp(line, "#NO_APP", 7) == 0))
			return ASM_GAS;
		if (len >= 3 && line[1] == ' ' && isdigit((unsigned char)line[2]))
			return ASM_GAS;
		return ASM_UNDECIDED;
	}
	if (line[0] == '*' || line[0] == ';')
		return ASM_UNDECIDED;	/* a comment line */
	size_t i = 0;
	/* a label (from the first column), with or without a colon */
	if (!isspace((unsigned char)line[0])) {
		while (i < len && !isspace((unsigned char)line[i]) && line[i] != ':')
			i++;
		if (i < len && line[i] == ':')
			i++;
	}
	while (i < len && isspace((unsigned char)line[i]))
		i++;
	size_t op = i;
	while (i < len && !isspace((unsigned char)line[i]) && line[i] != ';')
		i++;
	size_t opn = i - op;
	if (opn == 0)
		return ASM_UNDECIDED;
	if (word_in(line + op, opn, r68_directives))
		return ASM_R68;
	if (line[op] == '.' && (word_in(line + op, opn, gas_directives) ||
	                     (opn > 5 && strncmp(line + op, ".cfi_", 5) == 0)))
		return ASM_GAS;
	/* the operands, up to a comment (; or #) */
	while (i < len && isspace((unsigned char)line[i]))
		i++;
	size_t ops = i;
	char quote = 0;
	while (i < len && (quote != 0 || (line[i] != ';' && line[i] != '|'))) {
		if (quote != 0 && line[i] == quote)
			quote = 0;
		else if (quote == 0 && (line[i] == '"' || line[i] == '\''))
			quote = line[i];
		i++;
	}
	if (percent_register(line + ops, i - ops))
		return ASM_GAS;
	return ASM_UNDECIDED;
}

enum asm_syntax asm_syntax_of_text(const char *text, size_t len)
{
	size_t i = 0;
	while (i < len) {
		size_t j = i;
		while (j < len && text[j] != '\n' && text[j] != '\r')
			j++;
		enum asm_syntax s = asm_syntax_of_line(text + i, j - i);
		if (s != ASM_UNDECIDED)
			return s;
		i = j + 1;
	}
	return ASM_UNDECIDED;
}

static enum asm_syntax of_stream(FILE *fp)
{
	/* line by line, up to the first decisive statement, a line ending at
	 * \r\n, \r (OS-9's own) or \n; a line longer than the buffer is taken
	 * in pieces, the rest of it then as lines of its own (which, past an
	 * operation field, can only decide by a % register) */
	char line[4096];
	size_t len = 0;
	int c;
	while ((c = getc(fp)) != EOF) {
		if (c != '\r' && c != '\n' && len < sizeof line) {
			line[len++] = (char)c;
			continue;
		}
		enum asm_syntax s = asm_syntax_of_line(line, len);
		if (s != ASM_UNDECIDED)
			return s;
		len = 0;
		if (c != '\r' && c != '\n')
			line[len++] = (char)c;	/* a long line's next piece */
	}
	return asm_syntax_of_line(line, len);
}

enum asm_syntax asm_syntax_of_file(const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return ASM_UNDECIDED;
	enum asm_syntax s = of_stream(fp);
	fclose(fp);
	return s;
}

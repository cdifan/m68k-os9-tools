/* r68x_note.h: link-time expressions of external symbols, for references an
 * ELF relocation (one symbol plus an addend) can't express but r68's ROF can:
 * sums and differences of several externals (Dir_+Write_, PD_PAR-PD_OPT),
 * an external negated (-E$Abort); r68togas also takes one multiplied
 * (4*F$Send), which r68 refuses. l68 resolves those when it
 * links; here such a field gets a relocation against a symbol named after
 * the expression, r68x_<hash>, and the object gets a note in the section
 * .r68x defining that symbol; the link front ends (os9link.c) pass the
 * definitions to ld. r68togas and rof2elf make the notes, os9link.c reads
 * them: the canonical form and the names are made here only, so that every
 * tool names an expression alike.
 *
 * The canonical form: the terms sorted by symbol name (strcmp), each written
 * as NAME, -NAME, N*NAME or -N*NAME, joined by + and -; no constant (that
 * stays the relocation's addend). A note is a line NAME=CANONICAL.
 *
 * MIT License; see elf2mod.c.
 */

#ifndef R68X_NOTE_H
#define R68X_NOTE_H

#include <stddef.h>

#define R68X_SECTION ".r68x"
#define R68X_PREFIX "r68x_"

struct r68x_term {
	char *sym;
	long coef;
};

struct r68x {
	struct r68x_term *t;
	int n;
};

/* Adds COEF times symbol SYM (N chars); terms of a symbol merge, a zero
 * coefficient drops the term. */
void r68x_add(struct r68x *x, const char *sym, size_t n, long coef);
void r68x_free(struct r68x *x);

/* The number of terms, and whether one relocation can't express it (more
 * than one term, or a coefficient other than 1). */
int r68x_needed(const struct r68x *x);

/* The canonical text (allocated). */
char *r68x_text(const struct r68x *x);

/* The symbol name for canonical text TEXT: r68x_ and 8 hex digits (a 32-bit
 * FNV-1a hash), in NAME (at least 16 chars). */
void r68x_name(const char *text, char *name);

/* Parses canonical text TEXT into X (empty first); 0 if it isn't one. */
int r68x_parse(const char *text, struct r68x *x);

/* The expression as ld's linker script writes it (allocated): each symbol
 * quoted, as names like F$Send need. */
char *r68x_ld_expression(const struct r68x *x);

#endif

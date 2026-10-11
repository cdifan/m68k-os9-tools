/* r68togas: Microware r68 assembler sources to GNU as sources (MRI mode).
 *
 * Translates r68's own layer of the language line by line, so that every
 * line keeps its number: the output starts with a prelude (MRI macros for
 * r68's directives), then the source behind a linemarker, rewritten in
 * place, then an epilogue (xdefs, the module header symbols). GNU as
 * assembles it with ".mri 1" set in the prelude. See r68togas.md.
 *
 * MIT License; see elf2mod.c.
 */

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r68x_note.h"

static const char *progname = "r68togas";

static void die(const char *format, ...)
{
	va_list ap;
	fprintf(stderr, "%s: ", progname);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

static void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (p == NULL)
		die("out of memory");
	return p;
}

static void *xrealloc(void *p, size_t n)
{
	p = realloc(p, n ? n : 1);
	if (p == NULL)
		die("out of memory");
	return p;
}

static char *xstrdup(const char *s)
{
	char *d = xmalloc(strlen(s) + 1);
	strcpy(d, s);
	return d;
}

static char *xstrndup(const char *s, size_t n)
{
	char *d = xmalloc(n + 1);
	memcpy(d, s, n);
	d[n] = '\0';
	return d;
}

static int ci_eq(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
	return *a == *b;
}

/* ----- A growing string ----- */

struct buf {
	char *s;
	size_t n, size;
};

static void bput(struct buf *b, const char *s, size_t n)
{
	if (b->n + n + 1 > b->size) {
		b->size = (b->n + n + 1) * 2;
		b->s = xrealloc(b->s, b->size);
	}
	memcpy(b->s + b->n, s, n);
	b->n += n;
	b->s[b->n] = '\0';
}

static void bputs(struct buf *b, const char *s)
{
	bput(b, s, strlen(s));
}

static void bputc(struct buf *b, char c)
{
	bput(b, &c, 1);
}

static void bprintf(struct buf *b, const char *format, ...)
{
	char tmp[4096];
	va_list ap;
	va_start(ap, format);
	vsnprintf(tmp, sizeof tmp, format, ap);
	va_end(ap);
	bputs(b, tmp);
}

/* ----- Names ----- */

/* GNU as's m68k register names (from binutils' tc-m68k.c), which it also
 * knows in upper case and, in MRI mode, without '%': as symbols they must be
 * renamed. Those that r68 knows as registers too are left out: there they
 * are registers anyway. */
static const char *const gas_registers[] = {
	"fp", "ssp", "fpi", "fpc", "fps", "control", "status", "iaddr",
	"cop0", "cop1", "cop2", "cop3", "cop4", "cop5", "cop6", "cop7", "zpc", "cc", "acc", "acc0", "acc1",
	"acc2", "acc3", "accext01", "accext23", "macsr", "mask", "sfcr", "dfcr", "cpucr", "itt0",
	"itt1", "dtt0", "dtt1", "iacr0", "iacr1", "dacr0", "dacr1", "acr0", "acr1", "acr2", "acr3", "acr4",
	"acr5", "acr6", "acr7", "tc", "tcr", "asid", "mmusr", "srp", "urp", "buscr", "mmubar", "rombar",
	"rambar0", "rambar1", "mbar", "mbar0", "mbar1", "rombar0", "rombar1", "mpcr", "edrambar",
	"secmbar", "pcr1u0", "pcr1l0", "pcr2u0", "pcr2l0", "pcr3u0", "pcr3l0", "pcr1u1", "pcr1l1",
	"pcr2u1", "pcr2l1", "pcr3u1", "pcr3l1", "flashbar", "rambar", "mbar2", "rgpiobar", "cac", "mbb",
	"mbo", "ac", "bc", "cal", "crp", "drp", "pcsr", "psr", "scc", "val", "bad0", "bad1", "bad2", "bad3",
	"bad4", "bad5", "bad6", "bad7", "bac0", "bac1", "bac2", "bac3", "bac4", "bac5", "bac6", "bac7",
	"ic", "dc", "nc", "tt0", "tt1", "ac0", "ac1", "acusr", "zd0", "zd1", "zd2", "zd3", "zd4", "zd5",
	"zd6", "zd7", "za0", "za1", "za2", "za3", "za4", "za5", "za6", "za7", NULL,
};

/* r68's own register names (and pcr, its PC-relative spelling). */
static const char *const r68_registers[] = {
	"d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
	"sp", "pc", "pcr", "sr", "ccr", "usp", "vbr", "sfc", "dfc", "cacr", "caar", "msp", "isp",
	"fp0", "fp1", "fp2", "fp3", "fp4", "fp5", "fp6", "fp7", "fpcr", "fpsr", "fpiar", NULL,
};

static int in_list(const char *const *list, const char *s, size_t n, int any_case)
{
	for (; *list; list++)
		if (strlen(*list) == n && (any_case ? strncasecmp(*list, s, n) == 0 : strncmp(*list, s, n) == 0))
			return 1;
	return 0;
}

/* Whether S (N chars) is a GNU as register name as a symbol would clash
 * with it: lower or upper case, as gas enters them. */
static int clashes(const char *s, size_t n)
{
	for (const char *const *r = gas_registers; *r; r++) {
		if (strlen(*r) != n)
			continue;
		if (strncmp(*r, s, n) == 0)
			return 1;
		size_t i;
		for (i = 0; i < n && s[i] == toupper((unsigned char)(*r)[i]); i++)
			;
		if (i == n)
			return 1;
	}
	return 0;
}

static int is_r68_register(const char *s, size_t n)
{
	return in_list(r68_registers, s, n, 1);
}

/* A symbol's name as GNU as gets it: '@' (legal in r68) as "_at_", a name
 * GNU as takes for a register with "__r68" after it. */
static void put_symbol(struct buf *b, const char *s, size_t n)
{
	/* A dot inside the name (labels lab.a ... lab.s):
	 * __dot__, as would read bls.s lab.s's .s as a size;
	 * with __r68 after the name, whose dots m68k-os9-r68 gives back. */
	int dots = 0;
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '\\' && i + 1 < n) {
			/* a macro parameter in the name (Name\@) */
			bput(b, s + i, 2);
			i++;
		} else if (s[i] == '@')
			bputs(b, "_at_");
		else if (s[i] == '.' && i > 0) {
			bputs(b, "__dot__");
			dots = 1;
		} else
			bputc(b, s[i]);
	}
	if (dots || clashes(s, n))
		bputs(b, "__r68");
}

static int symchar(int c)
{
	return isalnum(c) || c == '_' || c == '@' || c == '$' || c == '.';
}

static int symstart(int c)
{
	return isalpha(c) || c == '_' || c == '@';
}

/* ----- Expressions ----- */

/* r68's operators by precedence, lowest first (the manual: "-" and "^"
 * unary; & and !/| bind tightest of the binary ones, then * and /, then +
 * and -, then << and >>). */
enum { P_SHIFT = 1, P_ADD, P_MUL, P_LOGIC };

struct lexer {
	const char *s, *p;
	int ok;
};

/* Translates an expression E (r68) to GNU as MRI syntax into B: every
 * binary operation below the top parenthesized by r68's precedence, | as !,
 * unary ^ as ~, 0x as $, character constants as numbers, '.' (the offset
 * counter) as r68__ofs, symbols renamed (put_symbol). When CHARS is set,
 * a quoted character constant becomes its number (dc.w and dc.l). */
static int expr_chars;

static void parse_binary(struct lexer *lx, struct buf *b, int prec, int top);
static int forward_set(const char *s, size_t n);

static void skipsp(struct lexer *lx)
{
	(void)lx;
}

/* One operand: a number, symbol, character constant, '*', '.', a macro
 * parameter, a parenthesized expression, or a unary operator before one. */
static int annotate;
static int global_label(const char *s, size_t n);
static void put_twin(struct buf *b, const char *s, size_t n);
static int twin_mode;	/* in a PC-relative displacement or a branch target: global labels as twins */
static int p1_mode;	/* an expression as r68's first pass: set symbols not set yet are 0 (p1_name) */
static int p1_name(struct buf *b, const char *s, size_t n);
static int in_macro_def(void);

static void parse_unary(struct lexer *lx, struct buf *b)
{
	const char *p = lx->p;
	skipsp(lx);
	if (*p == '-' ) {
		bputc(b, '-');
		lx->p++;
		parse_unary(lx, b);
		return;
	}
	if (*p == '^' || *p == '~') {
		bputc(b, '~');
		lx->p++;
		parse_unary(lx, b);
		return;
	}
	if (*p == '+') {
		lx->p++;
		parse_unary(lx, b);
		return;
	}
	if (*p == '(') {
		lx->p++;
		bputc(b, '(');
		parse_binary(lx, b, P_SHIFT, 1);
		if (*lx->p == ')')
			lx->p++;
		else
			lx->ok = 0;
		bputc(b, ')');
		return;
	}
	if (*p == '\\') {
		/* A macro parameter: \1..\9, \#, \@, \0, \Ln. */
		const char *q = p + 1;
		if (*q == 'L' || *q == 'l')
			q++;
		if (*q != '\0')
			q++;
		if (p[1] == '#')
			bputs(b, "NARG");
		else if ((p[1] == 'L' || p[1] == 'l') && isdigit((unsigned char)p[2]))
			/* \Ln: the length of argument n, counted by r68L */
			bprintf(b, "r68__L%c", p[2]);
		else
			bput(b, p, q - p);
		lx->p = q;
		return;
	}
	if (*p == '$' && !isxdigit((unsigned char)p[1]) && p[1] != '\0' && !symchar((unsigned char)p[1])) {
		/* $ without a hex digit, as in #$((\1)<<16)+(\2) (in
		 * macros): r68 takes the value after it. */
		lx->p++;
		annotate = 1;
		parse_unary(lx, b);
		return;
	}
	if (*p == '$') {
		const char *q = p + 1;
		while (isxdigit((unsigned char)*q))
			q++;
		bput(b, p, q - p);
		lx->p = q;
		return;
	}
	if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
		const char *q = p + 2;
		while (isxdigit((unsigned char)*q))
			q++;
		bputc(b, '$');
		bput(b, p + 2, q - p - 2);
		lx->p = q;
		return;
	}
	if (*p == '%') {
		const char *q = p + 1;
		while (*q == '0' || *q == '1')
			q++;
		bput(b, p, q - p);
		lx->p = q;
		return;
	}
	if (isdigit((unsigned char)*p)) {
		const char *q = p;
		while (isdigit((unsigned char)*q))
			q++;
		/* A float (dc.d, fmove #) passes through. */
		if (*q == '.' && isdigit((unsigned char)q[1])) {
			q++;
			while (isdigit((unsigned char)*q))
				q++;
		}
		if ((*q == 'e' || *q == 'E') && (isdigit((unsigned char)q[1]) || ((q[1] == '-' || q[1] == '+') && isdigit((unsigned char)q[2])))) {
			q += 2;
			while (isdigit((unsigned char)*q))
				q++;
		}
		bput(b, p, q - p);
		lx->p = q;
		return;
	}
	if (*p == '\'' && p[1] == '\\' && isdigit((unsigned char)p[2]) && p[3] == '\'' && in_macro_def()) {
		/* '\1' in a macro body (CHOOSE's case.b: cmpi.b #'\1',d0): the
		 * argument's character, as written, for as to substitute. */
		bput(b, p, 4);
		lx->p = p + 4;
		return;
	}
	if (*p == '\'') {
		/* A character constant: up to four characters, right-aligned
		 * (r68's value); '' is a quote. */
		const char *q = p + 1;
		unsigned long v = 0;
		int count = 0;
		while (*q != '\0') {
			if (*q == '\'') {
				if (q[1] == '\'') {
					q++;
				} else {
					break;
				}
			}
			v = v << 8 | (unsigned char)*q;
			count++;
			q++;
		}
		if (*q == '\'')
			q++;
		else
			lx->ok = 0;
		/* as splits operands at a comma, and ends them at a blank, in a
		 * quote too (move.b #',',(a3)+): only a letter or digit is
		 * kept as a character. */
		if (expr_chars || count != 1 || !isalnum((unsigned char)v))
			bprintf(b, "$%lx", v);
		else
			bput(b, p, q - p);
		lx->p = q;
		return;
	}
	if (*p == '*') {
		bputc(b, '*');
		lx->p++;
		return;
	}
	if (*p == '.' && !symchar((unsigned char)p[1])) {
		bputs(b, "r68__ofs");
		lx->p++;
		return;
	}
	if (*p == '=' && symstart((unsigned char)p[1])) {
		/* Ultra C's label designator, not part of the name. */
		lx->p++;
		parse_unary(lx, b);
		return;
	}
	if (symstart((unsigned char)*p) || *p == '.') {
		const char *q = p;
		while (symchar((unsigned char)*q))
			q++;
		if (twin_mode && global_label(p, q - p)) {
			put_twin(b, p, q - p);
			lx->p = q;
			return;
		}
		if (p1_mode && p1_name(b, p, q - p)) {
			lx->p = q;
			return;
		}
		put_symbol(b, p, q - p);
		if (forward_set(p, q - p))
			bputs(b, "__final");
		lx->p = q;
		return;
	}
	lx->ok = 0;
}

/* The binary operator at P, its precedence and length; 0 if none. */
static int binop(const char *p, int *len, const char **as)
{
	if (p[0] == '<' && p[1] == '<') { *len = 2; *as = "<<"; return P_SHIFT; }
	if (p[0] == '>' && p[1] == '>') { *len = 2; *as = ">>"; return P_SHIFT; }
	*len = 1;
	switch (*p) {
	case '+': *as = "+"; return P_ADD;
	case '-': *as = "-"; return P_ADD;
	case '*': *as = "*"; return P_MUL;
	case '/': *as = "/"; return P_MUL;
	case '&': *as = "&"; return P_LOGIC;
	case '!': case '|': *as = "!"; return P_LOGIC;
	}
	return 0;
}

/* Operators of precedence PREC and higher; TOP: the outermost level, left
 * without parentheses. */
static void parse_binary(struct lexer *lx, struct buf *b, int prec, int top)
{
	if (prec > P_LOGIC) {
		parse_unary(lx, b);
		return;
	}
	struct buf left = { 0 };
	parse_binary(lx, &left, prec + 1, 0);
	int len, p;
	const char *as;
	int any = 0;
	while ((p = binop(lx->p, &len, &as)) == prec) {
		struct buf right = { 0 };
		lx->p += len;
		parse_binary(lx, &right, prec + 1, 0);
		struct buf both = { 0 };
		bputs(&both, left.s ? left.s : "");
		bputs(&both, as);
		bputs(&both, right.s ? right.s : "");
		free(left.s);
		free(right.s);
		left = both;
		any = 1;
		/* Left to right: the next operation has this one as its left
		 * operand, parenthesized below unless at the top. */
		if (binop(lx->p, &len, &as) == prec) {
			struct buf par = { 0 };
			bputc(&par, '(');
			bputs(&par, left.s);
			bputc(&par, ')');
			free(left.s);
			left = par;
		}
	}
	if (any && !top) {
		bputc(b, '(');
		bputs(b, left.s);
		bputc(b, ')');
	} else if (left.s) {
		bputs(b, left.s);
	}
	free(left.s);
}

static int fold_all, fold_first, fold_imm;	/* fold_imm: in # operands only (quick fields) */
static int dc_size;	/* in a dc list: the element size (* is the statement's start) */	/* fold_first: in the first of a list (a trap macro's trap) */
static int annotate, annotate_op;	/* this line changed in a way not obvious from it: noted
					   (annotate_op: only its operation, noted alone) */
static struct buf orig_stmt;		/* its statement as in the source, for the note */
static struct buf orig_op;		/* its operation as in the source */
static struct buf line_rest;		/* the line's comment, as the translation ends with it */
static char *fold_equates(const char *s);
static FILE *fold_log;			/* R68TOGAS_FOLDLOG: where the folds are listed */
static const char *log_path = "";	/* the line being translated, for it */
static int log_line;
static struct buf line_before;	/* lines translate_file writes before the line's */

/* Translates expression text E (N chars) into B; returns 0 when it isn't
 * one r68 would take whole (then copied as it is). */
static int expression(struct buf *b, const char *e, size_t n)
{
	char *s = xstrndup(e, n);
	char *f = fold_equates(s);
	if (f != NULL) {
		free(s);
		s = f;
		e = s;
		n = strlen(s);
	}
	struct lexer lx = { s, s, 1 };
	struct buf out = { 0 };
	parse_binary(&lx, &out, P_SHIFT, 1);
	int ok = lx.ok && *lx.p == '\0';
	if (ok)
		bputs(b, out.s ? out.s : "");
	else
		bput(b, e, n);
	free(out.s);
	free(s);
	return ok;
}

/* The length of the longest expression at the start of E (N chars), 0 if
 * none: C 3.2's R68 takes a set's or equ's value up to there and ignores
 * the rest (x set x+size*2(a0),a0). */
static size_t expression_length(const char *e, size_t n)
{
	char *s = xstrndup(e, n);
	struct lexer lx = { s, s, 1 };
	struct buf out = { 0 };
	parse_binary(&lx, &out, P_SHIFT, 1);
	size_t len = lx.ok ? (size_t)(lx.p - s) : 0;
	free(out.s);
	free(s);
	return len;
}

/* ----- Operands ----- */

/* Whether text S (N chars) is a register as r68 writes one in an address
 * mode: Dn, An, sp, pc, pcr, with an index size (.w, .l) and scale (*n). */
static int register_part(const char *s, size_t n)
{
	size_t k = 0;
	/* a macro parameter in a register's place (\1(\2) in a macro body) */
	if (n >= 2 && s[0] == '\\' && isdigit((unsigned char)s[1]))
		return n == 2;
	while (k < n && isalnum((unsigned char)s[k]))
		k++;
	if (k == 0 || !is_r68_register(s, k))
		return 0;
	if (k + 1 < n && s[k] == '.' && strchr("wlWL", s[k + 1]))
		k += 2;
	if (k < n && s[k] == '*' && k + 1 < n && strchr("1248", s[k + 1]))
		k += 2;
	return k == n;
}

/* Whether the register in an address mode's parentheses S (N chars) is an
 * address register (a0-a7, sp). */
static int split_commas(const char *s, size_t n, const char **piece, size_t *len, int max);

static int address_register(const char *s, size_t n)
{
	const char *pieces[8];
	size_t lens[8];
	int k = split_commas(s, n, pieces, lens, 8);
	for (int i = 0; i < k; i++) {
		if (!register_part(pieces[i], lens[i]))
			continue;
		if (lens[i] == 2 && strncasecmp(pieces[i], "sp", 2) == 0)
			return 1;
		/* a parameter: taken as an address register (d(\2) keeps its
		 * 16-bit displacement, which r68 keeps for any register) */
		if (lens[i] == 2 && pieces[i][0] == '\\')
			return 1;
		return lens[i] == 2 && tolower((unsigned char)pieces[i][0]) == 'a' && pieces[i][1] >= '0' &&
			pieces[i][1] <= '7';
	}
	return 0;
}

/* Whether the base register in an address mode's parentheses S (N chars)
 * is the PC (pc, pcr). */
static int pc_register(const char *s, size_t n)
{
	const char *pieces[8];
	size_t lens[8];
	int k = split_commas(s, n, pieces, lens, 8);
	return k >= 1 && (lens[0] == 2 || lens[0] == 3) && strncasecmp(pieces[0], "pcr", lens[0]) == 0;
}

/* Whether S (N chars) is numbers only: [-]number, joined by + and -. */
static int is_number(const char *s, size_t n)
{
	size_t i = 0;
	for (;;) {
		if (i < n && s[i] == '-')
			i++;
		size_t start = i;
		if (i < n && s[i] == '$') {
			for (i++; i < n && isxdigit((unsigned char)s[i]); i++)
				;
		} else if (i < n && s[i] == '%') {
			for (i++; i < n && (s[i] == '0' || s[i] == '1'); i++)
				;
		} else {
			for (; i < n && isdigit((unsigned char)s[i]); i++)
				;
		}
		if (i == start || (i == start + 1 && (s[start] == '$' || s[start] == '%')))
			return 0;
		if (i == n)
			return 1;
		if (s[i] != '+' && s[i] != '-')
			return 0;
		i++;
	}
}

/* The offset of a PC-relative operand's extension word in the instruction
 * (2; 4 after a movem mask or a bit number). */
static int pc_ext_offset = 2;

/* Whether S (N chars) is a register or register list (d0-d3/a0). */
static int register_list(const char *s, size_t n)
{
	size_t i = 0;
	while (i < n) {
		size_t j = i;
		while (j < n && isalnum((unsigned char)s[j]))
			j++;
		if (j == i || !is_r68_register(s + i, j - i))
			return 0;
		if (j < n && (s[j] == '-' || s[j] == '/'))
			j++;
		i = j;
	}
	return n > 0;
}

/* Splits S at top-level commas (outside parentheses and quotes) into
 * pieces; returns their number. */
static int split_commas(const char *s, size_t n, const char **piece, size_t *len, int max)
{
	int count = 0, depth = 0;
	char quote = 0;
	size_t start = 0;
	for (size_t i = 0; i <= n; i++) {
		char c = i < n ? s[i] : ',';
		if (quote && i < n) {
			/* In "...", a backslash takes the next character. */
			if (c == '\\' && quote == '"' && i + 1 < n)
				i++;
			else if (c == quote)
				quote = 0;
			continue;
		}
		if ((c == '\'' || c == '"') && i < n) {
			quote = c;
			continue;
		}
		if (c == '(')
			depth++;
		else if (c == ')')
			depth--;
		else if (c == ',' && depth <= 0) {
			if (count < max) {
				piece[count] = s + start;
				len[count] = i - start;
				count++;
			}
			start = i + 1;
		}
	}
	return count;
}

static int global_label(const char *s, size_t n);
static void put_twin(struct buf *b, const char *s, size_t n);

/* S (N chars) with register names in lower case: r68 takes them in any
 * case, as in lower or upper case only (-(Sp)). */
static void put_regs(struct buf *b, const char *s, size_t n)
{
	for (size_t i = 0; i < n;) {
		size_t j = i;
		while (j < n && isalnum((unsigned char)s[j]))
			j++;
		if (j > i && (i == 0 || !symchar((unsigned char)s[i - 1])) && is_r68_register(s + i, j - i)) {
			for (size_t k = i; k < j; k++)
				bputc(b, (char)tolower((unsigned char)s[k]));
			i = j;
		} else if (j > i) {
			bput(b, s + i, j - i);
			i = j;
		} else {
			bputc(b, s[i++]);
		}
	}
}

/* Translates one instruction operand. ABSOLUTE: a bare expression operand
 * is an absolute address (r68's jsr/jmp/lea/pea label: absolute long). */
static void operand(struct buf *b, const char *s, size_t n, int absolute)
{
	if (n == 0)
		return;
	if (*s == '#') {
		bputc(b, '#');
		int save = fold_all;
		if (fold_imm)
			fold_all = 1;
		expression(b, s + 1, n - 1);
		fold_all = save;
		return;
	}
	if (register_list(s, n)) {
		put_regs(b, s, n);
		return;
	}
	if (n >= 4 && s[0] == '-' && s[1] == '(' && s[n - 1] == ')' && register_part(s + 2, n - 3)) {
		/* -(An); not -(a-b)(An), a negated displacement,
		 * which goes on as an address mode */
		put_regs(b, s, n);
		return;
	}
	if (n >= 4 && s[0] == '(' && s[n - 2] == ')' && s[n - 1] == '+') {
		put_regs(b, s, n);
		return;
	}
	/* An address mode: displacement(registers...), or (bd,An,Xn). */
	if (s[n - 1] == ')' || (n > 2 && s[n - 3] == ')' && s[n - 2] == '.')) {
		size_t end = s[n - 1] == ')' ? n - 1 : n - 3;
		int depth = 0;
		size_t open = 0;
		for (size_t i = end + 1; i-- > 0;) {
			if (s[i] == ')')
				depth++;
			else if (s[i] == '(' && --depth == 0) {
				open = i;
				break;
			}
		}
		const char *inner = s + open + 1;
		size_t ilen = end - open - 1;
		const char *pieces[8];
		size_t lens[8];
		int k = split_commas(inner, ilen, pieces, lens, 8);
		int regs = 0;
		for (int i = 0; i < k; i++)
			regs += register_part(pieces[i], lens[i]);
		if (regs > 0) {
			/* d(An): r68 keeps a 16-bit displacement, also of 0, which
			 * as would drop; (d).w(An) keeps it, on any CPU. */
			int an = regs == 1 && k == (open > 0 ? 1 : 2) && address_register(inner, ilen);
			/* A number before (pc): r68's raw displacement, which as
			 * (MRI) takes as an absolute address: the address it
			 * means, from the extension word's. */
			int pc = open > 0 && pc_register(inner, ilen) && is_number(s, open);
			if (an) {
				const char *d = s, *r = NULL;
				size_t dn = open, rn = 0;
				for (int i = 0; i < k; i++) {
					if (register_part(pieces[i], lens[i]))
						r = pieces[i], rn = lens[i];
					else
						d = pieces[i], dn = lens[i];
				}
				bputc(b, '(');
				expression(b, d, dn);
				bprintf(b, ").w(%.*s)", (int)rn, r);
				bput(b, s + end + 1, n - end - 1);
				return;
			}
			if (pc) {
				bprintf(b, "(*+%d+", pc_ext_offset);
				annotate = 1;
				expression(b, s, open);
				bputc(b, ')');
			} else if (open > 0 && pc_register(inner, ilen) && k == 1) {
				/* label(pc): 16 bits, as r68 keeps it: for a 68020 as
				 * would make a symbol whose distance it doesn't know
				 * (a global's, an external's) the 32-bit (bd,PC) form;
				 * (label).w(pc) is the same on a 68000. */
				bputc(b, '(');
				twin_mode = 1;
				expression(b, s, open);
				twin_mode = 0;
				bputs(b, ").w");
			} else if (open > 0 && pc_register(inner, ilen)) {
				/* d(pc,Xn): 8 bits, the brief form, as r68 keeps it:
				 * for a 68020 as would make a value it doesn't know in
				 * its first pass (L12.-LI12., a label further on) the
				 * full form with a 16-bit displacement. */
				bputc(b, '(');
				twin_mode = 1;
				expression(b, s, open);
				twin_mode = 0;
				bputs(b, ").b");
			} else if (open > 0) {
				expression(b, s, open);
			}
			bputc(b, '(');
			for (int i = 0; i < k; i++) {
				if (i)
					bputc(b, ',');
				if (register_part(pieces[i], lens[i])) {
					if (lens[i] == 3 && strncasecmp(pieces[i], "pcr", 3) == 0)
						bputs(b, "pc");
					else
						put_regs(b, pieces[i], lens[i]);
				} else {
					expression(b, pieces[i], lens[i]);
				}
			}
			bputc(b, ')');
			bput(b, s + end + 1, n - end - 1);
			return;
		}
	}
	/* An absolute address with a size (expr.w, expr.l, (expr).l). */
	if (n > 2 && s[n - 2] == '.' && strchr("wlWL", s[n - 1])) {
		expression(b, s, n - 2);
		bput(b, s + n - 2, 2);
		return;
	}
	if (absolute) {
		bputc(b, '(');
		expression(b, s, n);
		bputs(b, ").l");
		return;
	}
	expression(b, s, n);
}

/* ----- Strings ----- */

/* An r68 string operand "..." or '...' as an MRI one: single quotes, a
 * quote inside doubled; r68's backslash takes the next character as it is. */
/* In a macro body, where \1 ... \9, \@, \# in a string are the macro's
 * (r68 substitutes them first; MRI does inside quotes too). */
static int string_params;

static void string_operand(struct buf *b, const char *s, size_t n)
{
	char q = s[0];
	bputc(b, '\'');
	for (size_t i = 1; i < n; i++) {
		char c = s[i];
		if (string_params && c == '\\' && i + 1 < n && (isdigit((unsigned char)s[i + 1]) || s[i + 1] == '@' ||
								 s[i + 1] == '#')) {
			bput(b, s + i, 2);
			i++;
			continue;
		}
		if (c == q) {
			if (i + 1 < n && s[i + 1] == q) {
				i++;
			} else {
				break;
			}
		} else if (c == '\\' && q == '"' && i + 1 < n) {
			c = s[++i];
		}
		if (c == '\'')
			bputc(b, '\'');
		bputc(b, c);
	}
	bputc(b, '\'');
}

/* ----- Lines ----- */

struct line {
	char *label;		/* null if none */
	int global;		/* label: */
	char *op;		/* opcode, with its size; null if none */
	char *args;		/* operand field */
	char *rest;		/* the comment after the operands, with its blank */
};

/* The comment field S (from its blank) as a ';' comment: GNU as takes text
 * after the operands as a comment, but not everywhere (fill, a macro body's
 * lines, which lose the blank in the expansion). */
static char *comment(const char *s)
{
	const char *c = s;
	while (*c && isspace((unsigned char)*c))
		c++;
	if (*c == '\0')
		return xstrdup("");
	char *r = xmalloc(strlen(s) + 3);
	sprintf(r, "%.*s%s%s", c == s ? 1 : (int)(c - s), c == s ? " " : s, *c == ';' ? "" : ";", c);
	return r;
}

/* Instructions and directives without operands: what follows them is a
 * comment. */
static int no_operands(const char *op)
{
	static const char *const names[] = {
		"nop", "rts", "rte", "rtr", "reset", "trapv", "illegal", "endc", "else", "endm", "endr",
		"ends", "endsect", "end", "pag", "page", NULL,
	};
	char base[16];
	size_t i;
	for (i = 0; op[i] && op[i] != '.' && i + 1 < sizeof base; i++)
		base[i] = tolower((unsigned char)op[i]);
	base[i] = '\0';
	return in_list(names, base, i, 0);
}

/* Splits line TEXT into its fields; returns 0 for a comment or blank line. */
static int split_line(const char *text, struct line *l)
{
	memset(l, 0, sizeof *l);
	const char *p = text;
	if (*p == '\0' || *p == '*' || *p == ';')
		return 0;
	if (!isspace((unsigned char)*p)) {
		/* "=" before a label only says it is one (Ultra C). */
		if (*p == '=')
			p++;
		const char *q = p;
		while (*q && !isspace((unsigned char)*q) && *q != ':')
			q++;
		l->label = xstrndup(p, q - p);
		if (*q == ':') {
			l->global = 1;
			q++;
		}
		p = q;
	}
	const char *blank = p;
	while (*p && isspace((unsigned char)*p))
		p++;
	if (*p == '\0' || *p == '*' || *p == ';') {
		l->rest = comment(blank);
		return 1;
	}
	const char *q = p;
	while (*q && !isspace((unsigned char)*q))
		q++;
	l->op = xstrndup(p, q - p);
	p = q;
	if (no_operands(l->op)) {
		l->args = xstrdup("");
		l->rest = comment(p);
		return 1;
	}
	while (*p == ' ' || *p == '\t')
		p++;
	/* The operand field: up to a blank outside quotes. */
	q = p;
	char quote = 0;
	while (*q) {
		if (quote) {
			if (*q == '\\' && quote == '"' && q[1])
				q++;
			else if (*q == quote)
				quote = 0;
		} else if (*q == '"' || *q == '\'') {
			quote = *q;
		} else if (isspace((unsigned char)*q)) {
			break;
		}
		q++;
	}
	l->args = xstrndup(p, q - p);
	l->rest = comment(q);
	return 1;
}

static void free_line(struct line *l)
{
	free(l->label);
	free(l->op);
	free(l->args);
	free(l->rest);
}

/* ----- The translation ----- */

struct state {
	int in_psect, in_vsect, remote;
	int used_remote;	/* a vsect remote seen */
	int in_macro, macro_ifs;	/* in a macro definition; its open conditionals */
	int cond_depth;		/* open conditionals outside macro definitions */
	int branch_sizing;	/* -b: r68's -b/-bt */
	int relax_globals;	/* -B: as relaxes branches to global symbols */
	int cpu_level;		/* -m: 0, 10, 20, 30, 40, 60 */
	char **locals;		/* -b: labels defined without ':' (scan_labels) */
	int nlocals;
	char **lmacros;		/* macros whose body uses \Ln: called through r68L */
	int nlmacros;
	const char *rest_of_file;	/* the text after the current line */
	char **sets;		/* the names defined by set (scan_labels) */
	int nsets;
	char **set_done;	/* names set so far outside macro definitions */
	int nset_done;
	char **finals;		/* those referred to before their first set */
	int nfinals;
	char **p1s;		/* finals with a NAME__p1 (their value in r68's first pass) */
	int np1s;
	char **folded;		/* the names of -e (sys.l's equates) */
	char **foldval;		/* and their values */
	int nfolded;
	const char *section;	/* the current section, null if not known */
	char *psect;		/* the psect line's fields */
	char *psect_args;
	struct buf globals;	/* xdef lines for the epilogue */
	int masks;		/* r68__mask<n> symbols made */
	struct buf epilogue_more;
	struct buf macro_syms;	/* NAME equ 0 for macros defined as NAME: (exported) */
	const char *search[64];	/* -u directories */
	int nsearch;
	int errors;
	char **macros;		/* the macros defined so far */
	char **equates;		/* names defined by equ, set, do, lo so far */
	int nequates;
	int nmacros;
	unsigned *macro_fold;	/* per macro: the parameters (bit n-1 for \n) used where
				   equates are folded in (see fold_equates) */
	struct buf *macro_shapes;	/* per macro: its link-time expressions of parameters,
					   lines "K TEXT" (see macro_shape) */
	int nshapes;
	char **gl;		/* the labels defined with ':' (scan_labels) */
	int ngl;
	char **mren_from, **mren_to;	/* macros renamed: as's macro names ignore case */
	int nmren;
	int c32;		/* -d c32: C 3.2's R68's encodings */
	int cpu_given;		/* -m given (r68 without -m pads as with -m1..6, with nop) */
	int rept_fold, rept_state;	/* a rept of one dc line folded: its count; 1: the dc
					   next, 2: the endr */
	struct buf twins;	/* their __r68pc twins made (epilogue lines) */
};

static struct state st;

static int in_macro_def(void)
{
	return st.in_macro;
}

/* sys.l's equates (-e) are left to the linker, as r68 leaves them, where an
 * ELF relocation (symbol + addend) can take them: in a sum whose only
 * symbol is the equate, added. In any other expression (another symbol,
 * the equate subtracted, another operator) and where fold_all is set (a
 * field in the instruction word, a size, a conditional) their values are
 * put in. */
static int folded_index(const char *s, size_t n)
{
	for (int i = 0; i < st.nfolded; i++)
		if (strlen(st.folded[i]) == n && strncmp(st.folded[i], s, n) == 0)
			return i;
	return -1;
}

/* Whether name N (n chars) matches scanned name PAT, where a macro
 * parameter or \@ in PAT (_C\1) stands for any run of name characters. */
static int name_match(const char *pat, const char *n, size_t len)
{
	if (*pat == '\0')
		return len == 0;
	if (pat[0] == '\\' && pat[1] != '\0') {
		for (size_t k = 0; k <= len; k++)
			if (name_match(pat + 2, n + k, len - k))
				return 1;
		return 0;
	}
	return len > 0 && *pat == *n && name_match(pat + 1, n + 1, len - 1);
}

/* Whether name S (n chars) is external: defined nowhere in the source or
 * its use files (scan_labels: labels, equ and set names; a name a macro
 * makes, _C\1, as a pattern) nor so far (equ, set), and no register. r68
 * leaves such a name to the link, also in a sum (ext_tab+TabEnt,
 * chose_continue-bas_add): a link-time expression. Not a name with a dot
 * (written as __dot__, see put_symbol). */
static int external_name(const char *s, size_t n)
{
	static const char *const regs[] = { "sp", "pc", "sr", "ccr", "usp", "zpc", NULL };
	if (memchr(s, '.', n) != NULL)
		return 0;
	if (n == 2 && strchr("dDaA", s[0]) && s[1] >= '0' && s[1] <= '7')
		return 0;
	for (const char *const *r = regs; *r; r++)
		if (strlen(*r) == n && strncasecmp(*r, s, n) == 0)
			return 0;
	char **lists[] = { st.locals, st.gl, st.sets, st.equates };
	int counts[] = { st.nlocals, st.ngl, st.nsets, st.nequates };
	for (int l = 0; l < 4; l++)
		for (int i = 0; i < counts[l]; i++)
			if (name_match(lists[l][i], s, n))
				return 0;
	return 1;
}

/* Link-time expressions (r68x_note.h): an expression of sys.l's equates
 * that one relocation can't take but a sum of them can (Dir_+Write_,
 * PD_PAR-PD_OPT, 4*F$Send, with constants) becomes r68x_<hash>+constant,
 * the definition noted in .r68x, where a relocation takes it (r68x_ok: an
 * instruction's or dc's operand; not equ and set, whose names may be used
 * in fields where they are folded). */
static int r68x_ok;
static struct buf r68x_notes;	/* the .r68x section's lines */
static int lin_params;	/* a macro parameter \1..\9 is a term (macro_shape) */

struct lin {
	struct r68x x;
	long c;
	int ok;
	const char *p;
};

static void lin_binary(struct lin *v, int prec);

static void lin_unary(struct lin *v)
{
	const char *p = v->p;
	if (*p == '-' || *p == '+' || *p == '^' || *p == '~') {
		v->p++;
		lin_unary(v);
		if (*p == '-') {
			v->c = -v->c;
			for (int i = 0; i < v->x.n; i++)
				v->x.t[i].coef = -v->x.t[i].coef;
		} else if (*p != '+') {
			v->c = ~v->c;
			v->ok &= v->x.n == 0;
		}
		return;
	}
	if (*p == '(') {
		v->p++;
		lin_binary(v, P_SHIFT);
		if (*v->p == ')')
			v->p++;
		else
			v->ok = 0;
		return;
	}
	if (lin_params && *p == '\\' && p[1] >= '1' && p[1] <= '9') {
		r68x_add(&v->x, p, 2, 1);
		v->p = p + 2;
		return;
	}
	unsigned long n = 0;
	const char *q = p;
	if (*p == '$' || (*p == '0' && (p[1] == 'x' || p[1] == 'X'))) {
		q = p + (*p == '$' ? 1 : 2);
		for (; isxdigit((unsigned char)*q); q++)
			n = n << 4 | (isdigit((unsigned char)*q) ? *q - '0' : (tolower((unsigned char)*q) - 'a' + 10));
	} else if (*p == '%') {
		for (q = p + 1; *q == '0' || *q == '1'; q++)
			n = n << 1 | (*q - '0');
	} else if (isdigit((unsigned char)*p)) {
		for (; isdigit((unsigned char)*q); q++)
			n = n * 10 + (*q - '0');
	} else if (*p == '\'') {
		for (q = p + 1; *q != '\0'; q++) {
			if (*q == '\'') {
				if (q[1] != '\'')
					break;
				q++;
			}
			n = n << 8 | (unsigned char)*q;
		}
		if (*q != '\'')
			v->ok = 0;
		else
			q++;
	} else if (symstart((unsigned char)*p)) {
		while (symchar((unsigned char)*q))
			q++;
		/* only sys.l's equates and external names: a label, a set
		 * symbol, the location don't go in a note */
		if (folded_index(p, q - p) < 0 && !external_name(p, q - p))
			v->ok = 0;
		else
			r68x_add(&v->x, p, q - p, 1);
		v->p = q;
		return;
	}
	if (q == p || symchar((unsigned char)*q))
		v->ok = 0;
	v->c = (long)(n & 0xffffffffUL);
	v->p = q;
}

static void lin_binary(struct lin *v, int prec)
{
	if (prec > P_LOGIC) {
		lin_unary(v);
		return;
	}
	lin_binary(v, prec + 1);
	int len, p;
	const char *as;
	while (v->ok && (p = binop(v->p, &len, &as)) == prec) {
		struct lin r = { { NULL, 0 }, 0, 1, v->p + len };
		lin_binary(&r, prec + 1);
		v->p = r.p;
		v->ok &= r.ok;
		char o = *as;
		if (o == '+' || o == '-') {
			for (int i = 0; i < r.x.n; i++)
				r68x_add(&v->x, r.x.t[i].sym, strlen(r.x.t[i].sym), o == '-' ? -r.x.t[i].coef : r.x.t[i].coef);
			v->c = o == '-' ? v->c - r.c : v->c + r.c;
		} else if (o == '*' && (v->x.n == 0 || r.x.n == 0)) {
			/* a symbol times a number */
			struct lin *s = v->x.n ? v : &r;
			long k = v->x.n ? r.c : v->c;
			for (int i = 0; i < s->x.n; i++)
				s->x.t[i].coef *= k;
			v->c *= r.c;
			if (s == &r) {
				r68x_free(&v->x);
				v->x = r.x;
				r.x.t = NULL;
				r.x.n = 0;
			}
		} else if (v->x.n == 0 && r.x.n == 0) {
			unsigned long a = v->c, b = r.c;
			if (o == '/' && b == 0)
				v->ok = 0;
			else
				v->c = o == '/' ? (long)a / (long)b : o == '&' ? (long)(a & b) : o == '!' ? (long)(a | b) :
					o == '<' ? (long)(a << (b & 31)) : (long)(a >> (b & 31));
		} else {
			v->ok = 0;
		}
		r68x_free(&r.x);
	}
}

/* Expression text S as a link-time expression: null if it isn't one, or
 * one relocation takes it as it is. */
static char *r68x_expression(const char *s)
{
	struct lin v = { { NULL, 0 }, 0, 1, s };
	lin_binary(&v, P_SHIFT);
	if (!v.ok || *v.p != '\0' || (v.x.n > 0 && !r68x_needed(&v.x))) {
		r68x_free(&v.x);
		return NULL;
	}
	struct buf b = { 0 };
	long c = (long)(int32_t)(v.c & 0xffffffffUL);
	if (v.x.n > 0) {
		char *text = r68x_text(&v.x);
		char name[32];
		r68x_name(text, name);
		struct buf note = { 0 };
		bprintf(&note, "%s=%s\\n", name, text);
		if (r68x_notes.s == NULL || strstr(r68x_notes.s, note.s) == NULL)
			bputs(&r68x_notes, note.s);
		free(note.s);
		free(text);
		bputs(&b, name);
		if (c != 0)
			bputc(&b, c < 0 ? '-' : '+');
	} else if (c < 0) {
		bputc(&b, '-');
	}
	unsigned long a = c < 0 ? -(unsigned long)c : (unsigned long)c;
	if (v.x.n == 0 || c != 0)
		bprintf(&b, a < 10 ? "%lu" : "$%lx", a);
	r68x_free(&v.x);
	return b.s;
}

/* In a macro definition, expression text S with parameters that is a
 * link-time expression whatever names the arguments are (a macro's
 * \1+\1+\1+\1+256*4, 4*\1): .Lr68xm<k> plus the numbers, an alias that
 * each call sets for its arguments before the expansion (macro_aliases;
 * as keeps the value an earlier use saw); <k> numbers the macro's
 * distinct expressions of terms (two lines may share one). Null if
 * it isn't one: another symbol of the source, another operator, one
 * term. */
static char *macro_shape(const char *s)
{
	struct lin v = { { NULL, 0 }, 0, 1, s };
	lin_params = 1;
	lin_binary(&v, P_SHIFT);
	lin_params = 0;
	unsigned mask = 0;
	for (int i = 0; i < v.x.n; i++)
		if (v.x.t[i].sym[0] == '\\')
			mask |= 1u << (v.x.t[i].sym[1] - '1');
	if (!v.ok || *v.p != '\0' || mask == 0 || !r68x_needed(&v.x)) {
		r68x_free(&v.x);
		return NULL;
	}
	char *text = r68x_text(&v.x);
	r68x_free(&v.x);
	struct buf *sh = &st.macro_shapes[st.nmacros - 1];
	int k = 0;
	for (const char *p = sh->s; p != NULL && *p != '\0' && k == 0;) {
		const char *e = strchr(p, '\n');
		int kk, at = 0;
		if (sscanf(p, "%d %n", &kk, &at) == 1 && at > 0 && (size_t)(e - p - at) == strlen(text) &&
		    strncmp(p + at, text, e - p - at) == 0)
			k = kk;
		p = e + 1;
	}
	if (k == 0) {
		k = ++st.nshapes;
		bprintf(sh, "%d %s\n", k, text);
	}
	free(text);
	struct buf b = { 0 };
	bprintf(&b, ".Lr68xm%d", k);
	long c = (long)(int32_t)(v.c & 0xffffffffUL);
	unsigned long a = c < 0 ? -(unsigned long)c : (unsigned long)c;
	if (c != 0)
		bprintf(&b, a < 10 ? "%c%lu" : "%c$%lx", c < 0 ? '-' : '+', a);
	return b.s;
}

/* Expression text S with the folded equates that must be folded replaced by
 * their values; null if nothing is replaced. */
static char *fold_equates(const char *s)
{
	if (st.nfolded == 0 && (!r68x_ok || fold_all))
		return NULL;
	size_t n = strlen(s);
	size_t at[64], len[64];
	int idx[64], nf = 0, nsym = 0, other_op = 0, neg = 0;
	unsigned params = 0;	/* macro parameters \1..\9 in a macro definition */
	char prev = 0;		/* the last significant character, 0 at the start */
	for (size_t i = 0; i < n;) {
		char c = s[i];
		if (c == ' ' || c == '\t') {
			i++;
			continue;
		}
		if (c == '\'' || c == '"') {
			size_t j = i + 1;
			while (j < n && s[j] != c)
				j++;
			i = j < n ? j + 1 : n;
			prev = 'x';
			continue;
		}
		if (c == '\\' && i + 1 < n) {
			/* a macro parameter: a symbol as far as is known */
			nsym++;
			if (s[i + 1] >= '1' && s[i + 1] <= '9') {
				params |= 1u << (s[i + 1] - '1');
				if (prev == '-')
					neg = 1;
			}
			i += 2;
			prev = 'x';
			continue;
		}
		if (isalpha((unsigned char)c) || c == '_' || c == '.' || c == '@') {
			size_t j = i;
			while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '$' || s[j] == '.' || s[j] == '@'))
				j++;
			/* a name this source defined before (equ, set, do, lo) is
			 * a constant, not a second symbol: V_Name+BASE is one
			 * external and a number, a relocation takes it (folding
			 * sys.l's V_Name there was wrong: a source may
			 * define its own, which C 3.2's R68 leaves to the link) */
			int known = 0;
			for (int e = 0; e < st.nequates && !known; e++)
				known = strlen(st.equates[e]) == j - i && strncmp(st.equates[e], s + i, j - i) == 0;
			/* or set anywhere in the source (BASE, set in a use
			 * file after a macro that uses it) */
			for (int e = 0; e < st.nsets && !known; e++)
				known = strlen(st.sets[e]) == j - i && strncmp(st.sets[e], s + i, j - i) == 0;
			if (!known) {
				nsym++;
				/* subtracted (-E$Abort, -M$Exec(a2,d2.l)) */
				if (prev == '-')
					neg = 1;
			}
			int k = folded_index(s + i, j - i);
			if (k >= 0) {
				if (nf < 64) {
					at[nf] = i;
					len[nf] = j - i;
					idx[nf++] = k;
				}
			}
			i = j;
			prev = 'x';
			continue;
		}
		if (isdigit((unsigned char)c) || (c == '$' && i + 1 < n && isxdigit((unsigned char)s[i + 1])) ||
		    (c == '%' && i + 1 < n && (s[i + 1] == '0' || s[i + 1] == '1'))) {
			size_t j = i + 1;
			while (j < n && isalnum((unsigned char)s[j]))
				j++;
			i = j;
			prev = 'x';
			continue;
		}
		if (c == '*' && (prev == 0 || prev == '(' || strchr("+-*/&!|^<>~", prev))) {
			nsym++;		/* the location */
			i++;
			prev = 'x';
			continue;
		}
		if (c != '+' && c != '-')
			other_op = 1;
		prev = c;
		i++;
	}
	if (!(fold_all || nsym > 1 || other_op || neg))
		return NULL;
	/* in a macro definition: a link-time expression of the arguments,
	 * through an alias each call defines */
	if (params && st.in_macro && st.nmacros > 0 && r68x_ok && !fold_all) {
		char *x = macro_shape(s);
		if (x != NULL) {
			if (fold_log != NULL)
				fprintf(fold_log, "%s:%d\tr68xm\t%s\t%s\n", log_path, log_line, s, x);
			annotate = 1;
			return x;
		}
	}
	/* otherwise the arguments for these parameters are folded in where
	 * the macro is called */
	if (params && st.in_macro && st.nmacros > 0 && st.nfolded > 0)
		st.macro_fold[st.nmacros - 1] |= params;
	if (nf == 0) {
		/* no equate of sys.l's: a sum of external names (ext_a+EXT_B,
		 * lab_a-lab_b) or one subtracted (-M$Exec), which no
		 * relocation takes, as a link-time expression too */
		char *x = !fold_all && !params && r68x_ok && (nsym > 1 || (nsym == 1 && (neg || other_op))) ?
			r68x_expression(s) : NULL;
		if (x != NULL) {
			if (fold_log != NULL)
				fprintf(fold_log, "%s:%d\tr68x\t%s\t%s\n", log_path, log_line, s, x);
			annotate = 1;
		}
		return x;
	}
	if (!fold_all && !params && r68x_ok) {
		char *x = r68x_expression(s);
		if (x != NULL) {
			if (fold_log != NULL)
				fprintf(fold_log, "%s:%d\tr68x\t%s\t%s\n", log_path, log_line, s, x);
			annotate = 1;
			return x;
		}
		if (nsym == 1 && !neg) {
			/* one equate, added: a relocation takes it
			 * (T_VctIRQ-(4*4)) */
			struct lin v = { { NULL, 0 }, 0, 1, s };
			lin_binary(&v, P_SHIFT);
			int one = v.ok && *v.p == '\0' && v.x.n == 1 && v.x.t[0].coef == 1;
			r68x_free(&v.x);
			if (one)
				return NULL;
		}
	}
	if (fold_log != NULL)
		/* R68TOGAS_FOLDLOG: file, line, why, the expression */
		fprintf(fold_log, "%s:%d\t%s\t%s\n", log_path, log_line,
			fold_all ? (fold_imm ? "quick" : "context") : nsym > nf ? "other-symbols" : nf > 1 ? "equates" :
			neg ? "negated" : "operator", s);
	struct buf b = { 0 };
	size_t from = 0;
	for (int k = 0; k < nf; k++) {
		bput(&b, s + from, at[k] - from);
		/* the value as short as it goes: $00000080 as $80 */
		const char *v = st.foldval[idx[k]];
		if (v[0] == '$') {
			const char *d = v + 1;
			while (d[0] == '0' && d[1] != '\0')
				d++;
			bprintf(&b, "$%s", d);
		} else {
			bprintf(&b, "(%s)", v);
		}
		from = at[k] + len[k];
	}
	annotate = 1;
	bputs(&b, s + from);
	return b.s;
}

static void add_global_marked(const char *name)
{
	struct buf b = { 0 };
	put_symbol(&b, name, strlen(name));
	/* exported if the line was assembled: r68glab sets r68__g_NAME */
	bprintf(&st.globals, " ifdef r68__g_%s\n xdef %s\n endc\n", b.s, b.s);
	free(b.s);
}

static void add_global(const char *name)
{
	struct buf b = { 0 };
	put_symbol(&b, name, strlen(name));
	/* Each once. */
	if (st.globals.s != NULL) {
		char *needle = xmalloc(b.n + 16);
		sprintf(needle, " xdef %s\n", b.s);
		int found = strstr(st.globals.s, needle) != NULL;
		free(needle);
		if (found) {
			free(b.s);
			return;
		}
	}
	/* Only if defined: a label in a false conditional stays undefined. */
	bprintf(&st.globals, " ifdef %s\n xdef %s\n endc\n", b.s, b.s);
	free(b.s);
}

/* Whether the current section is SECT. */
static int same_section(const char *sect)
{
	return st.section != NULL && strcmp(st.section, sect) == 0;
}

static const char *data_section(int initialized)
{
	if (st.remote)
		return initialized ? ".remote.data" : ".remote.bss";
	return initialized ? ".data" : ".bss";
}

/* The operator's base name (lower case) and size letter (0 if none). */
static void op_parts(const char *op, char *base, size_t size, char *suffix)
{
	size_t i;
	for (i = 0; op[i] && op[i] != '.' && i + 1 < size; i++)
		base[i] = tolower((unsigned char)op[i]);
	base[i] = '\0';
	*suffix = op[i] == '.' && op[i + 1] ? tolower((unsigned char)op[i + 1]) : 0;
}

/* Expression S (N chars) with each * meaning the location (not a
 * multiplication) replaced by (*-OFFSET): in r68 a dc list's * is the
 * statement's start, in as the element's; null if it has none. */
static char *location_from(const char *s, size_t n, long offset)
{
	struct buf b = { 0 };
	char prev = 0;
	int any = 0;
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		if (c == '\'' || c == '"') {
			size_t j = i + 1;
			while (j < n && s[j] != c)
				j++;
			bput(&b, s + i, (j < n ? j + 1 : n) - i);
			i = j < n ? j : n;
			prev = 'x';
			continue;
		}
		if (c == '*' && (prev == 0 || prev == '(' || strchr("+-*/&!|^<>~", prev))) {
			bprintf(&b, "(*-%ld)", offset);
			any = 1;
			prev = 'x';
			continue;
		}
		bputc(&b, c);
		if (c != ' ' && c != '\t')
			prev = isalnum((unsigned char)c) || c == '_' || c == '$' || c == '.' || c == '@' ? 'x' : c;
	}
	if (!any) {
		free(b.s);
		return NULL;
	}
	return b.s;
}

/* Expressions separated by commas (dc, the if directives' operands...). */
/* Whether S (N chars) is one quoted token: its closing quote (a doubled
 * one is a quote inside) is its last character, blanks aside. */
static int whole_quoted(const char *s, size_t n)
{
	char q = s[0];
	size_t i = 1;
	while (i < n) {
		if (s[i] == q) {
			if (i + 1 < n && s[i + 1] == q) {
				i += 2;
				continue;
			}
			break;
		}
		if (s[i] == '\\' && q == '"' && i + 1 < n)
			i++;
		i++;
	}
	if (i >= n)
		return 1;	/* unterminated: as a string, as before */
	for (i++; i < n; i++)
		if (s[i] != ' ' && s[i] != '\t')
			return 0;
	return 1;
}

static void expression_list(struct buf *b, const char *args, int chars)
{
	/* as many elements as there are (a folded rept's list is long) */
	int max = 1;
	for (const char *c = args; *c; c++)
		max += *c == ',';
	const char **pieces = xmalloc(max * sizeof *pieces);
	size_t *lens = xmalloc(max * sizeof *lens);
	int k = split_commas(args, strlen(args), pieces, lens, max);
	expr_chars = chars;
	int written = 0;
	long offset = 0;	/* bytes before this element (dc lists) */
	for (int i = 0; i < k; i++) {
		/* An empty string "" adds nothing (and '' is a quote in MRI). */
		if (lens[i] == 2 && pieces[i][0] == '"' && pieces[i][1] == '"' && k > 1)
			continue;
		if (written++)
			bputc(b, ',');
		/* a string only if the element is one quoted token: ' '+12 is a
		 * value (dc.b ...,27,"Y",' '+12,' '+58) */
		if (lens[i] > 0 && (pieces[i][0] == '"' || (pieces[i][0] == '\'' && !chars)) &&
		    whole_quoted(pieces[i], lens[i])) {
			string_operand(b, pieces[i], lens[i]);
			offset = -1;	/* a string's length: not counted */
		} else {
			int save = fold_all;
			if (i == 0 && fold_first)
				fold_all = 1;
			char *loc = dc_size && offset > 0 ? location_from(pieces[i], lens[i], offset) : NULL;
			if (loc) {
				expression(b, loc, strlen(loc));
				free(loc);
				annotate = 1;
			} else {
				expression(b, pieces[i], lens[i]);
			}
			fold_all = save;
			if (offset >= 0)
				offset += dc_size;
		}
	}
	expr_chars = 0;
	free(pieces);
	free(lens);
}

static void translate_file(const char *path, FILE *out, int depth);

/* Whether name S (N chars), in an expression, is a set symbol not set yet
 * (outside macro bodies, which expand later): r68 takes its final value
 * there, as must as (NAME__final, defined at the end). */
static int forward_set(const char *s, size_t n)
{
	if (st.in_macro)
		return 0;
	int is_set = 0;
	for (int i = 0; i < st.nsets && !is_set; i++)
		is_set = strlen(st.sets[i]) == n && strncmp(st.sets[i], s, n) == 0;
	if (!is_set)
		return 0;
	for (int i = 0; i < st.nset_done; i++)
		if (strlen(st.set_done[i]) == n && strncmp(st.set_done[i], s, n) == 0)
			return 0;
	for (int i = 0; i < st.nfinals; i++)
		if (strlen(st.finals[i]) == n && strncmp(st.finals[i], s, n) == 0)
			return 1;
	st.finals = xrealloc(st.finals, (st.nfinals + 1) * sizeof *st.finals);
	st.finals[st.nfinals++] = xstrndup(s, n);
	return 1;
}

static int name_in(char **list, int n, const char *s, size_t len)
{
	for (int i = 0; i < n; i++)
		if (strlen(list[i]) == len && strncmp(list[i], s, len) == 0)
			return 1;
	return 0;
}

/* In p1_mode, name S (N chars) as r68's first pass sees it, written to B:
 * a set symbol not set yet is 0, one with a NAME__p1 that; 0 for the
 * others (written as usual). */
static int p1_name(struct buf *b, const char *s, size_t n)
{
	if (name_in(st.sets, st.nsets, s, n) && !name_in(st.set_done, st.nset_done, s, n)) {
		bputs(b, "0");
		return 1;
	}
	if (name_in(st.p1s, st.np1s, s, n)) {
		put_symbol(b, s, n);
		bputs(b, "__p1");
		return 1;
	}
	return 0;
}

/* LABEL set ARGS (outside macro definitions) of a symbol referred to
 * before its first set: r68 took the value it had at the end of its first
 * pass there, where set symbols not set yet were 0 (X set Y with Y set
 * later: 0, not Y's value). Its first-pass value LABEL__p1, set on a line
 * before (line_before), which NAME__final takes at the end. */
static void p1_set(const char *label, const char *args)
{
	if (!name_in(st.finals, st.nfinals, label, strlen(label)))
		return;
	struct buf pre = { 0 };
	put_symbol(&pre, label, strlen(label));
	bputs(&pre, "__p1 set ");
	p1_mode = 1;
	expression(&pre, args, strlen(args));
	p1_mode = 0;
	bprintf(&pre, "\n# %d \"%s\"\n", log_line, log_path);
	bput(&line_before, pre.s, pre.n);
	free(pre.s);
	if (!name_in(st.p1s, st.np1s, label, strlen(label))) {
		st.p1s = xrealloc(st.p1s, (st.np1s + 1) * sizeof *st.p1s);
		st.p1s[st.np1s++] = xstrdup(label);
	}
}

/* Whether the macro body in TEXT (up to its endm line) uses \Ln. */
static int uses_length(const char *text)
{
	if (text == NULL)
		return 0;
	for (const char *p = text; *p;) {
		const char *e = p + strcspn(p, "\r\n");
		const char *w = p;
		if (!isspace((unsigned char)*w))
			while (w < e && !isspace((unsigned char)*w))
				w++;
		while (w < e && isspace((unsigned char)*w))
			w++;
		if (e - w >= 4 && strncasecmp(w, "endm", 4) == 0 && (e - w == 4 || isspace((unsigned char)w[4])))
			return 0;
		for (const char *c = p; c + 2 < e; c++)
			if (c[0] == '\\' && (c[1] == 'L' || c[1] == 'l') && isdigit((unsigned char)c[2]))
				return 1;
		p = e;
		while (*p == '\r' || *p == '\n')
			p++;
	}
	return 0;
}

/* strcasestr, which isn't everywhere */
static const char *strcasestr_(const char *s, const char *t)
{
	size_t n = strlen(t);
	for (; *s; s++)
		if (strncasecmp(s, t, n) == 0)
			return s;
	return NULL;
}

/* The CPU an instruction needs (10 for the 68010 ... 60), 0 for any:
 * r68 takes every instruction whatever its -m, as doesn't. */
static int cpu_needed(const char *base, char size, const char *args)
{
	static const struct { const char *name; int level; } table[] = {
		{ "movec", 10 }, { "moves", 10 }, { "rtd", 10 }, { "bkpt", 10 },
		{ "extb", 20 }, { "cas", 20 }, { "cas2", 20 }, { "chk2", 20 }, { "cmp2", 20 }, { "pack", 20 },
		{ "unpk", 20 }, { "callm", 20 }, { "rtm", 20 }, { "bfchg", 20 }, { "bfclr", 20 }, { "bfexts", 20 },
		{ "bfextu", 20 }, { "bfffo", 20 }, { "bfins", 20 }, { "bfset", 20 }, { "bftst", 20 }, { "divsl", 20 },
		{ "divul", 20 }, { "pmove", 30 }, { "pflush", 30 }, { "pflusha", 30 }, { "ploadr", 30 },
		{ "ploadw", 30 }, { "ptestr", 30 }, { "ptestw", 30 }, { "pmovefd", 30 }, { "cinva", 40 },
		{ "cinvl", 40 }, { "cinvp", 40 }, { "cpusha", 40 }, { "cpushl", 40 }, { "cpushp", 40 },
		{ "move16", 40 }, { "pflushn", 40 }, { "pflushan", 40 }, { "ptest", 40 }, { "plpar", 60 },
		{ "plpaw", 60 }, { "lpstop", 60 }, { NULL, 0 },
	};
	int level = 0;
	for (int i = 0; table[i].name; i++)
		if (strcmp(base, table[i].name) == 0)
			level = table[i].level;
	if ((strcmp(base, "muls") == 0 || strcmp(base, "mulu") == 0 || strcmp(base, "divs") == 0 ||
	     strcmp(base, "divu") == 0 || strcmp(base, "link") == 0) && size == 'l')
		level = 20;
	if (strncmp(base, "trap", 4) == 0 && base[4] != '\0' && strcmp(base, "trapv") != 0)
		level = 20;
	/* movec's 68020 and 68040 control registers */
	if (strcmp(base, "movec") == 0) {
		static const struct { const char *reg; int level; } regs[] = {
			{ "cacr", 20 }, { "caar", 20 }, { "msp", 20 }, { "isp", 20 }, { "itt0", 40 }, { "itt1", 40 },
			{ "dtt0", 40 }, { "dtt1", 40 }, { "urp", 40 }, { "srp", 40 }, { "tc", 40 }, { "mmusr", 40 },
			{ NULL, 0 },
		};
		for (int i = 0; regs[i].reg; i++) {
			size_t rn = strlen(regs[i].reg);
			for (const char *f = args; (f = strcasestr_(f, regs[i].reg)) != NULL; f += rn)
				if ((f == args || !isalnum((unsigned char)f[-1])) && !isalnum((unsigned char)f[rn]) &&
				    regs[i].level > level)
					level = regs[i].level;
		}
	}
	return level > st.cpu_level ? level : 0;
}

static const char *chip_name(int level)
{
	switch (level) {
	case 0: return "68000/68881";
	case 10: return "68010/68881";
	case 20: return "68020/68881";
	case 30: return "68030/68881";
	case 40: return "68040";
	default: return "68060";
	}
}

static int is_lmacro(const char *op)
{
	for (int i = 0; i < st.nlmacros; i++)
		if (ci_eq(st.lmacros[i], op))
			return 1;
	return 0;
}

/* Whether TARGET (a branch operand) is a label this source defines
 * without ':' (scan_labels), or one made in a macro (\@). */
/* A label this source defines with ':' (global). */
static int global_label(const char *s, size_t n)
{
	if (memchr(s, '\\', n) != NULL)		/* a macro's name\@: not one symbol */
		return 0;
	for (int i = 0; i < st.ngl; i++)
		if (strlen(st.gl[i]) == n && strncmp(st.gl[i], s, n) == 0)
			return 1;
	return 0;
}

/* A global label of this source in a PC-relative operand or as a branch
 * target: r68 resolves it in the file, as resolves a global symbol only at
 * link time (a relocation; an error in an 8-bit field, as (pc,Xn) and .s
 * branches have). Written as NAME__r68pc, a local equate of NAME (epilogue),
 * which as resolves. */
static void put_twin(struct buf *b, const char *s, size_t n)
{
	struct buf t = { 0 };
	put_symbol(&t, s, n);
	bputs(&t, "__r68pc");
	bputs(b, t.s);
	char needle[600];
	snprintf(needle, sizeof needle, "\n%s equ ", t.s);
	if (st.twins.s == NULL || strstr(st.twins.s, needle) == NULL) {
		struct buf nm = { 0 };
		put_symbol(&nm, s, n);
		bprintf(&st.twins, "\n%s equ %s", t.s, nm.s);
		free(nm.s);
	}
	free(t.s);
}

static int local_label(const char *target)
{
	if (strchr(target, '\\') != NULL)
		return 1;
	for (int i = 0; i < st.nlocals; i++)
		if (strcmp(st.locals[i], target) == 0)
			return 1;
	return global_label(target, strlen(target));
}

static char *use_file(const char *arg, const char *from);

/* PATH's text into TEXT; 0 if it can't be read. A Ctrl-Z ends it (DOS's
 * end of file, after the last line of sources edited on DOS; r68 stops
 * there). */
static int read_text(const char *path, struct buf *text)
{
	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return 0;
	char chunk[8192];
	size_t n;
	while ((n = fread(chunk, 1, sizeof chunk, fp)) > 0)
		bput(text, chunk, n);
	fclose(fp);
	const char *z = text->n ? memchr(text->s, 0x1A, text->n) : NULL;
	if (z != NULL) {
		text->n = (size_t)(z - text->s);
		text->s[text->n] = '\0';
	}
	return 1;
}

/* The global labels macro calls make (LABMAC Fail, whose body has
 * \1: bsr.s past\1), which -b and the twins need to know as the source's
 * own (macro-label-pcrel). From a line of the pre-scan, its LABEL (null if
 * none; GLOBAL if with ':') and its statement from the operation, OP: a
 * macro's global labels made of parameters noted, a call outside macro
 * definitions giving its arguments' names. */
static void scan_macro_labels(const char *label, int global, const char *op, char ***globals, int *nglobals)
{
	static struct gmacro { char *name; char *pat[8]; int npat; } *gm;
	static int ngm, cur = -1;
	size_t n = strcspn(op, " \t");
	if (n == 5 && strncasecmp(op, "macro", 5) == 0) {
		cur = -1;
		if (label == NULL)
			return;
		gm = xrealloc(gm, (ngm + 1) * sizeof *gm);
		gm[ngm] = (struct gmacro){ xstrdup(label), { NULL }, 0 };
		cur = ngm++;
		return;
	}
	if (n == 4 && strncasecmp(op, "endm", 4) == 0) {
		cur = -1;
		return;
	}
	if (cur >= 0) {
		if (label && global && strchr(label, '\\') && strstr(label, "\\@") == NULL && gm[cur].npat < 8)
			gm[cur].pat[gm[cur].npat++] = xstrdup(label);
		return;
	}
	if (n == 0)
		return;
	int m = -1;
	for (int i = 0; i < ngm; i++)
		if (gm[i].npat > 0 && strlen(gm[i].name) == n && strncasecmp(gm[i].name, op, n) == 0)
			m = i;
	if (m < 0)
		return;
	const char *a = op + n;
	while (*a == ' ' || *a == '\t')
		a++;
	size_t alen = strcspn(a, " \t");
	const char *args[9] = { NULL };
	size_t lens[9] = { 0 };
	for (int k = 0; k < 9 && alen > 0; k++) {
		size_t c = strcspn(a, ",");
		if (c > alen)
			c = alen;
		args[k] = a;
		lens[k] = c;
		if (c == alen)
			break;
		a += c + 1;
		alen -= c + 1;
	}
	for (int i = 0; i < gm[m].npat; i++) {
		struct buf nm = { 0 };
		for (const char *q = gm[m].pat[i]; *q; q++) {
			if (q[0] == '\\' && q[1] >= '1' && q[1] <= '9') {
				int k = q[1] - '1';
				if (args[k])
					bput(&nm, args[k], lens[k]);
				q++;
			} else {
				bputc(&nm, *q);
			}
		}
		if (nm.s && *nm.s) {
			*globals = xrealloc(*globals, (*nglobals + 1) * sizeof **globals);
			(*globals)[(*nglobals)++] = nm.s;
		} else {
			free(nm.s);
		}
	}
}

/* Collects the labels PATH and the files it uses define without ':' (for
 * -b), conditionals not considered; a name also defined with ':' is left
 * out (global). */
static void scan_labels(const char *path, int depth, char ***globals, int *nglobals)
{
	if (depth > 16)
		return;
	struct buf text = { 0 };
	if (!read_text(path, &text))
		return;
	/* lines end in CR, LF or CR LF (OS-9's own files in CR) */
	for (size_t i = 0, j; i < text.n; i = j + (j < text.n && text.s[j] == '\r' && j + 1 < text.n && text.s[j + 1] == '\n') + 1) {
		j = i;
		while (j < text.n && text.s[j] != '\r' && text.s[j] != '\n')
			j++;
		char *line = xstrndup(text.s + i, j - i);
		char *p = line;
		if (*p == '\0' || *p == '*' || *p == ';') {
			free(line);
			continue;
		}
		char *label = NULL;
		int label_global = 0;
		if (!isspace((unsigned char)*p)) {
			if (*p == '=')
				p++;
			size_t n = 0;
			while (p[n] && !isspace((unsigned char)p[n]) && p[n] != ':')
				n++;
			{
				/* defined by set? */
				const char *o = p + n + (p[n] == ':');
				while (*o == ' ' || *o == '\t')
					o++;
				if (strncasecmp(o, "set", 3) == 0 && (o[3] == '\0' || isspace((unsigned char)o[3]))) {
					st.sets = xrealloc(st.sets, (st.nsets + 1) * sizeof *st.sets);
					st.sets[st.nsets++] = xstrndup(p, n);
				}
			}
			label = xstrndup(p, n);
			label_global = p[n] == ':';
			char **list = p[n] == ':' ? *globals : st.locals;
			int *count = p[n] == ':' ? nglobals : &st.nlocals;
			list = xrealloc(list, (*count + 1) * sizeof *list);
			list[(*count)++] = xstrndup(p, n);
			if (p[n] == ':')
				*globals = list;
			else
				st.locals = list;
			p += n;
		}
		while (*p && (isspace((unsigned char)*p) || *p == ':'))
			p++;
		scan_macro_labels(label, label_global, p, globals, nglobals);
		free(label);
		if (strncasecmp(p, "use", 3) == 0 && isspace((unsigned char)p[3])) {
			char *a = p + 3;
			while (isspace((unsigned char)*a))
				a++;
			a[strcspn(a, " \t")] = '\0';
			char *f = use_file(a, path);
			if (f != NULL)
				scan_labels(f, depth + 1, globals, nglobals);
			free(f);
		}
		free(line);
	}
	free(text.s);
}

/* Whether BASE (lower case) is one of r68's conditionals: not any name
 * beginning with "if" (a macro if.set, called as a statement). */
static int is_cond(const char *base)
{
	static const char *const conds[] = { "ifeq", "ifne", "iflt", "ifle", "ifgt", "ifge", "ifdef", "ifndef",
		"ifp1", NULL };
	for (const char *const *c = conds; *c; c++)
		if (strcmp(base, *c) == 0)
			return 1;
	return 0;
}

/* Whether BASE (lower case) is bra, bsr or a b<cc>. */
static int is_branch(const char *base)
{
	static const char *const cc[] = { "ra", "sr", "hi", "ls", "cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi", "ge",
		"lt", "gt", "le", "hs", "lo", NULL };
	if (base[0] != 'b' || strlen(base) != 3)
		return 0;
	for (const char *const *c = cc; *c; c++)
		if (strcmp(base + 1, *c) == 0)
			return 1;
	return 0;
}

static void add_equate(const char *name)
{
	st.equates = xrealloc(st.equates, (st.nequates + 1) * sizeof *st.equates);
	st.equates[st.nequates++] = xstrdup(name);
}

/* The values of equates whose expressions are constants (for rept counts):
 * a small evaluator of numbers, known names, + - * / and parentheses. */
static char **cv_names;
static long *cv_values;
static int ncv;

static int cv_expr(const char **p, long *v);

static int cv_unary(const char **p, long *v)
{
	while (**p == ' ' || **p == '\t')
		(*p)++;
	if (**p == '-') {
		(*p)++;
		if (!cv_unary(p, v))
			return 0;
		*v = -*v;
		return 1;
	}
	if (**p == '(') {
		(*p)++;
		if (!cv_expr(p, v) || **p != ')')
			return 0;
		(*p)++;
		return 1;
	}
	char *e;
	if (**p == '$') {
		*v = strtol(*p + 1, &e, 16);
		if (e == *p + 1)
			return 0;
		*p = e;
		return 1;
	}
	if (isdigit((unsigned char)**p)) {
		*v = strtol(*p, &e, 10);
		*p = e;
		return 1;
	}
	if (symstart((unsigned char)**p)) {
		const char *q = *p;
		while (symchar((unsigned char)*q))
			q++;
		for (int i = ncv - 1; i >= 0; i--)
			if (strlen(cv_names[i]) == (size_t)(q - *p) && strncmp(cv_names[i], *p, q - *p) == 0) {
				*v = cv_values[i];
				*p = q;
				return 1;
			}
	}
	return 0;
}

static int cv_term(const char **p, long *v)
{
	if (!cv_unary(p, v))
		return 0;
	for (;;) {
		while (**p == ' ' || **p == '\t')
			(*p)++;
		char op = **p;
		if (op != '*' && op != '/')
			return 1;
		(*p)++;
		long w;
		if (!cv_unary(p, &w) || (op == '/' && w == 0))
			return 0;
		*v = op == '*' ? *v * w : *v / w;
	}
}

static int cv_expr(const char **p, long *v)
{
	if (!cv_term(p, v))
		return 0;
	for (;;) {
		while (**p == ' ' || **p == '\t')
			(*p)++;
		char op = **p;
		if (op != '+' && op != '-')
			return 1;
		(*p)++;
		long w;
		if (!cv_term(p, &w))
			return 0;
		*v = op == '+' ? *v + w : *v - w;
	}
}

/* Notes the value of NAME equ ARGS if it is a constant. */
static void note_value(const char *name, const char *args)
{
	const char *p = args;
	long v;
	if (!cv_expr(&p, &v))
		return;
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p != '\0')
		return;
	cv_names = xrealloc(cv_names, (ncv + 1) * sizeof *cv_names);
	cv_values = xrealloc(cv_values, (ncv + 1) * sizeof *cv_values);
	cv_names[ncv] = xstrdup(name);
	cv_values[ncv++] = v;
}

/* The value of expression S if it is a constant (numbers, known equates). */
static int constant_value(const char *s, long *v)
{
	const char *p = s;
	if (!cv_expr(&p, v))
		return 0;
	while (*p == ' ' || *p == '\t')
		p++;
	return *p == '\0';
}

/* Whether expression S (N chars) has only numbers and names equated so
 * far, so that as can compute it (and mask it: r68 writes a move.b
 * immediate as its low byte, $00nn, as the whole value, $FF7F for ^$80). */
/* Whether expression S (N chars) uses the location counter: a * where an
 * operand starts (not a multiplication). */
static int has_location(const char *s, size_t n)
{
	char prev = 0;
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '\'') {
			for (i++; i < n && s[i] != '\''; i++)
				;
			prev = 'x';
			continue;
		}
		if (s[i] == ' ' || s[i] == '\t')
			continue;
		if (s[i] == '*' && (prev == 0 || strchr("+-*/(&!|^<>~", prev)))
			return 1;
		prev = s[i];
	}
	return 0;
}

/* Whether S (N chars) is a plain number from -128 to 127 (decimal, $hex,
 * %binary, with a minus). */
static int small_number(const char *s, size_t n)
{
	char t[64];
	if (n == 0 || n >= sizeof t)
		return 0;
	memcpy(t, s, n);
	t[n] = '\0';
	char *p = t, *e;
	int neg = *p == '-';
	if (neg)
		p++;
	int radix = *p == '$' ? 16 : *p == '%' ? 2 : 10;
	if (radix != 10)
		p++;
	if (!isxdigit((unsigned char)*p))
		return 0;
	long v = strtol(p, &e, radix);
	if (*e != '\0')
		return 0;
	if (neg)
		v = -v;
	return v >= -128 && v <= 127;
}

static int known_value(const char *s, size_t n)
{
	for (size_t i = 0; i < n;) {
		if (s[i] == '$' || s[i] == '%' || isdigit((unsigned char)s[i])) {
			i++;
			while (i < n && isalnum((unsigned char)s[i]))
				i++;
		} else if (s[i] == '\'') {
			for (i++; i < n && s[i] != '\''; i++)
				;
			i++;
		} else if (symstart((unsigned char)s[i])) {
			size_t j = i;
			while (j < n && symchar((unsigned char)s[j]))
				j++;
			int found = 0;
			for (int k = 0; k < st.nequates && !found; k++)
				found = strlen(st.equates[k]) == j - i && strncmp(st.equates[k], s + i, j - i) == 0;
			if (!found)
				return 0;
			i = j;
		} else if (s[i] == '*' && (i == 0 || strchr("+-*/(&!|^<>", s[i - 1]))) {
			return 0;	/* the location counter */
		} else {
			i++;
		}
	}
	return 1;
}

/* Text S (N chars) for r68error's <...> argument and .error's "...": no
 * angle brackets or double quotes. */
static void error_text(struct buf *b, const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++)
		bputc(b, s[i] == '<' ? '(' : s[i] == '>' ? ')' : s[i] == '"' ? '\'' : s[i]);
}

/* PATH if it exists, found with each component's case ignored (a
 * directory in upper case, named in lower case); null if not. */
static char *find_ci(const char *path)
{
	FILE *t = fopen(path, "rb");
	if (t != NULL) {
		fclose(t);
		return xstrdup(path);
	}
	struct buf done = { 0 };
	const char *p = path;
	if (*p == '/') {
		bputc(&done, '/');
		p++;
	}
	while (*p) {
		const char *slash = strchr(p, '/');
		size_t n = slash ? (size_t)(slash - p) : strlen(p);
		DIR *d = opendir(done.n ? done.s : ".");
		struct dirent *e;
		char *match = NULL;
		while (d != NULL && (e = readdir(d)) != NULL)
			if (strlen(e->d_name) == n && strncasecmp(e->d_name, p, n) == 0) {
				match = xstrdup(e->d_name);
				break;
			}
		if (d != NULL)
			closedir(d);
		if (match == NULL) {
			free(done.s);
			return NULL;
		}
		if (done.n && done.s[done.n - 1] != '/')
			bputc(&done, '/');
		bputs(&done, match);
		free(match);
		p += n;
		while (*p == '/')
			p++;
	}
	t = done.s ? fopen(done.s, "rb") : NULL;
	if (t == NULL) {
		free(done.s);
		return NULL;
	}
	fclose(t);
	return done.s;
}

static char *join(const char *dir, size_t n, const char *name)
{
	char *r = xmalloc(n + strlen(name) + 2);
	sprintf(r, "%.*s/%s", (int)n, dir, name);
	return r;
}

/* NAME with OS-9's multi-dot path components (... the parent's parent,
 * .... one more up) as ../ ones; NAME itself freed. */
static char *os9_dots(char *name)
{
	struct buf b = { 0 };
	const char *p = name;
	while (*p) {
		const char *e = strchr(p, '/');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		size_t dots = strspn(p, ".");
		if (dots == n && n >= 3) {
			for (size_t i = 1; i < n; i++)
				bprintf(&b, i > 1 ? "/.." : "..");
		} else {
			bprintf(&b, "%.*s", (int)n, p);
		}
		p += n;
		if (*p == '/') {
			bprintf(&b, "/");
			p++;
		}
	}
	free(name);
	return b.s ? b.s : xstrdup("");
}

/* The file of "use ARG" in the file FROM, by r68's rules: use x from the
 * working directory, use "x" from FROM's directory, use <x> from the -u
 * directories, then $MWOS/OS9/SRC/DEFS (Ultra C's r68 has \mwos there);
 * the others are tried after these, and names are matched ignoring case. A
 * DOS path's backslashes are slashes. Null if not found. */
static char *use_file(const char *arg, const char *from)
{
	size_t n = strlen(arg);
	char kind = 0;
	char *name;
	if (n >= 2 && ((arg[0] == '"' && arg[n - 1] == '"') || (arg[0] == '<' && strchr(">)", arg[n - 1])))) {
		/* r68 takes <name) too. */
		kind = arg[0];
		name = xstrndup(arg + 1, n - 2);
	} else {
		name = xstrdup(arg);
	}
	for (char *c = name; *c; c++)
		if (*c == '\\')
			*c = '/';
	/* OS-9's own r68 had .../x from the OS-9 path syntax (Ultra C's
	 * r68.exe hasn't). */
	name = os9_dots(name);
	char *found = NULL;
	if (isalpha((unsigned char)name[0]) && name[1] == ':' && name[2] == '/') {
		/* A DOS path (use c:\os9\defs\oskdefs.d): the drive as
		 * WSL mounts it (/mnt/c), else as MSYS (/c). */
		char d = (char)tolower((unsigned char)name[0]);
		for (int k = 0; k < 2 && found == NULL; k++) {
			struct buf p = { 0 };
			bprintf(&p, k == 0 ? "/mnt/%c%s" : "/%c%s", d, name + 2);
			found = find_ci(p.s);
			free(p.s);
		}
		free(name);
		return found;
	}
	if (name[0] == '/') {
		found = find_ci(name);
		free(name);
		return found;
	}
	const char *slash = strrchr(from, '/');
	char *local = slash ? join(from, slash - from, name) : xstrdup(name);
	const char *mwos = getenv("MWOS");
	char *defs = mwos ? join(mwos, strlen(mwos), "OS9/SRC/DEFS") : NULL;
	/* In the order of the kind, then the rest. */
	const char *order[3];
	order[0] = kind == '"' ? "source" : kind == '<' ? "search" : "cwd";
	order[1] = kind == '"' ? "cwd" : "source";
	order[2] = kind == '<' ? "cwd" : "search";
	for (int o = 0; o < 3 && found == NULL; o++) {
		if (strcmp(order[o], "cwd") == 0) {
			found = find_ci(name);
		} else if (strcmp(order[o], "source") == 0) {
			found = find_ci(local);
		} else {
			for (int i = 0; i < st.nsearch && found == NULL; i++) {
				char *f = join(st.search[i], strlen(st.search[i]), name);
				found = find_ci(f);
				free(f);
			}
			if (found == NULL && defs != NULL) {
				char *f = join(defs, strlen(defs), name);
				found = find_ci(f);
				free(f);
			}
		}
	}
	free(local);
	free(defs);
	free(name);
	return found;
}

/* Whether OP calls a macro defined so far (names in any case, as r68's
 * mnemonics). */
/* The name a macro was defined with, for OP: a renamed macro's call (see
 * the macro statement: a dotted name, a case clash) has its new name. */
static const char *macro_orig(const char *op)
{
	for (int i = 0; i < st.nmren; i++)
		if (strcmp(op, st.mren_to[i]) == 0)
			return st.mren_from[i];
	return op;
}

static int is_macro(const char *op)
{
	for (int i = 0; i < st.nmacros; i++)
		if (ci_eq(st.macros[i], op) || ci_eq(st.macros[i], macro_orig(op)))
			return 1;
	return 0;
}

/* A macro call's arguments: a quoted one passes its text without the
 * quotes in r68, as <...> does in MRI; the others as operands. */
/* S as a macro argument in <...>: as's MRI macros take ! there as an escape,
 * and count < and > ("HELLO!", a shift): those escaped. */
static void put_angle(struct buf *b, const char *s, size_t n)
{
	bputc(b, '<');
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '!' || s[i] == '<' || s[i] == '>')
			bputc(b, '!');
		bputc(b, s[i]);
	}
	bputc(b, '>');
}


static void macro_arguments(struct buf *b, const char *args, const char *name)
{
	const char *pieces[64];
	size_t lens[64];
	int k = split_commas(args, strlen(args), pieces, lens, 64);
	unsigned fold = 0;
	for (int i = 0; i < st.nmacros; i++)
		if (ci_eq(st.macros[i], name) || ci_eq(st.macros[i], macro_orig(name)))
			fold = st.macro_fold[i];
	int save_ok = r68x_ok;
	r68x_ok = 0;	/* folded as before: see r68x_ok */
	for (int i = 0; i < k; i++) {
		int save = fold_all;
		if (i < 9 && (fold & (1u << i)))
			fold_all = 1;
		if (i)
			bputc(b, ',');
		if (lens[i] >= 2 && pieces[i][0] == '"' && pieces[i][lens[i] - 1] == '"') {
			put_angle(b, pieces[i] + 1, lens[i] - 2);
		} else if (lens[i] >= 4 && (strncasecmp(pieces[i], "addi", 4) == 0 || strncasecmp(pieces[i], "subi", 4) == 0) &&
			   (lens[i] == 4 || pieces[i][4] == '.')) {
			/* A mnemonic argument (DTDEST subi.w,#1,V_Counter): as in
			 * an instruction, add/sub for r68's quick forms. */
			bput(b, pieces[i], 3);
			bput(b, pieces[i] + 4, lens[i] - 4);
			annotate = 1;
		} else if (lens[i] >= 3 && pieces[i][lens[i] - 2] == '.' && strchr("bwlsdxpBWLSDXP", pieces[i][lens[i] - 1]) &&
			   strspn(pieces[i], "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") == lens[i] - 2) {
			/* a mnemonic with its size (MAC tst.b,V_Mine): as it is,
			 * not a name with a dot (see put_symbol) */
			bput(b, pieces[i], lens[i]);
		} else {
			operand(b, pieces[i], lens[i], 0);
		}
		fold_all = save;
	}
	r68x_ok = save_ok;
}

/* A call of macro NAME with ARGS: for each of the macro's link-time
 * expressions of parameters (macro_shape), a line before the call
 * (line_before) setting its alias .Lr68xm<k> to the expression for these
 * arguments, a link-time expression where it is one (a system call macro with F$Sleep:
 * .Lr68xm1 set r68x_3fbf59ac, 4*F$Sleep), else the expression itself (a
 * number: mac 0,0,-1). A linemarker after it keeps the call's line
 * number; not in a macro definition, where an argument may be a parameter
 * \n of the macro defined, the expression one of that macro's in turn
 * (macro_shape). */
static void macro_aliases(const char *name, const char *args)
{
	int m = -1;
	for (int i = 0; i < st.nmacros; i++)
		if (ci_eq(st.macros[i], name) || ci_eq(st.macros[i], macro_orig(name)))
			m = i;
	if (m < 0 || st.macro_shapes[m].s == NULL)
		return;
	const char *pieces[64];
	size_t lens[64];
	int na = split_commas(args, strlen(args), pieces, lens, 64);
	struct buf pre = { 0 };
	for (const char *p = st.macro_shapes[m].s; *p != '\0';) {
		const char *e = strchr(p, '\n');
		int k, tat = 0;
		if (sscanf(p, "%d %n", &k, &tat) != 1 || tat == 0)
			break;
		char *text = xstrndup(p + tat, e - p - tat);
		p = e + 1;
		/* the expression for these arguments (a quoted one without its
		 * quotes, as r68 passes it) */
		struct buf x = { 0 };
		for (const char *t = text; *t != '\0'; t++) {
			if (*t == '\\' && t[1] >= '1' && t[1] <= '9') {
				int i = t[1] - '1';
				const char *a = i < na ? pieces[i] : "";
				size_t n = i < na ? lens[i] : 0;
				if (n >= 2 && a[0] == '"' && a[n - 1] == '"')
					a++, n -= 2;
				bputc(&x, '(');
				if (n > 0)
					bput(&x, a, n);
				else
					bputc(&x, '0');
				bputc(&x, ')');
				t++;
			} else {
				bputc(&x, *t);
			}
		}
		/* as an operand: a link-time expression (in a macro definition,
		 * of its parameters in turn), the equates folded where none can
		 * take them */
		bprintf(&pre, ".Lr68xm%d set ", k);
		int save_ok = r68x_ok, save_all = fold_all;
		r68x_ok = 1;
		fold_all = 0;
		expression(&pre, x.s, x.n);
		r68x_ok = save_ok;
		fold_all = save_all;
		bputc(&pre, '\n');
		free(x.s);
		free(text);
	}
	if (pre.n > 0 && !st.in_macro)
		bprintf(&pre, "# %d \"%s\"\n", log_line, log_path);
	bput(&line_before, pre.s, pre.n);
	free(pre.s);
}

/* The movem register mask as l68 reads it: a register list stays, a mask
 * with -(An) becomes a generated symbol (see r68togas.md). */
static void movem_operands(struct buf *b, const char *args)
{
	const char *pieces[2];
	size_t lens[2];
	int k = split_commas(args, strlen(args), pieces, lens, 2);
	if (k != 2) {
		bputs(b, args);
		return;
	}
	int predec = lens[1] > 1 && pieces[1][0] == '-' && pieces[1][1] == '(';
	for (int i = 0; i < 2; i++) {
		if (i)
			bputc(b, ',');
		if (pieces[i][0] == '#' && predec && i == 0) {
			/* The mask's bit order reversed, by as itself: a symbol for
			 * it in the epilogue. */
			struct buf e = { 0 };
			expression(&e, pieces[i] + 1, lens[i] - 1);
			char name[64];
			int plain = 1;
			for (size_t j = 1; j < lens[i]; j++)
				if (!symchar((unsigned char)pieces[i][j]))
					plain = 0;
			if (plain && symstart((unsigned char)pieces[i][1])) {
				struct buf nm = { 0 };
				put_symbol(&nm, pieces[i] + 1, lens[i] - 1);
				snprintf(name, sizeof name, "%.50s__rev", nm.s);
				free(nm.s);
			} else {
				snprintf(name, sizeof name, "r68__mask%d", ++st.masks);
			}
			if (st.epilogue_more.s == NULL || strstr(st.epilogue_more.s, name) == NULL) {
				bprintf(&st.epilogue_more, "%s equ ", name);
				for (int bit = 0; bit < 16; bit++)
					bprintf(&st.epilogue_more, "%s((((%s)>>%d)&1)<<%d)", bit ? "+" : "", e.s, bit, 15 - bit);
				bprintf(&st.epilogue_more, "   %.*s\n", (int)lens[i], pieces[i]);
			}
			free(e.s);
			bprintf(b, "#%s", name);
		} else {
			/* a bare address (movem.w d0-a7,$100): absolute long, as
			 * Microware's assemblers (see the instructions' operands) */
			int bare = pieces[i][0] != '#' && memchr(pieces[i], '(', lens[i]) == NULL &&
				memchr(pieces[i], '\\', lens[i]) == NULL && !register_list(pieces[i], lens[i]) &&
				!(lens[i] > 2 && pieces[i][lens[i] - 2] == '.');
			operand(b, pieces[i], lens[i], bare);
		}
	}
}

static int macro_name_clash(const char *name);

/* Whether BASE is an instruction whose operand may be a memory address
 * (its bare expression an absolute address); not a branch, nor a macro
 * called before its definition was seen (a descriptor calling its template macro). */
static int memory_instruction(const char *base)
{
	static const char *const ops[] = {
		"move", "movea", "add", "adda", "addi", "addq", "addx", "sub", "suba", "subi", "subq", "subx",
		"and", "andi", "or", "ori", "eor", "eori", "cmp", "cmpa", "cmpi", "cmpm", "clr", "tst", "neg",
		"negx", "not", "nbcd", "abcd", "sbcd", "tas", "btst", "bchg", "bclr", "bset", "muls", "mulu",
		"divs", "divu", "chk", "chk2", "cmp2", "asl", "asr", "lsl", "lsr", "rol", "ror", "roxl", "roxr",
		"st", "sf", "shi", "sls", "scc", "scs", "shs", "slo", "sne", "seq", "svc", "svs", "spl", "smi",
		"sge", "slt", "sgt", "sle", "moves", "movep", "cas", "cas2", "pack", "unpk", "bfchg", "bfclr",
		"bfexts", "bfextu", "bfffo", "bfins", "bfset", "bftst", NULL };
	for (const char *const *o = ops; *o; o++)
		if (strcmp(base, *o) == 0)
			return 1;
	return 0;
}

/* rept ARGS: whether it is a plain count (1..4096) around exactly one
 * dc.b line without a label (the next line) closed by endr (the line after
 * it), outside a macro definition; then st.rept_fold is the count. */
static int rept_of_bytes(const char *args)
{
	if (st.in_macro || st.rest_of_file == NULL)
		return 0;
	long n;
	if (!constant_value(args, &n) || n < 1 || n > 4096)
		return 0;
	const char *p = st.rest_of_file;	/* at the rept line's own line end */
	if (*p == '\r')
		p++;
	if (*p == '\n')
		p++;
	char *lines[2];
	for (int i = 0; i < 2; i++) {
		size_t len = strcspn(p, "\r\n");
		lines[i] = xstrndup(p, len);
		p += len;
		if (*p == '\r')
			p++;
		if (*p == '\n')
			p++;
	}
	struct line a, b;
	int ok = split_line(lines[0], &a) && split_line(lines[1], &b);
	if (ok) {
		char ba[32], bb[32], sa, sb;
		ok = a.label == NULL && a.op != NULL && b.op != NULL && a.args != NULL;
		if (ok) {
			op_parts(a.op, ba, sizeof ba, &sa);
			op_parts(b.op, bb, sizeof bb, &sb);
			ok = strcmp(ba, "dc") == 0 && (sa == 'b' || sa == 'w' || sa == 'l' || sa == 0) && strcmp(bb, "endr") == 0 && b.label == NULL;
		}
		free_line(&a);
		free_line(&b);
	}
	free(lines[0]);
	free(lines[1]);
	if (ok) {
		st.rept_fold = (int)n;
		st.rept_state = 1;
	}
	return ok;
}

/* -d c32: add, sub, and, or and cmp with an immediate source and a data
 * register destination, written without the i: C 3.2's R68 uses the
 * <ea>,Dn form with the immediate as its operand (and.l #2,d0: $C0BC
 * $00000002), as and Ultra C's r68 the immediate instructions (ANDI:
 * $0280 ...). Written as words: the opcode, then the immediate (through
 * the prelude's r68imml for a long, r68immb for a byte of an external).
 * 1 if done. */
static int c32_immediate(struct buf *out, const char *labs, const char *base, char size, const char *args,
			 const char *rest)
{
	static const struct { const char *name; unsigned op; } ops[] = {
		{ "or", 0x8000 }, { "sub", 0x9000 }, { "cmp", 0xB000 }, { "and", 0xC000 }, { "add", 0xD000 },
	};
	unsigned op = 0;
	for (size_t i = 0; i < sizeof ops / sizeof *ops; i++)
		if (strcmp(base, ops[i].name) == 0)
			op = ops[i].op;
	/* adda/suba #imm,An (add/sub to an address register too): C 3.2's
	 * R68 writes ADDA/SUBA with the immediate also for #1..#8, where
	 * as and Ultra C's r68 make ADDQ/SUBQ */
	int addr = 0;
	if (strcmp(base, "adda") == 0)
		op = 0xD000, addr = 1;
	else if (strcmp(base, "suba") == 0)
		op = 0x9000, addr = 1;
	if (op == 0)
		return 0;
	const char *pieces[4];
	size_t lens[4];
	if (split_commas(args, strlen(args), pieces, lens, 4) != 2 || pieces[0][0] != '#')
		return 0;
	int reg;
	if (lens[1] == 2 && (pieces[1][0] == 'd' || pieces[1][0] == 'D') && pieces[1][1] >= '0' && pieces[1][1] <= '7' &&
	    !addr)
		reg = pieces[1][1] - '0';
	else if (lens[1] == 2 && (pieces[1][0] == 'a' || pieces[1][0] == 'A') && pieces[1][1] >= '0' &&
		 pieces[1][1] <= '7' && (op == 0xD000 || op == 0x9000))
		reg = 8 + pieces[1][1] - '0';
	else if (lens[1] == 2 && strncasecmp(pieces[1], "sp", 2) == 0 && (op == 0xD000 || op == 0x9000))
		reg = 15;
	else
		return 0;
	int s = size == 'b' ? 0 : size == 'l' ? 2 : 1;
	if (reg >= 8 && s == 0)
		return 0;
	unsigned word = reg >= 8 ? op | (unsigned)(reg - 8) << 9 | (s == 2 ? 0x1C0u : 0xC0u) | 0x3C
		: op | (unsigned)reg << 9 | (unsigned)s << 6 | 0x3C;
	/* in parentheses: a dc element in quotes is a string ('0': $3000) */
	struct buf e = { 0 };
	bputc(&e, '(');
	expression(&e, pieces[0] + 1, lens[0] - 1);
	bputc(&e, ')');
	if (s == 1)
		bprintf(out, "%s dc.w $%04X,%s%s", labs, word, e.s, rest);
	else if (s == 0 && known_value(pieces[0] + 1, lens[0] - 1))
		bprintf(out, "%s dc.w $%04X,%s&$ff%s", labs, word, e.s, rest);
	else {	/* a long, or an external's byte (cmp.b #EXT_VAL,d1: relocated) */
		bprintf(out, "%s %s $%04X,", labs, s == 2 ? "r68imml" : "r68immb", word);
		put_angle(out, e.s, e.n);
		bputs(out, rest);
	}
	free(e.s);
	annotate = 1;
	return 1;
}

/* Translates one statement line into OUT (one line, no newline). */
static void translate_line(const char *text, struct buf *out, const char *path, int lineno, FILE *fp, int depth)
{
	struct line l;
	log_path = path;
	log_line = lineno;
	string_params = st.in_macro;
	if (!split_line(text, &l)) {
		bputs(out, text);
		return;
	}
	char base[32], size;
	if (l.op)
		op_parts(l.op, base, sizeof base, &size);
	else
		base[0] = '\0', size = 0;
	/* The dc line of a folded rept (see rept_of_bytes): its list N times. */
	if (st.rept_fold && st.rept_state == 1 && l.op && strcmp(base, "dc") == 0) {
		struct buf a = { 0 };
		for (int i = 0; i < st.rept_fold; i++)
			bprintf(&a, "%s%s", i ? "," : "", l.args);
		free(l.args);
		l.args = a.s;
		st.rept_state = 2;
	}
	/* A call of a renamed macro (see the macro statement): by its new name. */
	for (int i = 0; l.op && i < st.nmren; i++)
		if (strcmp(l.op, st.mren_from[i]) == 0) {
			free(l.op);
			l.op = xstrdup(st.mren_to[i]);
			op_parts(l.op, base, sizeof base, &size);
			break;
		}
	/* sys.l's equates folded in whole where no relocation can take them
	 * (r68 can't either): fields of the instruction word that aren't a
	 * byte (quick values, shift counts), sizes and counts, conditionals
	 * (see fold_equates). moveq's and trap's are bytes: relocated, as
	 * r68 does. */
	static const char *const fold_ops[] = { "ds", "dz", "do", "lo", "org", "rept", "align", NULL };
	static const char *const quick_ops[] = { "addq", "subq", "asl", "asr", "lsl", "lsr", "rol", "ror",
		"roxl", "roxr", NULL };
	fold_all = is_cond(base);
	for (int i = 0; fold_ops[i] && !fold_all; i++)
		fold_all = strcmp(base, fold_ops[i]) == 0;
	r68x_ok = strcmp(base, "equ") != 0 && strcmp(base, "set") != 0;
	fold_imm = 0;
	for (int i = 0; quick_ops[i] && !fold_imm; i++)
		fold_imm = strcmp(base, quick_ops[i]) == 0;
	fold_first = 0;
	line_rest.n = 0;
	if (line_rest.s)
		line_rest.s[0] = '\0';
	bputs(&line_rest, l.rest ? l.rest : "");
	orig_stmt.n = orig_op.n = 0;
	if (orig_stmt.s)
		orig_stmt.s[0] = '\0';
	if (orig_op.s)
		orig_op.s[0] = '\0';
	if (l.op) {
		bputs(&orig_op, l.op);
		bputs(&orig_stmt, l.op);
		if (l.args && *l.args)
			bprintf(&orig_stmt, " %s", l.args);
	}
	/* Outside a psect (definition files used before it) r68 exports
	 * nothing. */
	int wrap_global = 0;
	if (l.label && l.global && st.in_macro && strchr(l.label, '\\') != NULL) {
		/* A global label whose name a macro's argument makes
		 * (_C\1:): exported by each expansion (r68xlab), not in the
		 * epilogue. */
		wrap_global = 2;
	} else if (l.label && l.global && st.in_psect) {
		/* In a conditional, the label may be global only where the
		 * condition is false (Entry: for OS-9000; plain
		 * Entry for OS-9): exported only if its line is assembled. */
		if (st.cond_depth > 0 && !st.in_macro && !st.in_vsect) {
			add_global_marked(l.label);
			wrap_global = 1;
		} else {
			add_global(l.label);
		}
	}

	struct buf lab = { 0 };
	if (l.label) {
		put_symbol(&lab, l.label, strlen(l.label));
		if (l.global)
			bputc(&lab, ':');
	}
	const char *labs = lab.s ? lab.s : "";
	const char *rest = l.rest ? l.rest : "";

	if (!l.op) {
		/* A label alone. */
		if (st.in_vsect && l.label && !same_section(data_section(1))) {
			struct buf nm = { 0 };
			put_symbol(&nm, l.label, strlen(l.label));
			bprintf(out, " r68lab %s,%s%s", data_section(1), nm.s, rest);
			st.section = data_section(1);
			free(nm.s);
		} else {
			bprintf(out, "%s%s", labs, rest);
		}
		goto done;
	}

	if (strcmp(base, "psect") == 0) {
		st.in_psect = 1;
		free(st.psect_args);
		st.psect_args = xstrdup(l.args);
		st.section = ".text";
		bprintf(out, " section .text%s", rest);
		goto done;
	}
	if (strcmp(base, "vsect") == 0) {
		st.in_vsect = 1;
		st.remote = ci_eq(l.args, "remote");
		st.used_remote |= st.remote;
		bprintf(out, "*%s", text);
		goto done;
	}
	if (strcmp(base, "ends") == 0 || strcmp(base, "endsect") == 0) {
		if (st.in_vsect) {
			st.in_vsect = 0;
			st.remote = 0;
			if (same_section(".text"))
				bprintf(out, "*%s", text);
			else
				bprintf(out, " section .text%s", rest);
			st.section = ".text";
		} else {
			st.in_psect = 0;
			bprintf(out, "*%s", text);
		}
		goto done;
	}
	/* spanreg: Ultra C's scratch register for r68's branch sizing. */
	if (strcmp(base, "nam") == 0 || strcmp(base, "pag") == 0 || strcmp(base, "opt") == 0 || strcmp(base, "end") == 0 ||
	    strcmp(base, "spanreg") == 0) {
		bprintf(out, "*%s", text);
		goto done;
	}
	if (strcmp(base, "ttl") == 0 || strcmp(base, "spc") == 0 || strcmp(base, "page") == 0) {
		bprintf(out, "%s %s %s", labs, l.op, l.args);
		goto done;
	}
	if (strcmp(base, "use") == 0) {
		/* The used file, translated inline behind its own linemarker; the
		 * source's numbering resumes after it. */
		char *found = use_file(l.args, path);
		if (found == NULL) {
			/* An error if the line is assembled: r68 skips a use in a
			 * false conditional (a board's definitions file). */
			bputs(out, " r68error <use ");
			error_text(out, l.args, strlen(l.args));
			bputs(out, ": not found>");
		} else {
			bprintf(out, "*%s", text);
			fprintf(fp, "%s\n", out->s);
			out->n = 0;
			out->s[0] = '\0';
			translate_file(found, fp, depth + 1);
			/* The next line is the including file's next. */
			bprintf(out, "# %d \"%s\"", lineno + 1, path);
		}
		free(found);
		goto done;
	}
	if (strcmp(base, "fail") == 0) {
		/* An error with the rest of the line as its text (as's fail
		 * takes an expression). */
		const char *t = strstr(text, l.op) + strlen(l.op);
		while (*t == ' ' || *t == '\t')
			t++;
		size_t tn = strlen(t);
		while (tn > 0 && isspace((unsigned char)t[tn - 1]))
			tn--;
		bputs(out, " r68error <");
		error_text(out, t, tn);
		bputc(out, '>');
		goto done;
	}
	if (strcmp(base, "rept") == 0 && rept_of_bytes(l.args)) {
		/* rept N around one dc line: that line's list N times (the
		 * rept and endr lines comments). In MRI mode as aligns before
		 * a rept after an odd number of bytes, r68 doesn't
		 * (dc.b 0, then Tab rept 23 dc.b ... endr);
		 * and a label on the rept in a vsect belongs to the data that
		 * follows (Buf: rept LEN, dc.w $200): it goes to
		 * the dc line. */
		if (st.in_vsect) {
			/* the section switch and the label here, so that the dc
			 * line (already in its section) is written as it is: a
			 * long list as a macro argument (r68dc) is cut short */
			struct buf nm = { 0 };
			if (l.label)
				put_symbol(&nm, l.label, strlen(l.label));
			bprintf(out, " r68lab %s,%s%s", data_section(1), nm.s ? nm.s : "", rest);
			st.section = data_section(1);
			free(nm.s);
			annotate = 1;
		} else if (*labs) {
			bprintf(out, "%s%s", labs, rest);
			annotate = 1;
		} else {
			bprintf(out, "*%s", text);
		}
		goto done;
	}
	if (st.rept_fold && strcmp(base, "endr") == 0 && st.rept_state == 2) {
		bprintf(out, "*%s", text);
		st.rept_fold = st.rept_state = 0;
		goto done;
	}
	if (strcmp(base, "rept") == 0) {
		st.section = NULL;
		bprintf(out, "%s rept ", labs);
		expression(out, l.args, strlen(l.args));
		goto done;
	}
	if (strcmp(base, "com") == 0) {
		/* NAME: com.s n: a common block of n elements (.w by default),
		 * an ELF common symbol (rof2elf makes ROF's commons ones too).
		 * In a remote vsect it would be remote: refused. */
		if (l.label == NULL || st.remote) {
			fprintf(stderr, "%s:%d: com %s isn't supported\n", path, lineno,
				l.label == NULL ? "without a label" : "in a remote vsect");
			st.errors++;
			bprintf(out, "*%s", text);
			goto done;
		}
		struct buf nm = { 0 };
		put_symbol(&nm, l.label, strlen(l.label));
		bprintf(out, " r68com %s,%d*(", nm.s, size == 'b' ? 1 : size == 'l' ? 4 : 2);
		expression(out, l.args, strlen(l.args));
		bprintf(out, ")%s", rest);
		free(nm.s);
		annotate = 1;
		goto done;
	}
	if (strcmp(base, "org") == 0) {
		bputs(out, "r68__ofs set ");
		expression(out, l.args, strlen(l.args));
		bputs(out, rest);
		goto done;
	}
	if (l.label && (strcmp(base, "equ") == 0 || strcmp(base, "set") == 0 || strcmp(base, "do") == 0 ||
			strcmp(base, "lo") == 0)) {
		add_equate(l.label);
		if (!st.in_macro && (strcmp(base, "equ") == 0 || strcmp(base, "set") == 0) && l.args)
			note_value(l.label, l.args);
		/* set where it is assembled: not in a macro definition, whose
		 * lines run where it is called */
		if (!st.in_macro) {
			if (strcmp(base, "set") == 0 && l.args)
				p1_set(l.label, l.args);
			st.set_done = xrealloc(st.set_done, (st.nset_done + 1) * sizeof *st.set_done);
			st.set_done[st.nset_done++] = xstrdup(l.label);
		}
	}
	if (strcmp(base, "do") == 0 || strcmp(base, "lo") == 0) {
		int bytes = size == 'b' ? 1 : size == 'l' ? 4 : 2;
		struct buf nm = { 0 };
		if (l.label)
			put_symbol(&nm, l.label, strlen(l.label));
		/* Without a label: the macros without one (an empty \1 would
		 * leave a bare "equ"). */
		if (nm.s)
			bprintf(out, " r68%s %s,%d,", base, nm.s, bytes);
		else
			bprintf(out, " r68%sx %d,", base, bytes);
		expression(out, l.args, strlen(l.args));
		bputs(out, rest);
		free(nm.s);
		goto done;
	}
	if (st.in_vsect && (strcmp(base, "ds") == 0 || strcmp(base, "dc") == 0 || strcmp(base, "dz") == 0)) {
		struct buf nm = { 0 };
		if (l.label)
			put_symbol(&nm, l.label, strlen(l.label));
		char sz = size ? size : 'w';
		/* Already in the section: the line as it is (a section switch
		 * would also apply MRI's pending even alignment); otherwise a
		 * prelude macro, the label passed to it. */
		const char *sect = data_section(strcmp(base, "ds") != 0);
		int same = same_section(sect);
		st.section = sect;
		if (strcmp(base, "dc") == 0) {
			struct buf list = { 0 };
			dc_size = sz == 'b' ? 1 : sz == 'l' ? 4 : 2;
			expression_list(&list, l.args, sz != 'b');
			dc_size = 0;
			if (same) {
				bprintf(out, "%s dc.%c ", labs, sz);
				bput(out, list.s ? list.s : "", list.n);
			} else {
				bprintf(out, " r68dc %s,%c,%s,", sect, sz, nm.s ? nm.s : "");
				put_angle(out, list.s ? list.s : "", list.n);
			}
			free(list.s);
		} else if (strcmp(base, "ds") == 0 && sz == 'b') {
			/* ds.b as fill: MRI's ds.b would align the next statement
			 * after an odd count, a section switch too, r68 doesn't. */
			if (same)
				bprintf(out, "%s fill ", labs);
			else
				bprintf(out, " r68fill %s,%s,", sect, nm.s ? nm.s : "");
			expression(out, l.args, strlen(l.args));
			if (same)
				bputs(out, ",1,0");
			bputs(out, rest);
			free(nm.s);
			goto done;
		} else {
			if (same)
				bprintf(out, "%s %s.%c ", labs, strcmp(base, "dz") == 0 ? "dcb" : "ds", sz);
			else
				bprintf(out, " r68%s %s,%c,%s,", base, sect, sz, nm.s ? nm.s : "");
			expression(out, l.args, strlen(l.args));
			if (same && strcmp(base, "dz") == 0)
				bputs(out, ",0");
		}
		bputs(out, rest);
		free(nm.s);
		goto done;
	}
	if (st.in_vsect && strcmp(base, "align") == 0) {
		bprintf(out, " r68align %s,%s,%s%s", data_section(0), data_section(1), *l.args ? l.args : "2", rest);
		st.section = data_section(1);
		goto done;
	}
	if (strcmp(base, "dz") == 0) {
		bprintf(out, "%s dcb.%c ", labs, size ? size : 'w');
		expression(out, l.args, strlen(l.args));
		bputs(out, ",0");
		bputs(out, rest);
		goto done;
	}
	if (strcmp(base, "dc") == 0) {
		bprintf(out, "%s %s ", labs, l.op);
		dc_size = size == 'b' ? 1 : size == 'l' ? 4 : 2;
		expression_list(out, l.args, size != 'b');
		dc_size = 0;
		bputs(out, rest);
		goto done;
	}
	if (strcmp(base, "equ") == 0 && l.label && strcmp(l.args, "*") == 0 && !st.in_vsect) {
		/* label equ *: a plain label (as can't relax a branch to a
		 * global symbol equated to "*"). */
		bprintf(out, "%s%s", labs, rest);
		goto done;
	}
	if (strcmp(base, "equ") == 0 || strcmp(base, "set") == 0 || strcmp(base, "ds") == 0 ||
	    is_cond(base) || strcmp(base, "align") == 0 || strcmp(base, "else") == 0 ||
	    strcmp(base, "endc") == 0 || strcmp(base, "macro") == 0 || strcmp(base, "endm") == 0 ||
	    strcmp(base, "endr") == 0 || strcmp(base, "os9") == 0 || strcmp(base, "tcall") == 0) {
		/* A macro whose body uses \Ln: defined as NAME__r68, called
		 * through r68L. */
		if (strcmp(base, "macro") == 0 && l.label != NULL && uses_length(st.rest_of_file)) {
			st.lmacros = xrealloc(st.lmacros, (st.nlmacros + 1) * sizeof *st.lmacros);
			st.lmacros[st.nlmacros++] = xstrdup(l.label);
			struct buf nm = { 0 };
			put_symbol(&nm, l.label, strlen(l.label));
			free(lab.s);
			lab.s = NULL;
			lab.n = lab.size = 0;
			bprintf(&lab, "%s__r68", nm.s);
			labs = lab.s;
			free(nm.s);
		} else if (strcmp(base, "macro") == 0 && l.label != NULL && macro_name_clash(l.label)) {
			/* r68's macro names are case sensitive, as's aren't: a
			 * name that only the case tells from one of the prelude's
			 * (OS9, which calls os9) or from another macro is
			 * defined as NAME__r68m<n>, and called so. */
			struct buf nm = { 0 };
			put_symbol(&nm, l.label, strlen(l.label));
			free(lab.s);
			lab.s = NULL;
			lab.n = lab.size = 0;
			bprintf(&lab, "%s__r68m%d", nm.s, st.nmren + 1);
			labs = lab.s;
			free(nm.s);
			st.mren_from = xrealloc(st.mren_from, (st.nmren + 1) * sizeof *st.mren_from);
			st.mren_to = xrealloc(st.mren_to, (st.nmren + 1) * sizeof *st.mren_to);
			st.mren_from[st.nmren] = xstrdup(l.label);
			st.mren_to[st.nmren++] = xstrdup(lab.s);
			annotate = 1;
		} else if (strcmp(base, "macro") == 0 && l.label != NULL && strchr(l.label, '.') != NULL &&
			   lab.s != NULL) {
			/* A name with a dot (DRAW.w), defined as put_symbol
			 * writes it: called so, not as DRAW with a size. */
			st.mren_from = xrealloc(st.mren_from, (st.nmren + 1) * sizeof *st.mren_from);
			st.mren_to = xrealloc(st.mren_to, (st.nmren + 1) * sizeof *st.mren_to);
			st.mren_from[st.nmren] = xstrdup(l.label);
			st.mren_to[st.nmren++] = xstrdup(lab.s);
		}
		/* r68 takes an endc in a macro body that closes nothing (a
		 * board's definitions file), as doesn't: dropped. */
		if (strcmp(base, "macro") == 0) {
			st.in_macro = 1;
			st.macro_ifs = 0;
		} else if (strcmp(base, "endm") == 0) {
			st.in_macro = 0;
		} else if (!st.in_macro && is_cond(base)) {
			st.cond_depth++;
		} else if (!st.in_macro && strcmp(base, "endc") == 0) {
			/* An endc that closes nothing (also
			 * outside macros): r68 ignores it, as doesn't: dropped. */
			if (st.cond_depth == 0) {
				bprintf(out, "*%s", text);
				goto done;
			}
			st.cond_depth--;
		} else if (st.in_macro && is_cond(base)) {
			st.macro_ifs++;
		} else if (st.in_macro && strcmp(base, "endc") == 0) {
			if (st.macro_ifs == 0) {
				bprintf(out, "*%s", text);
				goto done;
			}
			st.macro_ifs--;
		}
		/* An equ of a name set from sys.l (-e): a set, as MRI's equ
		 * won't define a name again. */
		if (strcmp(base, "equ") == 0 && l.label != NULL) {
			int folded = 0;
			for (int i = 0; i < st.nfolded && !folded; i++)
				folded = strcmp(st.folded[i], l.label) == 0;
			if (folded) {
				bprintf(out, "%s set ", labs);
				expression_list(out, l.args, 0);
				bputs(out, rest);
				goto done;
			}
		}
		/* Where a conditional or macro leaves the section isn't known. */
		if (strcmp(base, "equ") != 0 && strcmp(base, "set") != 0 && strcmp(base, "ds") != 0 &&
		    strcmp(base, "os9") != 0 && strcmp(base, "tcall") != 0 && strcmp(base, "align") != 0)
			st.section = NULL;
		bprintf(out, "%s %s ", labs, l.op);
		if (strcmp(base, "macro") == 0 && l.label) {
			st.macros = xrealloc(st.macros, (st.nmacros + 1) * sizeof *st.macros);
			st.macro_fold = xrealloc(st.macro_fold, (st.nmacros + 1) * sizeof *st.macro_fold);
			st.macro_fold[st.nmacros] = 0;
			st.macro_shapes = xrealloc(st.macro_shapes, (st.nmacros + 1) * sizeof *st.macro_shapes);
			st.macro_shapes[st.nmacros] = (struct buf){ 0 };
			int renamed = st.nmren > 0 && strcmp(st.mren_from[st.nmren - 1], l.label) == 0 &&
				lab.s != NULL && strcmp(st.mren_to[st.nmren - 1], lab.s) == 0;
			st.macros[st.nmacros++] = xstrdup(renamed ? lab.s : l.label);
			if (l.global && st.in_psect && strchr(l.label, '\\') == NULL) {
				/* NAME: macro: both r68s export NAME, value 0 (ROF's
				 * symbol kind 7); its xdef (add_global) needs it
				 * defined, before the epilogue's globals. */
				struct buf nm = { 0 };
				put_symbol(&nm, l.label, strlen(l.label));
				bprintf(&st.macro_syms, "%s equ 0\n", nm.s);
				free(nm.s);
			}
		}
		size_t len = 0, alen = strlen(l.args);
		if (strcmp(base, "macro") == 0) {
			/* r68's macro takes no operand: what follows is a comment
			 * (MAC macro * for old ...), which
			 * as would take for a parameter list */
			if (*l.args)
				bprintf(out, ";%s", l.args);
		} else if (strcmp(base, "ifc") == 0 || strcmp(base, "ifnc") == 0) {
			bputs(out, l.args);
		} else if (st.c32 && (strcmp(base, "set") == 0 || strcmp(base, "equ") == 0) &&
			   (len = expression_length(l.args, alen)) > 0 && len < alen) {
			/* -d c32: junk after the value, which C 3.2's R68 ignores
			 * (Ultra C's r68 refuses the line) */
			expression(out, l.args, len);
			annotate = 1;
		} else {
			expression_list(out, l.args, 0);
		}
		bputs(out, rest);
		goto done;
	}
	/* An instruction (or a macro call): its operands. */
	{
		/* The caches of cinv and cpush (bc, dc, ic, nc) aren't symbols. */
		/* movec's control registers too (itt0 isn't a symbol there). */
		if (strncmp(base, "cinv", 4) == 0 || strncmp(base, "cpush", 5) == 0 || strcmp(base, "movec") == 0 ||
		    strcmp(base, "pmove") == 0 || strcmp(base, "pmovefd") == 0 || strncmp(base, "pflush", 6) == 0 ||
		    strncmp(base, "ptest", 5) == 0 || strncmp(base, "pload", 5) == 0) {
			int need = cpu_needed(base, size, l.args);
			if (need)
				bprintf(out, "%s r68chip %s,<%s %s>%s", labs, chip_name(need), l.op, l.args, rest);
			else
				bprintf(out, "%s %s %s%s", labs, l.op, l.args, rest);
			goto done;
		}
		int absolute = strcmp(base, "jsr") == 0 || strcmp(base, "jmp") == 0 || strcmp(base, "lea") == 0 ||
			strcmp(base, "pea") == 0;
		const char *op = l.op;
		char opb[16];
		size_t an = strlen(l.args);
		if ((strcmp(base, "ori") == 0 || strcmp(base, "andi") == 0 || strcmp(base, "eori") == 0) && size == 'w' &&
		    an > 4 && strcasecmp(l.args + an - 4, ",ccr") == 0) {
			/* r68 takes the word size for ccr (the byte operation's code). */
			snprintf(opb, sizeof opb, "%s.b", base);
			op = opb;
			annotate_op = 1;
		} else if (st.branch_sizing && size != 'l' && is_branch(base) && local_label(l.args)) {
			/* r68 -b sizes every branch to a label of this source, an
			 * explicit .s or .w too; in MRI mode as sizes only jbra,
			 * jb<cc> and jbsr. Not to an external label (as makes an
			 * absolute jmp on the 68000), nor to a global one unless as
			 * relaxes those (-B). */
			/* as has no jblo, jbhs */
			snprintf(opb, sizeof opb, "j%s", strcmp(base, "blo") == 0 ? "bcs" : strcmp(base, "bhs") == 0 ? "bcc" : base);
			op = opb;
		} else if (strcmp(base, "exg") == 0 && size) {
			/* exg.l: r68 takes the size, as doesn't */
			snprintf(opb, sizeof opb, "exg");
			op = opb;
			annotate_op = 1;
		} else if (strcmp(base, "addi") == 0 || strcmp(base, "subi") == 0) {
			/* r68 makes addi/subi #1..8 addq/subq; as does that for
			 * add/sub. */
			snprintf(opb, sizeof opb, "%.3s%s", base, l.op + 4);
			op = opb;
			annotate_op = 1;
		}
		/* move.l #n,Dn: r68 doesn't make it moveq, as would. */
		int no_moveq = strcmp(base, "move") == 0 && size == 'l';
		/* move.b with a negative immediate: r68 writes the extension
		 * word $00nn, as $FFnn (both $FFnn for andi.b and the others). */
		int byte_imm = strcmp(base, "move") == 0 && size == 'b' && l.args[0] == '#';
		pc_ext_offset = strcmp(base, "movem") == 0 || strcmp(base, "fmovem") == 0 ||
			((strcmp(base, "btst") == 0 || strcmp(base, "bchg") == 0 || strcmp(base, "bclr") == 0 ||
			  strcmp(base, "bset") == 0) && l.args[0] == '#') ? 4 : 2;
		if (is_lmacro(l.op)) {
			/* through r68L, which counts the arguments' lengths */
			bprintf(out, "%s r68L %s", labs, l.op);
			if (*l.args) {
				bputc(out, ',');
				macro_arguments(out, l.args, l.op);
				macro_aliases(l.op, l.args);
			}
			bputs(out, rest);
			goto done;
		}
		if (strcmp(base, "trap") == 0 && l.args[0] == '#' && !is_number(l.args + 1, strlen(l.args + 1))) {
			/* trap #symbol: the number as a byte added to the opcode's,
			 * which a relocation can take (r68's byte reference). */
			bprintf(out, "%s dc.b $4e,$40+(", labs);
			annotate = 1;
			expression(out, l.args + 1, strlen(l.args + 1));
			bprintf(out, ")%s", rest);
			goto done;
		}
		if (st.c32 && !is_macro(l.op) && c32_immediate(out, labs, base, size, l.args, rest))
			goto done;
		size_t start = out->n;
		bprintf(out, "%s %s ", labs, op);
		int need = cpu_needed(base, size, l.args);
		if (is_macro(l.op)) {
			macro_arguments(out, l.args, l.op);
			macro_aliases(l.op, l.args);
		} else if (strcmp(base, "movem") == 0 || strcmp(base, "fmovem") == 0) {
			movem_operands(out, l.args);
		} else {
			const char *pieces[32];
			size_t lens[32];
			int k = split_commas(l.args, strlen(l.args), pieces, lens, 32);
			for (int i = 0; i < k; i++) {
				if (i)
					bputc(out, ',');
				/* A bare address (move.l a6,Space with Space
				 * equ $c0): absolute long in both of Microware's
				 * assemblers, as takes a short one for a small value.
				 * Not a branch's target. */
				int addr_op = memory_instruction(base);
				int abs_here = (absolute ? i == 0 : addr_op && memchr(pieces[i], '\\', lens[i]) == NULL) &&
					pieces[i][0] != '#' &&
					memchr(pieces[i], '(', lens[i]) == NULL && !register_list(pieces[i], lens[i]) &&
					!(lens[i] > 2 && pieces[i][lens[i] - 2] == '.');
				if (lens[i] >= 2 && pieces[i][0] == '"' && pieces[i][lens[i] - 1] == '"') {
					/* No instruction takes a string: a call of a macro
					 * defined later (SBFDesc ...,"sbgiga" in a macro body),
					 * whose quoted argument r68 passes without quotes. */
					put_angle(out, pieces[i] + 1, lens[i] - 2);
				} else if (strcmp(base, "moveq") == 0 && i == 0 && pieces[i][0] == '#' &&
					   known_value(pieces[i] + 1, lens[i] - 1) && !small_number(pieces[i] + 1, lens[i] - 1)) {
					/* moveq #128..#255: r68 takes the byte (moveq #$80
					 * gives -128), as wants -128..127. */
					struct buf e = { 0 };
					expression(&e, pieces[i] + 1, lens[i] - 1);
					bprintf(out, "#((%s)&$ff)-(((%s)&$80)<<1)", e.s, e.s);
					free(e.s);
					annotate = 1;
				} else if (byte_imm && i == 0 && known_value(pieces[i] + 1, lens[i] - 1)) {
					bputs(out, "#(");
					expression(out, pieces[i] + 1, lens[i] - 1);
					bputs(out, ")&$ff");
					annotate = 1;
				} else if ((is_branch(base) || (base[0] == 'd' && base[1] == 'b')) && i == k - 1) {
					twin_mode = 1;
					operand(out, pieces[i], lens[i], abs_here);
					twin_mode = 0;
				} else
					operand(out, pieces[i], lens[i], abs_here);
				/* only where as could take it for moveq: a data register;
				 * and not with * in the value (#exit-*-8: a PC-relative
				 * value, which as can't add another undefined term to) */
				if (no_moveq && i == 0 && pieces[i][0] == '#' && k == 2 && lens[1] == 2 &&
				    (pieces[1][0] == 'd' || pieces[1][0] == 'D') && pieces[1][1] >= '0' && pieces[1][1] <= '7' &&
				    !has_location(pieces[i] + 1, lens[i] - 1)) {
					bputs(out, "+r68__0");
					annotate = 1;
				}
			}
		}
		if (need) {
			/* the line as r68chip <cpu>,<instruction>: the label first */
			char *ins = xstrdup(out->s + start);
			out->n = start;
			out->s[start] = '\0';
			size_t ln = strlen(labs);
			bprintf(out, "%s r68chip %s,<%s>", labs, chip_name(need), ins + ln + 1);
			free(ins);
		}
		bputs(out, rest);
	}
done:
	if (wrap_global && out->s != NULL && lab.s != NULL && strncmp(out->s, lab.s, lab.n) == 0) {
		/* NAME: rest -> r68glab NAME,<rest> (the comment after it) */
		char *body = xstrdup(out->s + lab.n);
		size_t rn = strlen(rest);
		size_t bn = strlen(body);
		if (rn <= bn && strcmp(body + bn - rn, rest) == 0)
			body[bn - rn] = '\0';
		char *b = body;
		while (*b == ' ' || *b == '\t')
			b++;
		struct buf nm = { 0 };
		put_symbol(&nm, l.label, strlen(l.label));
		out->n = 0;
		out->s[0] = '\0';
		if (wrap_global == 2)
			bprintf(out, " r68xlab %s%s%s%s%s", nm.s, *b ? ",<" : "", b, *b ? ">" : "", rest);
		else if (*b && (strcmp(base, "equ") == 0 || strcmp(base, "set") == 0))
			/* NAME: equ value: NAME equ value on one line */
			bprintf(out, " r68gdef %s,<%s>%s", nm.s, b, rest);
		else if (*b)
			bprintf(out, " r68glab %s,<%s>%s", nm.s, b, rest);
		else
			bprintf(out, " r68glab %s%s", nm.s, rest);
		free(nm.s);
		free(body);
	}
	free(lab.s);
	free_line(&l);
}

/* Reads PATH and writes its translation, behind a linemarker, to OUT.
 * Lines end in CR, LF or CR LF; their count is kept. */
static void translate_file(const char *path, FILE *out, int depth)
{
	if (depth > 16)
		die("%s: use nested too deeply", path);
	struct buf text = { 0 };
	if (!read_text(path, &text))
		die("cannot read %s", path);
	fprintf(out, "# 1 \"%s\"\n", path);
	struct buf line = { 0 };
	int lineno = 0;
	size_t i = 0;
	while (i < text.n) {
		size_t j = i;
		while (j < text.n && text.s[j] != '\r' && text.s[j] != '\n')
			j++;
		char *raw = xstrndup(text.s + i, j - i);
		lineno++;
		line.n = 0;
		if (line.s)
			line.s[0] = '\0';
		st.rest_of_file = text.s + j;
		annotate = annotate_op = 0;
		translate_line(raw, &line, path, lineno, out, depth);
		if (line_before.n > 0) {
			fputs(line_before.s, out);
			line_before.n = 0;
		}
		if ((annotate || annotate_op) && line.s && orig_stmt.n) {
			/* on the same line, right after the operands, before the
			 * line's own comment (a comment in MRI mode after ;) */
			size_t ln = strlen(line.s), rn = line_rest.n;
			if (rn == 0 || rn > ln || strcmp(line.s + ln - rn, line_rest.s) != 0)
				rn = 0;
			fprintf(out, "%.*s\t;[r68: %s]%s\n", (int)(ln - rn), line.s, annotate ? orig_stmt.s : orig_op.s,
				line.s + ln - rn);
		} else {
			fprintf(out, "%s\n", line.s ? line.s : "");
		}
		free(raw);
		if (j < text.n && text.s[j] == '\r' && j + 1 < text.n && text.s[j + 1] == '\n')
			j++;
		i = j + 1;
	}
	free(text.s);
	free(line.s);
}

static const char prelude[] =
	" .mri 1\n"
	"* Translated by r68togas from r68 (Microware's assembler) source. A statement changed in a\n"
	"* way not obvious from it is followed by ;[r68: <its original text>] (the operation alone\n"
	"* if only it changed); these rewrites are described in r68togas.md, \"Annotations\".\n"
	"* an error with a text, if assembled: fail, a use file not found\n"
	"r68error macro\n"
	" .mri 0\n"
	" .error \"\\1\"\n"
	" .mri 1\n"
	" endm\n"
	"* os9 call: trap #0 and the call's code\n"
	"os9 macro\n"
	" trap #0\n"
	" dc.w \\1\n"
	" endm\n"
	"* tcall n,code: trap #n and the code\n"
	"tcall macro\n"
	" dc.b $4e,$40+\\1 (trap: an equate's number relocated, as r68 does)\n"
	" dc.w \\2\n"
	" endm\n"
	"* vsect contents: uninitialized (ds), initialized (dc, dz), labels\n"
	"r68ds macro\n"
	" section \\1\n"
	"\\3 ds.\\2 \\4\n"
	" endm\n"
	"r68fill macro\n"
	" section \\1\n"
	"\\2 fill \\3,1,0\n"
	" endm\n"
	"r68dc macro\n"
	" section \\1\n"
	"\\3 dc.\\2 \\4\n"
	" endm\n"
	"r68dz macro\n"
	" section \\1\n"
	"\\3 dcb.\\2 \\4,0\n"
	" endm\n"
	"r68lab macro\n"
	" section \\1\n"
	"\\2\n"
	" endm\n"
	"r68align macro\n"
	" section \\1\n"
	" align \\3\n"
	" section \\2\n"
	" align \\3\n"
	" endm\n"
	"* the offset counter: org, do, lo, and . in expressions\n"
	"r68__ofs set 0\n"
	"r68do macro\n"
	" ifne \\2-1\n"
	"r68__ofs set (r68__ofs+1)/2*2\n"
	" endc\n"
	"\\1 equ r68__ofs\n"
	"r68__ofs set r68__ofs+\\2*(\\3)\n"
	" endm\n"
	"r68lo macro\n"
	"r68__ofs set r68__ofs-\\2*(\\3)\n"
	" ifne \\2-1\n"
	"r68__ofs set r68__ofs&~1\n"
	" endc\n"
	"\\1 equ r68__ofs\n"
	" endm\n"
	"r68dox macro\n"
	" ifne \\1-1\n"
	"r68__ofs set (r68__ofs+1)/2*2\n"
	" endc\n"
	"r68__ofs set r68__ofs+\\1*(\\2)\n"
	" endm\n"
	"r68lox macro\n"
	"r68__ofs set r68__ofs-\\1*(\\2)\n"
	" ifne \\1-1\n"
	"r68__ofs set r68__ofs&~1\n"
	" endc\n"
	" endm\n"
	" section .text\n"
	"r68__btext\n";

/* Whether a macro named NAME would clash in as, whose macro names ignore
 * case: with a prelude macro or with a macro defined before whose name
 * differs from NAME only in case. */
static int macro_name_clash(const char *name)
{
	for (int i = 0; i < st.nmacros; i++)
		if (ci_eq(st.macros[i], name) && strcmp(st.macros[i], name) != 0)
			return 1;
	for (const char *p = prelude; (p = strstr(p, " macro\n")) != NULL; p++) {
		const char *s = p;
		while (s > prelude && s[-1] != '\n')
			s--;
		if ((size_t)(p - s) == strlen(name) && strncasecmp(s, name, p - s) == 0)
			return 1;
	}
	return 0;
}

static void usage(void)
{
	printf("usage: %s [-a SYM[=VAL]]... [-m N] [-b] [-e FILE] [-u DIR]... [-o OUTPUT] SOURCE\n", progname);
	printf("  Translates a Microware r68 source to a GNU as source for as --mri\n");
	printf("  (\".mri 1\" inside), keeping its line numbers; see r68togas.md.\n");
	printf("  -a SYM[=VAL]  define SYM (1 without VAL) in the prelude (r68's -a=)\n");
	printf("  -m N       the CPU in the prelude (r68's -m: 0 68000 ... 6 68060); also the\n");
	printf("             padding of the sections (ucc: 2 with -m0, else 4, nop without -m)\n");
	printf("  -e FILE    equates (NAME VALUE lines, as sys.l's) set in the prelude\n");
	printf("  -b         r68's -b/-bt: branches to this source's labels sized by as\n");
	printf("  -B         with -b: also to its global labels (as relaxes those)\n");
	printf("  -d D       the dialect: ucc (Ultra C's r68, the default) or c32 (C 3.2's R68:\n");
	printf("             its encodings where they differ, sections padded to 2)\n");
	printf("  -u DIR     search DIR for use files (r68's -u)\n");
	printf("  -o OUTPUT  write to OUTPUT (default: standard output)\n");
}

/* What the Microware assemblers pad each section of a psect to: 2 for C
 * 3.2's R68 (-d c32) and Ultra C's r68 -m0, 4 for Ultra C's r68 otherwise
 * (m68k-os9-r68's section_alignment follows the same rule). */
static int pad_alignment(void)
{
	return st.c32 || (st.cpu_given && st.cpu_level == 0) ? 2 : 4;
}

int main(int argc, char **argv)
{
	const char *output = NULL, *source = NULL;
	struct buf defs = { 0 };
	const char *arch = NULL;
	if (getenv("R68TOGAS_FOLDLOG") != NULL && *getenv("R68TOGAS_FOLDLOG"))
		fold_log = fopen(getenv("R68TOGAS_FOLDLOG"), "a");
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
			output = argv[++i];
		else if (strncmp(argv[i], "-a", 2) == 0) {
			const char *d = argv[i][2] ? argv[i] + 2 + (argv[i][2] == '=') : (i + 1 < argc ? argv[++i] : NULL);
			if (d == NULL || !symstart((unsigned char)*d))
				die("-a takes SYM[=VAL]");
			const char *eq = strchr(d, '=');
			size_t n = eq ? (size_t)(eq - d) : strlen(d);
			put_symbol(&defs, d, n);
			bputs(&defs, " set ");
			if (eq)
				expression(&defs, eq + 1, strlen(eq + 1));
			else
				bputs(&defs, "1");
			bputc(&defs, '\n');
		} else if (strcmp(argv[i], "-b") == 0) {
			st.branch_sizing = 1;
		} else if (strcmp(argv[i], "-B") == 0) {
			st.relax_globals = 1;
		} else if (strncmp(argv[i], "-d", 2) == 0) {
			/* -d c32 | ucc: the dialect (whose encodings) */
			const char *d = argv[i][2] ? argv[i] + 2 + (argv[i][2] == '=') : (i + 1 < argc ? argv[++i] : NULL);
			if (d == NULL || (strcmp(d, "c32") != 0 && strcmp(d, "ucc") != 0))
				die("-d takes c32 or ucc");
			st.c32 = strcmp(d, "c32") == 0;
		} else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
			/* NAME VALUE lines (sys.l's equates): set lines in the
			 * prelude, but for the names the source defines itself
			 * (see after scan_labels). */
			FILE *ef = fopen(argv[++i], "r");
			if (ef == NULL)
				die("cannot read %s", argv[i]);
			char name[512], value[64];
			while (fscanf(ef, "%511s %63s", name, value) == 2) {
				st.folded = xrealloc(st.folded, (st.nfolded + 1) * sizeof *st.folded);
				st.foldval = xrealloc(st.foldval, (st.nfolded + 1) * sizeof *st.foldval);
				st.folded[st.nfolded] = xstrdup(name);
				st.foldval[st.nfolded++] = xstrdup(value);
			}
			fclose(ef);
		} else if (strncmp(argv[i], "-m", 2) == 0) {
			const char *d = argv[i][2] ? argv[i] + 2 + (argv[i][2] == '=') : (i + 1 < argc ? argv[++i] : NULL);
			/* With the FPU: r68 takes its instructions for any CPU.
			 * As .arch (before .mri 1 and any instruction), the
			 * same as m68k-os9-as's -m68000 -m68881: MRI's chip
			 * can't give a 68000 the FPU. */
			static const char *const archs[] = { "68000,68881", "68010,68881", "68020,68881", "68030,68881", "68040",
				NULL, "68060" };
			if (d == NULL || d[0] < '0' || d[0] > '6' || d[1] || archs[d[0] - '0'] == NULL)
				die("-m takes 0, 1, 2, 3, 4 or 6");
			arch = archs[d[0] - '0'];
			st.cpu_level = (d[0] - '0') * 10;
			st.cpu_given = 1;
		}
		else if (strncmp(argv[i], "-u", 2) == 0) {
			const char *d = argv[i][2] ? argv[i] + 2 + (argv[i][2] == '=') : (i + 1 < argc ? argv[++i] : NULL);
			if (d && st.nsearch < 64)
				st.search[st.nsearch++] = d;
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-?") == 0 || strcmp(argv[i], "--help") == 0) {
			usage();
			return 0;
		} else if (argv[i][0] == '-' && argv[i][1]) {
			die("unknown option %s", argv[i]);
		} else {
			source = argv[i];
		}
	}
	if (source == NULL) {
		usage();
		return 1;
	}
	if (output != NULL && strcmp(output, source) == 0)
		die("%s: won't write over the source", output);
	FILE *out = stdout;
	char *temp = NULL, *psect_name = NULL;
	if (output != NULL) {
		temp = xmalloc(strlen(output) + 8);
		sprintf(temp, "%s.tmp", output);
		out = fopen(temp, "w");
		if (out == NULL)
			die("cannot write %s", temp);
	}
	/* The translation goes to a temporary file first: the psect's name,
	 * known at its end, is written first, as .file before .mri 1 (a .mri 0
	 * later would make as expand earlier MRI macros' strings wrongly). */
	FILE *final = out;
	out = tmpfile();
	if (out == NULL)
		die("cannot make a temporary file");
	if (arch != NULL)
		fprintf(out, " .arch %s\n", arch);
	fputs(prelude, out);
	/* r68glab NAME[,<rest>]: a global label in a conditional, with a marker */
	fputs("r68glab macro\n\\1\nr68__g_\\1 set 1\n \\2\n endm\n", out);
	/* r68imml OPCODE,VALUE: an instruction word and a long immediate (-d c32) */
	fputs("r68imml macro\n dc.w \\1\n dc.l \\2\n endm\n", out);
	/* r68immb OPCODE,VALUE: an instruction word and a byte immediate of an
	 * external (-d c32) */
	fputs("r68immb macro\n dc.w \\1\n dc.b 0,\\2\n endm\n", out);
	/* r68com NAME,SIZE: a common block */
	fputs("r68com macro\n .mri 0\n .comm \\1,\\2,2\n .mri 1\n endm\n", out);
	/* r68xlab NAME[,<rest>]: a global label made by a macro expansion */
	fputs("r68xlab macro\n xdef \\1\n\\1\n \\2\n endm\n", out);
	/* r68gdef NAME,<equ value>: the same for an equ or set (one line) */
	fputs("r68gdef macro\n\\1 \\2\nr68__g_\\1 set 1\n endm\n", out);
	/* r68chip CPU,<instruction>: one instruction of a later CPU than -m's */
	fprintf(out, "r68chip macro\n chip \\1\n \\2\n chip %s\n endm\n", chip_name(st.cpu_level));
	/* r68L NAME,args: r68__L1..8 set to the arguments' lengths (\Ln in
	 * r68), then NAME__r68 called with the same arguments */
	fputs("r68L macro\n", out);
	for (int a = 1; a <= 8; a++)
		fprintf(out, "r68__L%d set 0\n ifnc '\\%d',''\n irpc r68__ch,\\%d\nr68__L%d set r68__L%d+1\n endr\n endc\n",
			a, a + 1, a + 1, a, a);
	for (int n = 1; n <= 9; n++) {
		fprintf(out, " ifeq NARG-%d\n \\1__r68", n);
		for (int a = 2; a <= n; a++)
			fprintf(out, "%s<\\%d>", a == 2 ? " " : ",", a);
		fputs("\n endc\n", out);
	}
	fputs(" endm\n", out);
	fputs(defs.s ? defs.s : "", out);
	{
		char **globals = NULL;
		int nglobals = 0;
		scan_labels(source, 0, &globals, &nglobals);
		st.gl = globals;
		st.ngl = nglobals;
		/* a name defined global anywhere isn't local */
		int k = 0;
		for (int i = 0; i < st.nlocals; i++) {
			int g = 0;
			for (int j = 0; j < nglobals && !g; j++)
				g = strcmp(st.locals[i], globals[j]) == 0;
			if (!g)
				st.locals[k++] = st.locals[i];
		}
		st.nlocals = k;
		if (st.relax_globals) {
			/* -B: as sizes branches to global labels too */
			for (int j = 0; j < nglobals; j++) {
				st.locals = xrealloc(st.locals, (st.nlocals + 1) * sizeof *st.locals);
				st.locals[st.nlocals++] = globals[j];
			}
		}
		/* sys.l's equates (-e) that may be folded in (fold_equates):
		 * not the names the source defines (by a label, equ, set, do
		 * or lo, under any conditional), as the sources of the
		 * libraries themselves do. */
		int k2 = 0;
		for (int i = 0; i < st.nfolded; i++) {
			int d = 0;
			for (int j = 0; j < st.nlocals && !d; j++)
				d = strcmp(st.locals[j], st.folded[i]) == 0;
			for (int j = 0; j < nglobals && !d; j++)
				d = strcmp(globals[j], st.folded[i]) == 0;
			for (int j = 0; j < st.nsets && !d; j++)
				d = strcmp(st.sets[j], st.folded[i]) == 0;
			if (d)
				continue;
			st.folded[k2] = st.folded[i];
			st.foldval[k2++] = st.foldval[i];
		}
		st.nfolded = k2;
	}
	translate_file(source, out, 0);
	r68x_ok = 0;
	/* A psect without ends: the end of its code, before the line marker
	 * (which as aligns after an odd dc.b, as any directive) */
	int no_ends = pad_alignment() == 4 && st.in_psect && same_section(".text") && st.cond_depth == 0;
	if (no_ends)
		fputs("r68__pend ds.b 0\n", out);
	fprintf(out, "# 1 \"r68.epilogue\"\n");
	/* r68 ends a source in a conditional without a word
	 * (ifne DEBUG): closed here, as doesn't. */
	if (st.cond_depth > 0)
		fprintf(out, "* closing %d conditional%s the source leaves open, as r68 does\n", st.cond_depth,
			st.cond_depth > 1 ? "s" : "");
	for (; st.cond_depth > 0; st.cond_depth--)
		fputs(" endc\n", out);
	/* Each section padded as the Microware assemblers pad a psect's
	 * (measured 2026-10-09): C 3.2's R68 and Ultra C's r68 -m0 to 2; Ultra
	 * C's r68 otherwise to 4, the code with nop (without -m) or trapf
	 * (-m1..6), after a zero byte where the size is odd. m68k-os9-r68
	 * gives the sections that alignment too. */
	{
		int to = pad_alignment();
		unsigned fill = st.cpu_given ? 0x51FC : 0x4E71;
		fprintf(out, "* padding: each section to %d, as %s\n", to,
			st.c32 ? "C 3.2's R68" : to == 2 ? "Ultra C's r68 -m0" : st.cpu_given ? "Ultra C's r68 -m1..6" : "Ultra C's r68");
		if (to == 2) {
			fprintf(out, " section .text\n balign 2,0\n");
		} else if (no_ends) {
			/* A psect without ends (descriptors of 1992-1994 ported
			 * trees): Ultra C's r68 fills an odd size with zeros to
			 * 4, an even one as with ends (measured 2026-10-11). The
			 * line marker added the zero byte (as's MRI alignment
			 * after an odd dc.b), ds.b the rest where the size at
			 * r68__pend was 1 mod 4. The label is on a ds.b, which
			 * isn't aligned (a label alone would move with the
			 * alignment). */
			fprintf(out, "* no ends: an odd size filled with zeros\n section .text\n"
				" ds.b ((r68__pend-r68__btext)&1)*(3-((r68__pend-r68__btext)&3))\n"
				" balignw %d,$%04X\n", to, fill);
		} else {
			fprintf(out, " section .text\n balignw %d,$%04X\n", to, fill);
		}
		fprintf(out, " section .data\n balign %d,0\n section .bss\n balign %d\n", to, to);
		if (st.used_remote)
			fprintf(out, " section .remote.data\n balign %d,0\n section .remote.bss\n balign %d\n", to, to);
		fputs(" section .text\n", out);
	}
	fputs(st.macro_syms.s ? st.macro_syms.s : "", out);
	fputs(st.globals.s ? st.globals.s : "", out);
	if (st.twins.s)
		fprintf(out, "%s\n", st.twins.s + 1);
	fputs(st.epilogue_more.s ? st.epilogue_more.s : "", out);
	fputs("r68__0 equ 0\n", out);
	/* set symbols referred to before their first set: their value at the
	 * end of r68's first pass (p1_set), as r68 (two passes) takes it */
	for (int i = 0; i < st.nfinals; i++) {
		struct buf nm = { 0 };
		put_symbol(&nm, st.finals[i], strlen(st.finals[i]));
		fprintf(out, "%s__final equ %s%s\n", nm.s, nm.s,
			name_in(st.p1s, st.np1s, st.finals[i], strlen(st.finals[i])) ? "__p1" : "");
		free(nm.s);
	}
	if (st.psect_args != NULL) {
		/* psect name,type/lang,attr/rev,edition,stack,entry[,trap entry]:
		 * the module header symbols, as rof2elf makes them. */
		const char *pieces[8];
		size_t lens[8];
		int k = split_commas(st.psect_args, strlen(st.psect_args), pieces, lens, 8);
		static const char *const names[] = { NULL, "__os9_typelang", "__os9_attrev", "__os9_edition",
			"__os9_stack", "__os9_entry", "__os9_trapent" };
		if (k >= 2) {
			struct buf tl = { 0 };
			expression(&tl, pieces[1], lens[1]);
			fprintf(out, " ifne %s\n", tl.s);
			for (int i = 1; i < k && i < 7; i++) {
				struct buf e = { 0 };
				expression(&e, pieces[i], lens[i]);
				/* The entry points are code offsets: a number is one
				 * from the start of the code, as in rof2elf's object. */
				int code = (i == 5 || i == 6) && is_number(pieces[i], lens[i]);
				fprintf(out, " xdef %s\n%s equ %s%s\n", names[i], names[i], code ? "r68__btext+" : "", e.s);
				free(e.s);
			}
			fprintf(out, " endc\n");
			free(tl.s);
		}
		if (k >= 1)
			psect_name = xstrndup(pieces[0], lens[0]);
	}
	fprintf(final, "# 1 \"r68.prelude\"\n");
	if (psect_name != NULL)
		fprintf(final, " .file \"%s\"\n", psect_name);
	if (r68x_notes.s != NULL)
		/* the link-time expressions' definitions (r68x_note.h), not loaded */
		fprintf(final, " .section " R68X_SECTION ",\"\"\n .ascii \"%s\"\n .text\n", r68x_notes.s);
	if (st.used_remote)
		/* The remote data's sections with their ELF flags (MRI's section
		 * has none to give): allocated, writable, .remote.bss without
		 * contents, as GCC's. */
		fprintf(final, " .section .remote.data,\"aw\"\n .section .remote.bss,\"aw\",@nobits\n .text\n");
	rewind(out);
	char chunk[8192];
	size_t got;
	while ((got = fread(chunk, 1, sizeof chunk, out)) > 0)
		fwrite(chunk, 1, got, final);
	fclose(out);
	out = final;
	if (output != NULL) {
		if (fclose(out) != 0)
			die("cannot write %s", temp);
		remove(output);
		if (rename(temp, output) != 0)
			die("cannot write %s", output);
	}
	return st.errors ? 1 : 0;
}

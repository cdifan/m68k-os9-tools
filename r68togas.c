/* r68togas: Microware r68 assembler sources to gas sources (MRI mode).
 *
 * Translates r68's own layer of the language line by line, so that every
 * line keeps its number: the output starts with a prelude (MRI macros for
 * r68's directives), then the source behind a linemarker, rewritten in
 * place, then an epilogue (xdefs, the module header symbols). gas
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
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "r68x_note.h"

static const char *progname = "r68togas";

/* The files this run has written, removed if it fails (die): the output's
 * temporary file, -s's use files' outputs */
static char *made[256];
static int nmade;

static void die(const char *format, ...)
{
	va_list ap;
	fprintf(stderr, "%s: ", progname);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fputc('\n', stderr);
	for (int i = 0; i < nmade; i++)
		remove(made[i]);
	exit(1);
}

/* An anonymous temporary file, as tmpfile(): Windows' tmpfile() makes it in
 * the drive's root, which a user may not write to; there one in the
 * temporary directory, deleted when closed. */
static FILE *temp_file(void)
{
#ifdef _WIN32
	char dir[MAX_PATH], path[MAX_PATH];
	if (GetTempPathA(sizeof dir, dir) == 0 || GetTempFileNameA(dir, "r68", 0, path) == 0)
		return NULL;
	return fopen(path, "w+bD");
#else
	return tmpfile();
#endif
}

static void note_made(char *path)
{
	if (nmade < (int)(sizeof made / sizeof made[0]))
		made[nmade++] = path;
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
	/* any length: a rept's dc list can be several kilobytes */
	char tmp[4096];
	va_list ap;
	va_start(ap, format);
	int n = vsnprintf(tmp, sizeof tmp, format, ap);
	va_end(ap);
	if (n < 0)
		return;
	if ((size_t)n < sizeof tmp) {
		bput(b, tmp, n);
		return;
	}
	char *big = xmalloc((size_t)n + 1);
	va_start(ap, format);
	vsnprintf(big, (size_t)n + 1, format, ap);
	va_end(ap);
	bput(b, big, n);
	free(big);
}

/* ----- Names ----- */

/* gas's m68k register names (from binutils' tc-m68k.c), which it also
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

/* Whether S (N chars) is a gas register name as a symbol would clash
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

/* -r (r68's): registers only written with % */
static int reg_prefix_only;

/* Whether S (N chars) is a register as written in an operand: r68 takes
 * %d0 always, d0 unless -r (then a symbol). */
static int is_reg(const char *s, size_t n)
{
	if (n > 1 && s[0] == '%')
		return is_r68_register(s + 1, n - 1);
	return !reg_prefix_only && is_r68_register(s, n);
}

/* The number of data or address register S (N chars), 0-7 for d0-d7, 8-15
 * for a0-a7 and sp; -1 for anything else (is_reg: %d0, d0 unless -r). */
static int reg_number(const char *s, size_t n)
{
	if (!is_reg(s, n))
		return -1;
	if (s[0] == '%') {
		s++;
		n--;
	}
	if (n == 2 && strncasecmp(s, "sp", 2) == 0)
		return 15;
	if (n == 2 && (s[0] == 'd' || s[0] == 'D') && s[1] >= '0' && s[1] <= '7')
		return s[1] - '0';
	if (n == 2 && (s[0] == 'a' || s[0] == 'A') && s[1] >= '0' && s[1] <= '7')
		return 8 + s[1] - '0';
	return -1;
}

/* S (N chars) without the % of a register written with one (%d0.w*4; a
 * binary number has a digit after the %). */
static void skip_percent(const char **s, size_t *n)
{
	if (*n > 1 && (*s)[0] == '%' && isalpha((unsigned char)(*s)[1])) {
		(*s)++;
		(*n)--;
	}
}

static int defined_name(const char *s, size_t n);

/* A symbol's name as gas gets it: '@' (legal in r68) as "_at_", a name
 * gas takes for a register with "__r68" after it (one r68 knows as a
 * register too where the source defines it: a1 dc.b 1, CCR equ 0; with -r
 * any). */
static void put_symbol(struct buf *b, const char *s, size_t n)
{
	/* A dot inside the name (labels lab.a ... lab.s):
	 * __dot__, gas would read bls.s lab.s's .s as a size;
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
	if (dots || clashes(s, n) || (is_r68_register(s, n) && (defined_name(s, n) || reg_prefix_only)))
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

/* Translates an expression E (r68) to gas MRI syntax into B: every
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
static void mri_strings(struct buf *b, const char *text, size_t n);
static int is_cond(const char *base);
static void dot_less_use(const char *s, size_t n);
static int twin_mode;	/* in a PC-relative displacement or a branch target: global labels as twins */
static int p1_mode;	/* an expression as r68's first pass: set symbols not set yet are 0 (p1_name) */
static int value_mode;	/* an equ's or set's value: register names are symbols there */
static int p1_name(struct buf *b, const char *s, size_t n);
static int in_macro_def(void);
static int memory_instruction(const char *base);
static void char_param(struct buf *b, int n);
static int name_in(char **list, int n, const char *s, size_t len);
static int shift_masked(void);

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
			/* r68 counts empty arguments too, gas's NARG doesn't:
			 * the count the call set, captured by the expansion
			 * (narg_capture) */
			bputs(b, "r68__n\\@");
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
	if (*p == '\'' && p[1] == '\\' && p[2] >= '1' && p[2] <= '9' && p[3] == '\'' && in_macro_def()) {
		/* '\1' in a macro body (cmpi.b #'\1',d0;
		 * move.l #'\1',(a5)+): the value of the argument's
		 * characters, an alias each call sets (char_aliases) */
		char_param(b, p[2] - '0');
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
		/* gas splits operands at a comma, and ends them at a blank, in a
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
		dot_less_use(p, q - p);
		if (value_mode && is_r68_register(p, q - p) && !defined_name(p, q - p))
			bputs(b, "__r68");	/* Reg equ d3: d3 an undefined symbol in r68 */
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
		struct buf count = { 0 };
		int small = right.s && isdigit((unsigned char)right.s[0]) &&
			    strspn(right.s, "0123456789") == strlen(right.s) && atoi(right.s) < 32;
		if (prec == P_SHIFT && shift_masked() && right.s && !small) {
			/* Ultra C's r68 shifts as the x86 does, the count
			 * modulo 32 (1<<8+199 is 1<<15); C 3.2's
			 * R68 and gas give 0 */
			bprintf(&count, "((%s)&31)", right.s);
		} else {
			bputs(&count, right.s ? right.s : "");
		}
		if (prec == P_SHIFT && strcmp(as, ">>") == 0 && left.s) {
			/* Both r68s shift a 32-bit signed value: arithmetic
			 * ($80000000>>4 is $f8000000); gas's >> is logical on
			 * 64 bits. Sign-extended X, (X-(X&(2^n-1)))/2^n. */
			struct buf x = { 0 };
			bprintf(&x, "((((%s)+$80000000)&$ffffffff)-$80000000)", left.s);
			if (small)
				bprintf(&both, "((%s-(%s&%lu))/%lu)", x.s, x.s,
					(1ul << atoi(right.s)) - 1, 1ul << atoi(right.s));
			else
				bprintf(&both, "((%s-(%s&((1<<%s)-1)))/(1<<%s))", x.s, x.s, count.s, count.s);
			free(x.s);
		} else {
			bputs(&both, left.s ? left.s : "");
			bputs(&both, as);
			bputs(&both, count.s ? count.s : "");
		}
		free(count.s);
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
static struct buf line_after;	/* and after it */
static struct buf hoisted;	/* macro definitions moved before the source (translate_file) */

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
	if (n > 0 && s[0] == '%')
		k++;
	while (k < n && isalnum((unsigned char)s[k]))
		k++;
	if (!is_reg(s, k))
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
		const char *p = pieces[i];
		size_t l = lens[i];
		skip_percent(&p, &l);
		if (l == 2 && strncasecmp(p, "sp", 2) == 0)
			return 1;
		/* a parameter: taken as an address register (d(\2) keeps its
		 * 16-bit displacement, which r68 keeps for any register) */
		if (l == 2 && p[0] == '\\')
			return 1;
		return l == 2 && tolower((unsigned char)p[0]) == 'a' && p[1] >= '0' && p[1] <= '7';
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
	if (k < 1 || !is_reg(pieces[0], lens[0]))
		return 0;
	const char *p = pieces[0];
	size_t l = lens[0];
	skip_percent(&p, &l);
	return (l == 2 || l == 3) && strncasecmp(p, "pcr", l) == 0;
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

/* -y (r68's -y with -b, for a 68020 or later): branches and PC-relative
 * references to labels 32-bit (bra.l, (bd.l,PC)); to constants as without */
static int long_refs;

/* -m2 and later: (d,An,Xn), (d,An) and (d,PC...) in the full format (see
 * operand) */
static int full_format;

/* Whether S (N chars) is a register or register list (d0-d3/a0), or a
 * 68020 register pair (mulu.l #n,d3:d2; cas2's d0:d1). */
static int register_list(const char *s, size_t n)
{
	size_t i = 0;
	while (i < n) {
		size_t j = i + (s[i] == '%');
		while (j < n && isalnum((unsigned char)s[j]))
			j++;
		if (!is_reg(s + i, j - i))
			return 0;
		if (j < n && (s[j] == '-' || s[j] == '/' || s[j] == ':'))
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
		/* %d0: r68's register prefix, which gas (MRI) needn't */
		if (s[i] == '%' && i + 1 < n && isalpha((unsigned char)s[i + 1]) &&
		    (i == 0 || !symchar((unsigned char)s[i - 1]))) {
			size_t j = i + 1;
			while (j < n && isalnum((unsigned char)s[j]))
				j++;
			if (is_r68_register(s + i + 1, j - i - 1))
				i++;
		}
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
static int source_label(const char *s, size_t n);

/* Whether operand S (N chars) is an address in parentheses without a
 * register ((4): move.l (4),a5), which r68 takes as an absolute
 * long address as a bare one; gas as a short one. */
static int paren_address(const char *s, size_t n)
{
	if (n < 3 || s[0] != '(' || s[n - 1] != ')')
		return 0;
	int depth = 0;
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '(')
			depth++;
		else if (s[i] == ')' && --depth == 0 && i != n - 1)
			return 0;
		else if (s[i] == ',')
			return 0;
	}
	return !register_part(s + 1, n - 2);
}

static int source_constant(const char *s, size_t n);

static void operand(struct buf *b, const char *s, size_t n, int absolute)
{
	if (n == 0)
		return;
	if (n > 2 && s[0] == '$' && s[1] == '#') {
		/* $#: r68 skips the $ without digits (andi.w
		 * $#Mask,d1, a typo it takes for an immediate) */
		s++;
		n--;
		annotate = 1;
	}
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
		if (regs > 0 && full_format && open == 0 && k >= 2 && !register_part(pieces[0], lens[0]) &&
		    register_part(pieces[1], lens[1]) &&
		    memchr(s, '[', n) == NULL &&
		    !(lens[0] > 2 && pieces[0][lens[0] - 2] == '.' && strchr("wlWL", pieces[0][lens[0] - 1]) &&
		      is_number(pieces[0], lens[0] - 2))) {
			/* (d,An,Xn), (d,An), (d,PC), (d,PC,Xn) on a 68020 and
			 * later (Ultra C's r68 refuses the form on a 68000):
			 * r68 always writes the full extension word with a
			 * 32-bit displacement, the index suppressed where there
			 * is none (gas: zd0), also for (0,a0); a constant before
			 * PC less 2 from the extension word ((6,pc): 8). A
			 * number with .w stays 16 bits; a symbol's .w is part of
			 * its name in r68 (tested 2026-10-13). */
			const char *d = pieces[0], *base = pieces[1];
			size_t dn = lens[0], bn = lens[1];
			/* ((d).w,PC,Xn): the size of a displacement in
			 * parentheses, 16 bits (both r68s; checked 2026-10-16) */
			char dsz = 'l';
			int whole = dn > 4 && d[0] == '(' && d[dn - 3] == ')' && d[dn - 2] == '.' &&
				strchr("wlWL", d[dn - 1]) != NULL;
			for (size_t i = 0, depth = 0; whole && i < dn - 3; i++) {
				if (d[i] == '(')
					depth++;
				else if (d[i] == ')' && --depth == 0)
					whole = 0;	/* (a)+(b).w: not one */
			}
			if (whole) {
				dsz = (char)tolower((unsigned char)d[dn - 1]);
				d++;
				dn -= 4;
			}
			skip_percent(&base, &bn);
			int pcb = (bn == 2 || bn == 3) && strncasecmp(base, "pcr", bn) == 0;
			bputc(b, '(');
			if (pcb && (is_number(d, dn) || source_constant(d, dn))) {
				bprintf(b, "(*+%d+2+", pc_ext_offset);
				expression(b, d, dn);
				bputc(b, ')');
				annotate = 1;
			} else {
				bputc(b, '(');
				twin_mode = pcb;
				expression(b, d, dn);
				twin_mode = 0;
				bputc(b, ')');
			}
			bprintf(b, ".%c", dsz);
			for (int i = 1; i < k; i++) {
				bputc(b, ',');
				const char *r = pieces[i];
				size_t rl = lens[i];
				skip_percent(&r, &rl);
				if (rl == 3 && strncasecmp(r, "pcr", 3) == 0)
					bputs(b, "pc");
				else
					put_regs(b, pieces[i], lens[i]);
			}
			if (k == 2)
				bputs(b, ",zd0.w");
			bputc(b, ')');
			bput(b, s + end + 1, n - end - 1);
			return;
		}
		if (regs > 0) {
			/* d(An): r68 keeps a 16-bit displacement, also of 0, which
			 * gas would drop; (d).w(An) keeps it, on any CPU. */
			int an = regs == 1 && k == (open > 0 ? 1 : 2) && address_register(inner, ilen);
			/* A number before (pc), or a constant (equates): both
			 * r68s' raw displacement, which gas (MRI) takes as an
			 * absolute address: the address it means, from the
			 * extension word's. */
			int pc = open > 0 && pc_register(inner, ilen) && (is_number(s, open) || source_constant(s, open));
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
				/* label(pc): 16 bits, as r68 keeps it: for a 68020 gas
				 * would make a symbol whose distance it doesn't know
				 * (a global's, an external's) the 32-bit (bd,PC) form;
				 * (label).w(pc) is the same on a 68000. With -y 32 bits,
				 * as r68's -y. */
				bputc(b, '(');
				twin_mode = 1;
				expression(b, s, open);
				twin_mode = 0;
				bputs(b, long_refs ? ").l" : ").w");
			} else if (open > 0 && pc_register(inner, ilen)) {
				/* d(pc,Xn): 8 bits, the brief form, as r68 keeps it:
				 * for a 68020 gas would make a value it doesn't know in
				 * its first pass (L12.-LI12., a label further on) the
				 * full form with a 16-bit displacement. With -y the
				 * full form with 32 bits, as r68's -y. */
				bputc(b, '(');
				twin_mode = 1;
				expression(b, s, open);
				twin_mode = 0;
				bputs(b, long_refs ? ").l" : ").b");
			} else if (open > 0 && k >= 2 && full_format) {
				/* d(An,Xn): the brief form in both r68s, also on a
				 * 68020 (a value out of 8 bits is an error); gas would
				 * make a value it doesn't know yet (a label or equate
				 * further on) the full form (checked 2026-10-16) */
				bputc(b, '(');
				expression(b, s, open);
				bputs(b, ").b");
			} else if (open > 0) {
				expression(b, s, open);
			}
			bputc(b, '(');
			for (int i = 0; i < k; i++) {
				if (i)
					bputc(b, ',');
				if (register_part(pieces[i], lens[i])) {
					const char *r = pieces[i];
					size_t rl = lens[i];
					skip_percent(&r, &rl);
					if (rl == 3 && strncasecmp(r, "pcr", 3) == 0)
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
	/* An absolute address with a size (expr.w, expr.l, (expr).l); not a
	 * label of the source with a dot (Get.w). */
	if (n > 2 && s[n - 2] == '.' && strchr("wlWL", s[n - 1]) && !source_label(s, n)) {
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
	/* A backslash of the string goes as $5c between the parts
	 * ('\'): gas's scrubber takes \' in quotes for an escaped quote and
	 * runs the string on, though MRI strings have no escapes. */
	int open = 0, any = 0;
	for (size_t i = 1; i < n; i++) {
		char c = s[i];
		if (string_params && c == '\\' && i + 1 < n && (isdigit((unsigned char)s[i + 1]) || s[i + 1] == '@' ||
								 s[i + 1] == '#')) {
			if (!open)
				bputs(b, any++ ? ",'" : "'");
			open = 1;
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
		if (c == '\\') {
			if (open)
				bputc(b, '\'');
			open = 0;
			bputs(b, any++ ? ",$5c" : "$5c");
			continue;
		}
		if (!open)
			bputs(b, any++ ? ",'" : "'");
		open = 1;
		if (c == '\'')
			bputc(b, '\'');
		bputc(b, c);
	}
	if (open)
		bputc(b, '\'');
	else if (!any)
		bputs(b, "''");
}

/* ----- Lines ----- */

struct line {
	char *label;		/* null if none */
	int global;		/* label: */
	char *op;		/* opcode, with its size; null if none */
	char *args;		/* operand field */
	char *rest;		/* the comment after the operands, with its blank */
};

/* The comment field S (from its blank) as a ';' comment: gas takes text
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
		"ends", "endsect", "end", "pag", "page", "fnop", "pflusha", "pflushan", "bgnd", NULL,
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
	/* the operand right after the operation, without a blank
	 * (ifeq(Type-Board), andi.l#Mask,d7, tst.l(a0), move.l-(a0),d0):
	 * r68 ends the operation at #, (, - or * (Ultra C's r68, tested
	 * 2026-10-13; a $ belongs to it: clr.l$10(a0) is "illegal suffix") */
	for (const char *c = p + 1; c < q; c++)
		if (strchr("#(-*", *c) != NULL) {
			q = c;
			break;
		}
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
		} else if (*q == '\\' && (q[1] == '"' || q[1] == '\'')) {
			/* an escaped quote, not one beginning a string
			 * (MAC \""abc> \"") */
			q++;
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
	int used_pcm;		/* a .r68pcm note written (pcm_marks) */
	char **lsnames;		/* the source's labels with their sections (label_section) */
	char *lssecs;
	int nls;
	char *cur_psect;	/* the psect statement's name, for label_alias */
	struct buf aliases;	/* "<section letter> <alias>" lines, those used (label_alias) */
	int in_macro, macro_ifs;	/* in a macro definition; its open conditionals */
	int cond_depth;		/* open conditionals outside macro definitions */
	int branch_sizing;	/* -b: r68's -b/-bt */
	int relax_globals;	/* -B: gas relaxes branches to global symbols */
	int cpu_level;		/* -m: 0, 10, 20, 30, 40, 60 */
	char **locals;		/* -b: labels defined without ':' (scan_labels) */
	int nlocals;
	char **lmacros;		/* macros whose body uses \Ln: called through r68L */
	int nlmacros;
	const char *rest_of_file;	/* the text after the current line */
	char **vlabs;		/* the vsect labels so far, each in the section of vsecs[i] */
	const char **vsecs;
	int nvlabs;
	char **dlvs;		/* the names defined as .-LABEL (dot_less_vsect) */
	int ndlvs;
	int dlv_line;		/* the line whose use of one has its error */
	char **sets;		/* the names defined by set (scan_labels) */
	int nsets;
	char **equ_names;	/* the names defined by equ outside macro bodies and */
	char **equ_texts;	/* their expressions, NULL for a name defined twice */
	int nequs;		/* (scan_labels; prescan_equate) */
	char **set_done;	/* names set so far outside macro definitions */
	int nset_done;
	char **finals;		/* those referred to before their first set */
	int nfinals;
	char **p1s;		/* finals with a NAME__p1 (their value in r68's first pass) */
	int np1s;
	const char *section;	/* the current section, null if not known */
	char *psect;		/* the psect line's fields */
	char *psect_args;
	char ps_captured[8];	/* the psect's values captured where it is (r68__ps<n>) */
	int npsects;		/* psect statements (in conditionals: either) */
	char **ps_all;		/* each psect statement's fields and captured values */
	char (*ps_all_captured)[8];
	char **complete;	/* equ and set names whose value is known where they are */
	int ncomplete;
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
	char *macro_narg;	/* per macro: its body uses \# (narg_capture) */
	int macro_variant;	/* translating one variant of a macro body's dc/ds */
	char *macro_viaL;	/* per macro: its body calls a macro through r68L */
	char *macro_open;	/* per macro: its body leaves a conditional open */
	int nends;		/* psect ends statements (.Lr68ends<n>) */
	int ends_cond;		/* one in a conditional or macro body: whether one is
				   assembled known only to gas (r68__ended) */
	unsigned *macro_twin;	/* per macro: the parameters it uses only as
				   branch targets and on pc (twin_params) */
	unsigned *macro_mem;	/* per macro: the parameters (bit n-1 for \n) its body
				   uses only as a whole address operand (mem_params) */
	unsigned *macro_chars;	/* per macro: the parameters (bit n-1 for \n) its body
				   uses as '\n' (char_param) */
	unsigned *macro_bconst;	/* per macro: the parameters its body takes as a branch's
				   target (branch_const_flags) */
	unsigned *macro_ops;	/* per macro: the parameters (bit n-1 for \n) its body
				   uses as an operation (b\2, \1.w): mnemonic arguments */
	/* gas's MRI mode aligns at any directive after an odd dc.b, a line
	 * marker too: after byte data (maybe_odd) the lines put before a
	 * statement (line_before) get no marker; one is owed, written before
	 * the next statement that aligns anyway (aligning: an instruction,
	 * dc.w ...) */
	int maybe_odd, marker_owed, aligning;
	int narg_pending;	/* the macro defined on the line before uses \# */
	struct buf *macro_shapes;	/* per macro: its link-time expressions of parameters,
					   lines "K TEXT" (see macro_shape) */
	int nshapes;
	char **gl;		/* the labels defined with ':' (scan_labels) */
	int ngl;
	char **mren_from, **mren_to;	/* macros renamed: gas's macro names ignore case */
	int nmren;
	int c32;		/* -d c32: C 3.2's R68's encodings */
	int ucc95;		/* -d ucc95: Ultra C's r68 of 1994-95, code padded with zeros */
	int cpu_given;		/* -m given (r68 without -m pads as with -m2..6, with nop) */
	int rept_fold, rept_state;	/* a rept of one dc line folded: its count; 1: the dc
					   next, 2: the endr */
	struct buf twins;	/* their __r68pc twins made (epilogue lines) */
	int used_debug;		/* a debug.b/.w/.l line: .r68dbg (debug_line) */
	int ndebug;		/* debug lines (.Lr68dp<n>, .Lr68ds<n>) */
	int nvsects;		/* vsect statements (.Lr68vs<n>, .Lr68ve<n>) */
	int vs_init;		/* the vsect's initialized data has had a dc or debug line */
	int reps;		/* open rept blocks */
	struct buf ord;		/* the .r68ord records (order_record) */
};

static struct state st;

/* Whether the names of expression text S are all equ or set names whose
 * values were known where they were defined (st.complete), numbers aside:
 * gas has its value there (a set to a name defined later would be 0). */
static int expr_complete(const char *s)
{
	for (const char *p = s; *p;) {
		if (*p == '$' && isxdigit((unsigned char)p[1])) {
			p++;
			while (isxdigit((unsigned char)*p))
				p++;
			continue;
		}
		if (*p == '\'' || *p == '"') {
			char q = *p++;
			while (*p && *p != q)
				p++;
			if (*p)
				p++;
			continue;
		}
		if (isalpha((unsigned char)*p) || *p == '_' || *p == '.' || *p == '@') {
			const char *q = p;
			while (isalnum((unsigned char)*q) || *q == '_' || *q == '.' || *q == '@' || *q == '$')
				q++;
			if (!name_in(st.complete, st.ncomplete, p, (size_t)(q - p)))
				return 0;
			p = q;
			continue;
		}
		if (isspace((unsigned char)*p) || *p == ';')
			break;
		p++;
	}
	return 1;
}

static int in_macro_def(void)
{
	return st.in_macro;
}

/* Whether S (N chars) is a label of the source as written (the pre-scan's
 * names, local or global). */
static int source_label(const char *s, size_t n)
{
	for (int i = 0; i < st.nlocals; i++)
		if (strlen(st.locals[i]) == n && strncmp(st.locals[i], s, n) == 0)
			return 1;
	for (int i = 0; i < st.ngl; i++)
		if (strlen(st.gl[i]) == n && strncmp(st.gl[i], s, n) == 0)
			return 1;
	return 0;
}

/* '\N' in the body of the macro being defined: .Lr68c<macro>_<N>, which
 * each call sets to the value of its argument's characters (char_aliases). */
static void char_param(struct buf *b, int n)
{
	int m = st.nmacros - 1;
	if (m < 0) {
		bprintf(b, "'\\%d'", n);
		return;
	}
	st.macro_chars[m] |= 1u << (n - 1);
	bprintf(b, ".Lr68c%d_%d", m, n);
}

/* Whether shift counts are taken modulo 32 (Ultra C's r68, the default
 * dialect; see parse_binary). */
static int shift_masked(void)
{
	return !st.c32;
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
 * its use files (scan_labels: labels, equ and set names; the names macro
 * calls make, scan_macro_labels: _C\1 called with one is _Cone) nor so far
 * (equ, set), and no register. A name made of parameters (_C\1, V_\1,
 * a macro's \1:) matched every name with that prefix as a pattern, which
 * left the source with no externals (V_Play+BASE). r68
 * leaves such a name to the link, also in a sum (ext_a+EXT_B,
 * lab_a-lab_b): a link-time expression. A name with a dot too (Mem.Beg,
 * written as __dot__ in the field, see put_symbol): the note names it as
 * the object will after m68k-os9-r68's renaming, and as rof2elf does. */
static int external_name(const char *s, size_t n)
{
	static const char *const regs[] = { "sp", "pc", "sr", "ccr", "usp", "zpc", NULL };
	if (n == 2 && strchr("dDaA", s[0]) && s[1] >= '0' && s[1] <= '7')
		return 0;
	for (const char *const *r = regs; *r; r++)
		if (strlen(*r) == n && strncasecmp(*r, s, n) == 0)
			return 0;
	return !defined_name(s, n);
}

/* Whether the source defines S (N chars): a label, equ or set. */
static int defined_name(const char *s, size_t n)
{
	char **lists[] = { st.locals, st.gl, st.sets, st.equates };
	int counts[] = { st.nlocals, st.ngl, st.nsets, st.nequates };
	for (int l = 0; l < 4; l++)
		for (int i = 0; i < counts[l]; i++)
			if (strchr(lists[l][i], '\\') == NULL && name_match(lists[l][i], s, n))
				return 1;
	return 0;
}

/* Link-time expressions (r68x_note.h): an expression of external names
 * (a library's equates) that one relocation can't take but a sum of them
 * can (Dir_+Write_, PD_PAR-PD_OPT, 4*F$Send, with constants; a label of
 * this source plus them, Lab+Ext, through its section's alias, see
 * label_alias) becomes r68x_<hash>+constant, the definition noted in
 * .r68x, where a relocation takes it (r68x_ok: an instruction's or dc's
 * operand; not equ and set, whose names may be used anywhere). */
static int r68x_ok;
static struct buf r68x_notes;	/* the .r68x section's lines */
static int lin_params;	/* a macro parameter \1..\9 is a term (macro_shape) */

struct lin {
	struct r68x x;
	long c;
	int ok;
	const char *p;
	const char *lab;	/* a label of this source (labc times), at most one */
	size_t labn;
	long labc;
};

static char label_section(const char *s, size_t n);

/* Adds R's label (negated if NEG) to V's: one label only, added. */
static void lin_add_label(struct lin *v, const struct lin *r, int neg)
{
	if (r->labc == 0)
		return;
	if (v->labc != 0) {
		v->ok = 0;
		return;
	}
	v->lab = r->lab;
	v->labn = r->labn;
	v->labc = neg ? -r->labc : r->labc;
}

static void lin_binary(struct lin *v, int prec);

static int lin_depth;
static const char *equate_text(const char *s, size_t n);
static int cv_known(const char *s, size_t n, long *v);
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
			v->labc = -v->labc;
		} else if (*p != '+') {
			v->c = ~v->c;
			v->ok &= v->x.n == 0 && v->labc == 0;
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
		/* only external names (a library's equates): a label, a set
		 * symbol, the location don't go in a note; an equate of
		 * external names goes as its expression (PD_Mine
		 * equ PD_Base+18 in M$DTyp+PD_Mine-PD_OPT) */
		const char *eq = !external_name(p, q - p) ? equate_text(p, q - p) : NULL;
		long cv;
		if (eq == NULL && !external_name(p, q - p) && cv_known(p, q - p, &cv)) {
			/* a constant of the source that isn't an equate: an
			 * org/do offset (E.Field-P$Ext(a2)) */
			v->c = cv;
			v->p = q;
			return;
		}
		if (eq != NULL && lin_depth < 8) {
			struct lin sub = { { NULL, 0 }, 0, 1, eq, NULL, 0, 0 };
			lin_depth++;
			lin_binary(&sub, P_SHIFT);
			lin_depth--;
			if (!sub.ok || *sub.p != '\0' || (lin_params && sub.x.n == 0)) {
				/* in a macro definition (macro_shape) an equate of
				 * constants is another symbol of the source, as before
				 * equates went in: an argument there may be a label */
				v->ok = 0;
			} else {
				for (int t = 0; t < sub.x.n; t++)
					r68x_add(&v->x, sub.x.t[t].sym, strlen(sub.x.t[t].sym), sub.x.t[t].coef);
				v->c = sub.c;
				lin_add_label(v, &sub, 0);
			}
			r68x_free(&sub.x);
			v->p = q;
			return;
		}
		if (external_name(p, q - p)) {
			r68x_add(&v->x, p, q - p, 1);
		} else if (!lin_params && v->labc == 0 && label_section(p, q - p)) {
			/* a label whose section is known: its section's alias in
			 * the note, the rest of it after (r68x_expression) */
			v->lab = p;
			v->labn = q - p;
			v->labc = 1;
		} else {
			v->ok = 0;
		}
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
		struct lin r = { { NULL, 0 }, 0, 1, v->p + len, NULL, 0, 0 };
		lin_binary(&r, prec + 1);
		v->p = r.p;
		v->ok &= r.ok;
		char o = *as;
		if (o == '+' || o == '-') {
			for (int i = 0; i < r.x.n; i++)
				r68x_add(&v->x, r.x.t[i].sym, strlen(r.x.t[i].sym), o == '-' ? -r.x.t[i].coef : r.x.t[i].coef);
			v->c = o == '-' ? v->c - r.c : v->c + r.c;
			lin_add_label(v, &r, o == '-');
		} else if (v->labc != 0 || r.labc != 0) {
			v->ok = 0;	/* a label times something, shifted, ... */
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

/* The alias of section SEC (label_section's letter) of the current psect,
 * r68l_<psect>_<section>, into NAME (SIZE chars): a hidden global at the
 * section's start, which a link-time expression's note names for a label's
 * section (Lab+Ext: r68l_x_bss+Ext, the label's offset added outside);
 * rof2elf names a ROF's local reference so, from the ROF's (the psect's)
 * name, characters other than letters, digits and _ as _. Noted for the
 * prelude (st.aliases). 0 if there is no psect. */
static int label_alias(char sec, char *name, size_t size)
{
	static const char secs[] = "tdbDB";
	static const char *const names[] = { "text", "data", "bss", "rdata", "rbss" };
	const char *w = sec ? strchr(secs, sec) : NULL;
	if (w == NULL || st.cur_psect == NULL || *st.cur_psect == '\0')
		return 0;
	size_t n = (size_t)snprintf(name, size, "r68l_%s_%s", st.cur_psect, names[w - secs]);
	if (n >= size)
		return 0;
	for (char *c = name + 5; *c && c < name + 5 + strlen(st.cur_psect); c++)
		if (!isalnum((unsigned char)*c) && *c != '_')
			*c = '_';
	char line[128];
	snprintf(line, sizeof line, "%c %s\n", sec, name);
	if (st.aliases.s == NULL || strstr(st.aliases.s, line) == NULL)
		bputs(&st.aliases, line);
	return 1;
}

/* Expression text S as a link-time expression: null if it isn't one, or
 * one relocation takes it as it is. A label of this source in it (one,
 * added, with external names: Block+Field) is its section's alias in the
 * note (label_alias), (label-alias) added to the symbol: the label's
 * offset, which gas computes. */
static char *r68x_expression(const char *s)
{
	struct lin v = { { NULL, 0 }, 0, 1, s, NULL, 0, 0 };
	lin_binary(&v, P_SHIFT);
	char alias[96] = "";
	if (v.ok && *v.p == '\0' && v.labc != 0) {
		/* not PC-relative (Table+Off(pc)): the linkers make the
		 * expression's symbol absolute, which a PC-relative
		 * relocation can't take (r68's ROF has the distance in the
		 * bytes and Off absolute, which gas can't write) */
		if (v.labc != 1 || v.x.n == 0 || twin_mode ||
		    !label_alias(label_section(v.lab, v.labn), alias, sizeof alias))
			v.ok = 0;
		else
			r68x_add(&v.x, alias, strlen(alias), 1);
	}
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
		if (*alias) {
			struct buf ls = { 0 };
			put_symbol(&ls, v.lab, v.labn);
			bprintf(&b, "+(%s-%s)", ls.s, alias);
			free(ls.s);
		}
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
 * gas keeps the value an earlier use saw); <k> numbers the macro's
 * distinct expressions of terms (two lines may share one). Null if
 * it isn't one: another symbol of the source, another operator, one
 * term. */
static char *macro_shape(const char *s)
{
	struct lin v = { { NULL, 0 }, 0, 1, s, NULL, 0, 0 };
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

/* Expression text S as a link-time expression where no relocation takes it
 * (a sum of external names, one subtracted or multiplied, or a label of
 * this source plus external names): r68x_<hash> plus the rest (see
 * r68x_expression), or in a macro definition an alias each call sets
 * (macro_shape); null if it is left as it is. Not where a field must be a
 * number (fold_all: a quick value, a size, a conditional; r68 refuses an
 * external there too) nor in equ and set (!r68x_ok). */
static char *fold_equates(const char *s)
{
	if (!r68x_ok || fold_all)
		return NULL;
	size_t n = strlen(s);
	int nsym = 0, other_op = 0, neg = 0;
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
			 * external and a number, a relocation takes it */
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
	if (!(nsym > 1 || neg || (nsym == 1 && other_op)))
		return NULL;
	/* in a macro definition: a link-time expression of the arguments,
	 * through an alias each call defines */
	if (params && st.in_macro && st.nmacros > 0) {
		char *x = macro_shape(s);
		if (x != NULL) {
			if (fold_log != NULL)
				fprintf(fold_log, "%s:%d\tr68xm\t%s\t%s\n", log_path, log_line, s, x);
			annotate = 1;
		}
		return x;
	}
	/* a sum of external names (ext_a+EXT_B, lab_a-lab_b), one subtracted
	 * (-M$Exec), a label of this source plus external names
	 * (lab+Ext), which no relocation takes: a link-time expression */
	char *x = r68x_expression(s);
	if (x != NULL) {
		if (fold_log != NULL)
			fprintf(fold_log, "%s:%d\tr68x\t%s\t%s\n", log_path, log_line, s, x);
		annotate = 1;
	}
	return x;
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

/* NAME's equ or set value '.-LABEL' (A, N chars), LABEL a vsect label
 * defined before (Size equ .-Label): r68's '.' is the offset counter
 * there too (the manuals: "the current value of the offset origin"). r68
 * makes the symbol one of LABEL's section, the counter less LABEL's offset,
 * which the link relocates (the global symbol in the ROF), but a use in the
 * same source the counter less LABEL's address (a negated reference, which
 * ELF can't express: an error, see parse_unary). Written into B as the
 * former, from the section's start (r68__bdata, r68__bbss in the prelude);
 * gas leaves such a symbol out of its object, so it is also noted for
 * m68k-os9-r68, which adds it (.r68dlv, in translate's prelude). 0 if A is
 * not of that form (or the label is in a remote section). */
static int dot_less_vsect(struct buf *b, const char *name, const char *a, size_t n)
{
	size_t i = 0;
	while (i < n && isspace((unsigned char)a[i]))
		i++;
	if (i >= n || a[i] != '.' || (i + 1 < n && symchar((unsigned char)a[i + 1])))
		return 0;
	for (i++; i < n && isspace((unsigned char)a[i]); i++)
		;
	if (i >= n || a[i] != '-')
		return 0;
	for (i++; i < n && isspace((unsigned char)a[i]); i++)
		;
	size_t s = i;
	if (s >= n || !symstart((unsigned char)a[s]))
		return 0;
	while (i < n && symchar((unsigned char)a[i]))
		i++;
	size_t e = i;
	while (i < n && isspace((unsigned char)a[i]))
		i++;
	if (i != n)
		return 0;
	const char *base = NULL;
	for (int k = st.nvlabs - 1; k >= 0 && base == NULL; k--)
		if (strlen(st.vlabs[k]) == e - s && strncmp(st.vlabs[k], a + s, e - s) == 0)
			base = strcmp(st.vsecs[k], ".bss") == 0 ? "r68__bbss" :
			       strcmp(st.vsecs[k], ".data") == 0 ? "r68__bdata" : "";
	if (base == NULL || *base == '\0')
		return 0;
	struct buf nm = { 0 };
	put_symbol(&nm, a + s, e - s);
	bprintf(b, "%s+(r68__ofs-(%s-%s))", base, nm.s, base);
	free(nm.s);
	if (name != NULL && !name_in(st.dlvs, st.ndlvs, name, strlen(name))) {
		st.dlvs = xrealloc(st.dlvs, (st.ndlvs + 1) * sizeof *st.dlvs);
		st.dlvs[st.ndlvs++] = xstrdup(name);
	}
	return 1;
}

static void error_text(struct buf *b, const char *s, size_t n);

/* A use of S (N chars), if one of the .-LABEL symbols (dot_less_vsect):
 * an error, once a line; r68 makes it the counter less LABEL's address,
 * a negated relocation. */
static void dot_less_use(const char *s, size_t n)
{
	if (st.ndlvs == 0 || !name_in(st.dlvs, st.ndlvs, s, n) || st.dlv_line == log_line)
		return;
	st.dlv_line = log_line;
	bputs(&line_before, " r68error <");
	error_text(&line_before, s, n);
	bputs(&line_before, " (.-a vsect label) used in its own source: r68 relocates that negated, "
		"which ELF cannot express>\n");
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

static char *location_as(const char *s, size_t n, const char *repl);

/* Expression S (N chars) with each * meaning the location (not a
 * multiplication) replaced by (*-OFFSET): in r68 a dc list's * is the
 * statement's start, in as the element's; null if it has none. */
static char *location_from(const char *s, size_t n, long offset)
{
	char repl[32];
	snprintf(repl, sizeof repl, "(*-%ld)", offset);
	return location_as(s, n, repl);
}

/* Expression S (N chars) with each * meaning the location replaced by
 * REPL; null if it has none. */
static char *location_as(const char *s, size_t n, const char *repl)
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
			bputs(&b, repl);
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
			/* its length, rounded to the element size (dc.w "er",
			 * Err-*-4: * is the statement's start) */
			if (offset >= 0 && dc_size > 0) {
				long chars = 0;
				char q = pieces[i][0];
				for (size_t c = 1; c < lens[i]; c++) {
					if (pieces[i][c] == q) {
						if (c + 1 < lens[i] && pieces[i][c + 1] == q)
							c++;
						else
							break;
					} else if (pieces[i][c] == '\\' && q == '"' && c + 1 < lens[i]) {
						c++;
					}
					chars++;
				}
				offset += (chars + dc_size - 1) / dc_size * dc_size;
			} else {
				offset = -1;
			}
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
 * there, gas must take it too (NAME__final, defined at the end). */
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

/* The line marker after lines put before a statement (line_before), which
 * keeps the line numbers; after byte data owed (see maybe_odd). */
static void put_marker(struct buf *b)
{
	if (st.maybe_odd)
		st.marker_owed = 1;
	else
		bprintf(b, "# %d \"%s\"\n", log_line, log_path);
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
	bputc(&pre, '\n');
	put_marker(&pre);
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
 * r68 takes every instruction whatever its -m, gas doesn't. */
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
	/* move ccr,<ea>: the 68010's */
	if (strcmp(base, "move") == 0 && strncasecmp(args, "ccr,", 4) == 0)
		level = 10;
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
 * target: r68 resolves it in the file, gas resolves a global symbol only at
 * link time (a relocation; an error in an 8-bit field, as (pc,Xn) and .s
 * branches have). Written as NAME__r68pc, a local equate of NAME (epilogue),
 * which gas resolves. */
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

/* The names macro calls make (LAB Bad, whose body has \1: bsr.s
 * past\1; VAR X_Y: with V_\1 ds.l 1; a macro calling Flag \3, which
 * has m\1 equ 1<<.), which -b, the twins and external_name need to know as
 * the source's own (macro-label-pcrel). From a line of the pre-scan, its
 * LABEL (null if none; GLOBAL if with ':') and its statement from the
 * operation, OP: a macro's labels made of parameters and the statements of
 * its body noted; a call outside macro definitions expanded for its names,
 * the calls in the body too. */
static struct gmacro {
	char *name;
	char *pat[16];		/* labels with parameters */
	char glob[16];
	int npat;
	char *call[32];		/* the body's statements: "op args" */
	int ncall;
	int ifs;		/* conditionals the body leaves open (so far) */
} *gm;
static int ngm;

/* Whether the macro NAME's body leaves a conditional open, or calls one
 * that does (the pre-scan's view: also where the call is translated
 * before the called macro's definition, a template's in a later use
 * file). */
static int gm_open(const char *name, int depth)
{
	if (depth > 8)
		return 0;
	for (int m = ngm; m-- > 0;) {
		if (strcasecmp(gm[m].name, name) != 0)
			continue;
		if (gm[m].ifs > 0)
			return 1;
		for (int i = 0; i < gm[m].ncall; i++) {
			size_t cn = strcspn(gm[m].call[i], " \t");
			char *c = xstrndup(gm[m].call[i], cn);
			int o = strcasecmp(c, name) != 0 && gm_open(c, depth + 1);
			free(c);
			if (o)
				return 1;
		}
		return 0;
	}
	return 0;
}

/* TEXT with \1..\9 replaced by ARGS (LENS), into a new string. */
static char *gm_subst(const char *text, const char *const *args, const size_t *lens)
{
	struct buf nm = { 0 };
	for (const char *q = text; *q; q++) {
		if (q[0] == '\\' && q[1] >= '1' && q[1] <= '9') {
			int k = q[1] - '1';
			if (args[k])
				bput(&nm, args[k], lens[k]);
			q++;
		} else {
			bputc(&nm, *q);
		}
	}
	return nm.s ? nm.s : xstrdup("");
}

/* The call of macro OP (N chars) with the arguments in A (blank-ended):
 * its names into GLOBALS or st.locals, its body's calls in turn. */
static void gm_call(const char *op, size_t n, const char *a, char ***globals, int *nglobals, int depth)
{
	if (depth > 8)
		return;
	int m = -1;
	for (int i = 0; i < ngm; i++)
		if (strlen(gm[i].name) == n && strncasecmp(gm[i].name, op, n) == 0)
			m = i;
	if (m < 0 || (gm[m].npat == 0 && gm[m].ncall == 0))
		return;
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
		char *nm = gm_subst(gm[m].pat[i], args, lens);
		size_t len = strlen(nm);
		/* an argument with a colon (VAR X_Y: with V_\1 ds.l
		 * 1): a global label */
		int glob = gm[m].glob[i];
		if (len > 1 && nm[len - 1] == ':') {
			nm[--len] = '\0';
			glob = 1;
		}
		if (*nm == '\0' || strchr(nm, '\\') != NULL) {
			free(nm);
		} else if (glob) {
			/* exported (a vsect's label made by the macro has no
			 * xdef of its own) */
			add_global(nm);
			*globals = xrealloc(*globals, (*nglobals + 1) * sizeof **globals);
			(*globals)[(*nglobals)++] = nm;
		} else {
			st.locals = xrealloc(st.locals, (st.nlocals + 1) * sizeof *st.locals);
			st.locals[st.nlocals++] = nm;
		}
	}
	for (int i = 0; i < gm[m].ncall; i++) {
		char *c = gm_subst(gm[m].call[i], args, lens);
		size_t cn = strcspn(c, " \t");
		gm_call(c, cn, c + cn, globals, nglobals, depth + 1);
		free(c);
	}
}

static void scan_macro_labels(const char *label, int global, const char *op, char ***globals, int *nglobals)
{
	static int cur = -1;
	size_t n = strcspn(op, " \t");
	if (n == 5 && strncasecmp(op, "macro", 5) == 0) {
		cur = -1;
		if (label == NULL)
			return;
		gm = xrealloc(gm, (ngm + 1) * sizeof *gm);
		memset(&gm[ngm], 0, sizeof *gm);
		gm[ngm].name = xstrdup(label);
		cur = ngm++;
		return;
	}
	if (n == 4 && strncasecmp(op, "endm", 4) == 0) {
		cur = -1;
		return;
	}
	if (cur >= 0) {
		char base[32];
		size_t bn = strcspn(op, ". \t");
		if (bn < sizeof base) {
			for (size_t i = 0; i < bn; i++)
				base[i] = (char)tolower((unsigned char)op[i]);
			base[bn] = '\0';
			if (is_cond(base))
				gm[cur].ifs++;
			else if (strcmp(base, "endc") == 0 && gm[cur].ifs > 0)
				gm[cur].ifs--;
		}
		/* (a global label without parameters too:
		 * Entry: equ *, exported where the macro is called) */
		if (label && (strchr(label, '\\') || global) && strstr(label, "\\@") == NULL && gm[cur].npat < 16) {
			gm[cur].glob[gm[cur].npat] = (char)global;
			gm[cur].pat[gm[cur].npat++] = xstrdup(label);
		}
		if (n > 0 && gm[cur].ncall < 32) {
			size_t e = n + strspn(op + n, " \t");
			e += strcspn(op + e, " \t");
			gm[cur].call[gm[cur].ncall++] = xstrndup(op, e);
		}
		return;
	}
	if (n > 0)
		gm_call(op, n, op + n, globals, nglobals, 0);
}

/* Collects the labels PATH and the files it uses define without ':' (for
 * -b), conditionals not considered; a name also defined with ':' is left
 * out (global). */
/* Labels' sections, from the pre-scan (scan_labels): t code, d and b a
 * vsect's initialized and uninitialized data, D and B a remote vsect's; ?
 * a name defined in more than one. */
static void add_label_section(const char *name, char sec)
{
	for (int i = 0; i < st.nls; i++)
		if (strcmp(st.lsnames[i], name) == 0) {
			if (st.lssecs[i] != sec)
				st.lssecs[i] = '?';
			return;
		}
	st.lsnames = xrealloc(st.lsnames, (st.nls + 1) * sizeof *st.lsnames);
	st.lssecs = xrealloc(st.lssecs, st.nls + 1);
	st.lsnames[st.nls] = xstrdup(name);
	st.lssecs[st.nls++] = sec;
}

/* The section of label S (N chars), or 0 if not known (not a label of this
 * source, in more than one section, or made by a macro). */
static char label_section(const char *s, size_t n)
{
	for (int i = 0; i < st.nls; i++)
		if (strlen(st.lsnames[i]) == n && strncmp(st.lsnames[i], s, n) == 0)
			return st.lssecs[i] == '?' ? 0 : st.lssecs[i];
	return 0;
}

/* Operations whose label isn't a location (outside a vsect: a code label
 * otherwise). */
static int non_location(const char *base)
{
	static const char *const ops[] = { "equ", "set", "do", "lo", "org", "macro", "endm", "rept", "endr", "else",
		"endc", "use", "nam", "ttl", "opt", "psect", "vsect", "ends", "endsect", "xdef", "xref", "spc", "pag",
		"page", "fail", "end", "align", "com", NULL };
	for (const char *const *o = ops; *o; o++)
		if (strcmp(base, *o) == 0)
			return 1;
	return is_cond(base);
}

static int scan_vsect, scan_remote, scan_macro_def;

/* Records NAME (N chars) equ ARGS (its operand field) for prescan_equate;
 * a name defined twice (in conditionals) gets no expression. */
static void note_prescan_equate(const char *name, size_t n, const char *args)
{
	while (*args == ' ' || *args == '\t')
		args++;
	size_t an = 0;
	while (args[an] && !isspace((unsigned char)args[an]))
		an++;
	for (int i = 0; i < st.nequs; i++)
		if (strlen(st.equ_names[i]) == n && strncmp(st.equ_names[i], name, n) == 0) {
			free(st.equ_texts[i]);
			st.equ_texts[i] = NULL;
			return;
		}
	st.equ_names = xrealloc(st.equ_names, (st.nequs + 1) * sizeof *st.equ_names);
	st.equ_texts = xrealloc(st.equ_texts, (st.nequs + 1) * sizeof *st.equ_texts);
	st.equ_names[st.nequs] = xstrndup(name, n);
	st.equ_texts[st.nequs++] = xstrndup(args, an);
}

/* The expression of equate S (N chars) from the pre-scan, NULL if there is
 * none or more than one, or S is also a label (in a section). */
static const char *prescan_equate(const char *s, size_t n)
{
	if (label_section(s, n))
		return NULL;
	for (int i = 0; i < st.nequs; i++)
		if (strlen(st.equ_names[i]) == n && strncmp(st.equ_names[i], s, n) == 0)
			return st.equ_texts[i];
	return NULL;
}

static void scan_labels(const char *path, int depth, char ***globals, int *nglobals)
{
	if (depth > 16)
		return;
	if (depth == 0)
		scan_vsect = scan_remote = scan_macro_def = 0;
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
				if (!scan_macro_def && strncasecmp(o, "equ", 3) == 0 && isspace((unsigned char)o[3]))
					note_prescan_equate(p, n, o + 3);
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
		{
			/* the label's section (label_section): code, or in a vsect
			 * its data (ds uninitialized, else initialized; remote) */
			char ob[16];
			size_t on = 0;
			while (p[on] && !isspace((unsigned char)p[on]) && p[on] != '.' && on < sizeof ob - 1) {
				ob[on] = (char)tolower((unsigned char)p[on]);
				on++;
			}
			ob[on] = '\0';
			if (scan_macro_def) {
				if (strcmp(ob, "endm") == 0)
					scan_macro_def = 0;
			} else if (strcmp(ob, "macro") == 0) {
				scan_macro_def = 1;
			} else if (strcmp(ob, "vsect") == 0) {
				scan_vsect = 1;
				scan_remote = 0;
				for (const char *r = p + on; *r && !scan_remote; r++)
					scan_remote = strncasecmp(r, "remote", 6) == 0;
			} else if (strcmp(ob, "ends") == 0 || strcmp(ob, "endsect") == 0) {
				scan_vsect = 0;
			} else if (label != NULL && *label && strchr(label, '\\') == NULL) {
				char sec = 0;
				if (scan_vsect) {
					if (strcmp(ob, "ds") == 0)
						sec = scan_remote ? 'B' : 'b';
					else if (on == 0 || strcmp(ob, "dc") == 0 || strcmp(ob, "dz") == 0 ||
						 strcmp(ob, "dcb") == 0)
						sec = scan_remote ? 'D' : 'd';
				} else if (on == 0 || !non_location(ob)) {
					sec = 't';
				}
				if (sec)
					add_label_section(label, sec);
			}
		}
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

/* The opcode word of branch or db<cc> BASE (bra $6000, bhi $6200, dbra
 * $51C8), *DB set for a db<cc>; -1 if BASE is neither. */
static int branch_code(const char *base, int *db)
{
	static const char *const cc[] = { "t", "f", "hi", "ls", "cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi", "ge",
		"lt", "gt", "le", NULL };
	*db = 0;
	const char *c;
	if (strcmp(base, "bra") == 0)
		return 0x6000;
	if (strcmp(base, "bsr") == 0)
		return 0x6100;
	if (strcmp(base, "dbra") == 0) {
		*db = 1;
		return 0x51c8;
	}
	if (base[0] == 'd' && base[1] == 'b') {
		*db = 1;
		c = base + 2;
	} else if (is_branch(base)) {
		c = base + 1;
	} else {
		return -1;
	}
	if (strcmp(c, "hs") == 0)
		c = "cc";
	else if (strcmp(c, "lo") == 0)
		c = "cs";
	for (int k = *db ? 0 : 2; cc[k]; k++)
		if (strcmp(cc[k], c) == 0)
			return *db ? 0x50c8 | k << 8 : 0x6000 | k << 8;
	return -1;
}

/* Whether ARGS is just a symbol this source doesn't define (no label,
 * equ or set of that name, not a register or macro parameter): an external
 * as the target of a short branch. */
static int short_external(const char *args)
{
	size_t n = strlen(args);
	if (n == 0 || !symstart((unsigned char)args[0]))
		return 0;
	for (size_t i = 0; i < n; i++)
		if (!symchar((unsigned char)args[i]))
			return 0;
	return !defined_name(args, n) && !is_reg(args, n) && !global_label(args, n);
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

/* The value of name S (N chars) if it is known so far (cv_names). */
static int cv_known(const char *s, size_t n, long *v)
{
	for (int i = ncv - 1; i >= 0; i--)
		if (strlen(cv_names[i]) == n && strncmp(cv_names[i], s, n) == 0) {
			*v = cv_values[i];
			return 1;
		}
	return 0;
}

static int cv_expr(const char **p, long *v);
/* cv_prescan: names not equated yet are looked up in the pre-scan's equates
 * (prescan_equate), cv_depth deep */
static int cv_prescan, cv_depth;

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
		/* with cv_prescan: an equate further on, by its expression */
		const char *t = cv_prescan && cv_depth < 16 ? prescan_equate(*p, q - *p) : NULL;
		if (t) {
			cv_depth++;
			int ok = cv_expr(&t, v) && *t == '\0';
			cv_depth--;
			if (ok)
				*p = q;
			return ok;
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
/* The equ statements' expressions (outside macro bodies), for link-time
 * expressions of equates of external names (equate_text). */
static char **eq_names, **eq_texts;
static int neq;

static const char *equate_text(const char *s, size_t n)
{
	for (int i = neq; i-- > 0;)
		if (strlen(eq_names[i]) == n && strncmp(eq_names[i], s, n) == 0)
			return eq_texts[i];
	return NULL;
}

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

/* The offset counter of org/do/lo (the prelude's r68__ofs) as far as it is
 * known here, so that a do/lo name has a value like an equate (do.w 1 then
 * name(pc): a constant before pc). Unknown in a conditional (both of its
 * branches come here) up to the next org of a constant, and for good once
 * a macro body has org, do or lo (its calls move the counter where it
 * isn't seen). */
static long ofs_value;
static int ofs_known = 1, ofs_macro;

static int constant_value(const char *s, long *v);

static void note_offset(const char *base, const char *label, const char *args, char size)
{
	long v, n;
	if (st.in_macro) {
		ofs_macro = 1;
		return;
	}
	if (ofs_macro || st.cond_depth > 0 || args == NULL) {
		ofs_known = 0;
		return;
	}
	if (strcmp(base, "org") == 0) {
		ofs_known = constant_value(args, &v);
		ofs_value = v;
		return;
	}
	if (!ofs_known || !constant_value(args, &n)) {
		ofs_known = 0;
		return;
	}
	long bytes = size == 'b' ? 1 : size == 'l' ? 4 : 2;
	if (strcmp(base, "do") == 0) {
		if (bytes != 1)
			ofs_value = (ofs_value + 1) / 2 * 2;
		v = ofs_value;
		ofs_value += bytes * n;
	} else {
		ofs_value -= bytes * n;
		if (bytes != 1)
			ofs_value &= ~1L;
		v = ofs_value;
	}
	if (label == NULL)
		return;
	cv_names = xrealloc(cv_names, (ncv + 1) * sizeof *cv_names);
	cv_values = xrealloc(cv_values, (ncv + 1) * sizeof *cv_values);
	cv_names[ncv] = xstrdup(label);
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

/* constant_value, with the equates of the whole source (those further on
 * too). */
static int source_value(const char *s, long *v)
{
	cv_prescan = 1;
	int c = constant_value(s, v);
	cv_prescan = 0;
	return c;
}

/* Whether expression S (N chars) is a constant (source_value). */
static int source_constant(const char *s, size_t n)
{
	char *t = xstrndup(s, n);
	long v;
	int c = source_value(t, &v);
	free(t);
	return c;
}

/* Whether expression S (N chars) has only numbers and names equated so
 * far, so that gas can compute it (and mask it: r68 writes a move.b
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

/* A moveq value of a number 128..255 added to more terms ($80+Ext):
 * r68 takes the byte, gas wants -128..127. */
static int moveq_high_sum(const char *s, size_t n)
{
	const char *p = s, *end = s + n;
	int radix = 10;
	if (p < end && (*p == '$' || *p == '%')) {
		radix = *p == '$' ? 16 : 2;
		p++;
	}
	long v = 0;
	const char *d = p;
	for (; p < end && isxdigit((unsigned char)*p); p++) {
		int c = isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10);
		if (c >= radix)
			return 0;
		v = v * radix + c;
		if (v > 255)
			return 0;
	}
	return p > d && p < end && (*p == '+' || *p == '-') && v >= 128;
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

/* The names of expression S (N chars) not equated so far that a macro's
 * expansion defines (m\1 equ 1<<. called as Bit Sign: mSign; the pre-scan's
 * names without a section of their own), each followed by a space, into
 * NAMES; 0 where there are none, or a name of another kind (an external), the
 * location counter or a parameter. */
static int macro_equ_names(struct buf *names, const char *s, size_t n)
{
	int k = 0;
	if (memchr(s, '\\', n) != NULL || has_location(s, n))
		return 0;
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
			int known = 0;
			for (int q = 0; q < st.nequates && !known; q++)
				known = strlen(st.equates[q]) == j - i && strncmp(st.equates[q], s + i, j - i) == 0;
			if (!known) {
				if (!source_label(s + i, j - i) || label_section(s + i, j - i) ||
				    name_in(st.vlabs, st.nvlabs, s + i, j - i))
					return 0;
				bput(names, s + i, j - i);
				bputc(names, ' ');
				k++;
			}
			i = j;
		} else {
			i++;
		}
	}
	return k;
}

/* A moveq (MOVEQ) or move.b immediate S (N chars) with names a macro's
 * expansion defines (macro_equ_names): r68 knows their values and takes the
 * byte (moveq #mSign with 128 is $80, move.b #^mBit $00EF); the translation
 * can't, since gas expands the macros. .Lr68q set before the line, as the
 * known values' form where gas has the names by then, else as the value
 * (an external of the same name: relocated as before). */
static int macro_equ_imm(const char *s, size_t n, int moveq)
{
	struct buf names = { 0 }, e = { 0 };
	if (macro_equ_names(&names, s, n) == 0) {
		free(names.s);
		return 0;
	}
	expression(&e, s, n);
	bputs(&line_before, ".Lr68d set 1\n");
	for (char *p = names.s, *q; (q = strchr(p, ' ')) != NULL; p = q + 1)
		bprintf(&line_before, " ifndef %.*s\n.Lr68d set 0\n endc\n", (int)(q - p), p);
	bputs(&line_before, " ifne .Lr68d\n");
	if (moveq)
		bprintf(&line_before, ".Lr68q set ((%s)&$ff)-(((%s)&$80)<<1)\n", e.s, e.s);
	else
		bprintf(&line_before, ".Lr68q set (%s)&$ff\n", e.s);
	bprintf(&line_before, " else\n.Lr68q set %s\n endc\n", e.s);
	free(names.s);
	free(e.s);
	return 1;
}

/* Text S (N chars) for r68error's <...> argument and .error's "...": no
 * angle brackets or double quotes. */
static void error_text(struct buf *b, const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		/* a backslash doubled: .error's string takes escapes (a DOS
		 * path's ..\tables.d had a tab) */
		if (s[i] == '\\')
			bputc(b, '\\');
		/* a single quote as a backquote: gas lost track of a quote in
		 * a <...> macro argument where its input buffer ended there (a
		 * conditional left open, "end of file inside conditional";
		 * found 2026-10-15 with an r68error text 'to' or 'from') */
		bputc(b, s[i] == '<' ? '(' : s[i] == '>' ? ')' : s[i] == '"' || s[i] == '\'' ? '`' : s[i]);
	}
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

/* ----- Separate files (-s) ----- */

/* -s: each use file translated into a file of its own, included; -x: the
 * outputs' extension; out_dir: where the use files' outputs go (the
 * top-level output's directory) */
static int separate;
static const char *out_ext;
static char *out_dir;

/* The use files' outputs written so far: the use file, the output's name
 * (in out_dir) and its text, to reuse it where a use file's translation
 * comes out the same again */
static char **sep_sources, **sep_names, **sep_texts;
static int nsep;

/* The output name of source PATH (its last component): a final .a becomes
 * .s, a .s source gets .gas.s, any other name .s appended; with -x a final
 * .a or .s becomes .<ext>, another name gets .<ext> appended. */
static char *output_name(const char *path)
{
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	size_t n = strlen(base);
	int a = n > 2 && base[n - 2] == '.' && (base[n - 1] == 'a' || base[n - 1] == 'A');
	int s = n > 2 && base[n - 2] == '.' && (base[n - 1] == 's' || base[n - 1] == 'S');
	struct buf b = { 0 };
	if (out_ext != NULL)
		bprintf(&b, "%.*s.%s", (int)(a || s ? n - 2 : n), base, out_ext);
	else if (a)
		bprintf(&b, "%.*s.s", (int)(n - 2), base);
	else if (s)
		bprintf(&b, "%.*s.gas.s", (int)(n - 2), base);
	else
		bprintf(&b, "%s.s", base);
	return b.s;
}

static int file_exists(const char *path)
{
	struct stat sb;
	return stat(path, &sb) == 0;
}

static int is_directory(const char *path)
{
	struct stat sb;
	return stat(path, &sb) == 0 && S_ISDIR(sb.st_mode);
}

/* Whether paths A and B are the same file (both existing). */
static int same_file(const char *a, const char *b)
{
	struct stat sa, sb;
	return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

/* The output of use file PATH for TEXT (its translation): the name of one
 * written before with the same text, else a new file in out_dir named by
 * output_name, with .2, .3 ... before the extension where that name is
 * taken (another file of that name, or the same one translated
 * differently: in another state of the conditionals' symbols, say). */
static const char *separate_output(const char *path, const char *text)
{
	for (int i = 0; i < nsep; i++)
		if (strcmp(sep_sources[i], path) == 0 && strcmp(sep_texts[i], text) == 0)
			return sep_names[i];
	char *name = output_name(path);
	for (int k = 2;; k++) {
		int taken = 0;
		for (int i = 0; i < nsep && !taken; i++)
			taken = strcmp(sep_names[i], name) == 0;
		if (!taken)
			break;
		char *base = output_name(path);
		char *dot = strrchr(base, '.');
		struct buf b = { 0 };
		bprintf(&b, "%.*s.%d%s", (int)(dot - base), base, k, dot);
		free(base);
		free(name);
		name = b.s;
	}
	char *file = join(out_dir, strlen(out_dir), name);
	/* never over an existing file (an input, an earlier run's output) */
	if (file_exists(file))
		die("%s: exists; -s writes no file over another", file);
	FILE *f = fopen(file, "w");
	if (f == NULL)
		die("cannot write %s", file);
	note_made(file);
	if (fputs(text, f) == EOF || fclose(f) != 0)
		die("cannot write %s", file);
	sep_sources = xrealloc(sep_sources, (nsep + 1) * sizeof *sep_sources);
	sep_names = xrealloc(sep_names, (nsep + 1) * sizeof *sep_names);
	sep_texts = xrealloc(sep_texts, (nsep + 1) * sizeof *sep_texts);
	sep_sources[nsep] = xstrdup(path);
	sep_names[nsep] = name;
	sep_texts[nsep++] = xstrdup(text);
	return name;
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
		/* An OS-9 path of the machine it was written on (use
		 * /dd/defs/oskdefs.d, /h0/defs/...) that isn't here: its file
		 * name in the -u directories, as if they were /dd/defs. */
		const char *base = strrchr(name, '/') + 1;
		for (int i = 0; found == NULL && *base && i < st.nsearch; i++) {
			char *f = join(st.search[i], strlen(st.search[i]), base);
			found = find_ci(f);
			free(f);
		}
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
/* S as a macro argument in <...>: gas's MRI macros take ! there as an escape,
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


/* a call of a macro using \Ln made directly, not through r68L */
static int direct_lcall;

static void macro_arguments(struct buf *out_b, const char *args, const char *name)
{
	struct buf *b = out_b;
	int lmacro = is_lmacro(name) && !direct_lcall;
	for (int i = 0; i < st.nmacros && !lmacro; i++)
		if ((ci_eq(st.macros[i], name) || ci_eq(st.macros[i], macro_orig(name))) && st.macro_viaL[i] && !direct_lcall)
			lmacro = 1;
	const char *pieces[64];
	size_t lens[64];
	/* \' is a quote (MAC.w #\'@7\' in a macro body,
	 * MAC.l #\'Read\' outside): as one here */
	char *unesc = NULL;
	if (strstr(args, "\\'")) {
		unesc = xstrdup(args);
		char *w = unesc;
		for (const char *r = args; *r; r++) {
			/* a quote of the argument's text, not one around it
			 * (MAC.l \'AbCd\': 'AbCd', a number): \1 until the
			 * argument is written */
			if (r[0] == '\\' && r[1] == '\'') {
				*w++ = '\1';
				r++;
			} else {
				*w++ = *r;
			}
		}
		*w = '\0';
		args = unesc;
	}
	int k = split_commas(args, strlen(args), pieces, lens, 64);
	unsigned ops = 0, mem = 0, twin = 0;
	for (int i = 0; i < st.nmacros; i++)
		if (ci_eq(st.macros[i], name) || ci_eq(st.macros[i], macro_orig(name))) {
			ops = st.macro_ops[i];
			mem = st.macro_mem[i];
			twin = st.macro_twin[i];
		}
	int save_ok = r68x_ok;
	r68x_ok = 0;	/* left as it is: see r68x_ok */
	for (int i = 0; i < k; i++) {
		int save = fold_all;
		if (i)
			bputc(out_b, ',');
		/* the argument's text, in <> where angled (put_angle) */
		struct buf arg = { 0 };
		int angled = 0;
		b = &arg;
		if (lens[i] == 0 && k > 1) {
			/* an empty argument (MAC X,Y,,,,,,,): <>, as
			 * gas's MRI macros drop a bare one, which counts in r68's
			 * \# and moves the later ones */
			angled = 1;
		} else if (lens[i] >= 2 && ((pieces[i][0] == '"' && pieces[i][lens[i] - 1] == '"') ||
					    (pieces[i][0] == '\'' && pieces[i][lens[i] - 1] == '\''))) {
			/* r68 strips either kind of quotes from an argument
			 * (MAC '@7' with #'\1' in the body: '@7'); in "...",
			 * a backslash takes the next character, and a string
			 * left in it ("\"ABC: \"", a macro's for a dc.b
			 * \1) is one in gas's quotes, '...' */
			const char *a = pieces[i] + 1;
			size_t an = lens[i] - 2;
			if (pieces[i][0] == '"' && memchr(a, '\\', an) != NULL) {
				struct buf u = { 0 };
				for (size_t c = 0; c < an; c++) {
					if (a[c] == '\\' && c + 1 < an)
						c++;
					bputc(&u, a[c]);
				}
				mri_strings(b, u.s ? u.s : "", u.n);
				free(u.s);
			} else {
				bput(b, a, an);
			}
			angled = 1;
		} else if (lens[i] >= 2 && pieces[i][0] == '\\' && pieces[i][1] == '"') {
			/* an escaped quote outside quotes (MAC
			 * \""abc> \""): the quote, then quoted parts without
			 * their quotes, a backslash taking the next character
			 * ("abc> ") */
			struct buf u = { 0 };
			for (size_t c = 0; c < lens[i]; c++) {
				if (pieces[i][c] == '\\' && c + 1 < lens[i])
					bputc(&u, pieces[i][++c]);
				else if (pieces[i][c] != '"')
					bputc(&u, pieces[i][c]);
			}
			mri_strings(b, u.s ? u.s : "", u.n);
			free(u.s);
			angled = 1;
		} else if (memchr(pieces[i], '"', lens[i]) != NULL) {
			/* quotes inside an argument (MAC.l #"'"Done"'":
			 * #'Done'): r68 strips them too */
			struct buf t = { 0 };
			for (size_t c = 0; c < lens[i]; c++)
				if (pieces[i][c] != '"')
					bputc(&t, pieces[i][c]);
			operand(b, t.s ? t.s : "", t.n, 0);
			free(t.s);
		} else if (memchr(pieces[i], '=', lens[i]) != NULL) {
			/* = (Mac >150,=,.b,V_Done(a2)): a keyword argument
			 * to gas's macros; in <> */
			bput(b, pieces[i], lens[i]);
			angled = 1;
		} else if (lens[i] >= 4 && (strncasecmp(pieces[i], "addi", 4) == 0 || strncasecmp(pieces[i], "subi", 4) == 0) &&
			   (lens[i] == 4 || pieces[i][4] == '.') && i < 9 && (ops & (1u << i))) {
			/* A mnemonic argument (MAC subi.w,#1,V_Count): as in
			 * an instruction, add/sub for r68's quick forms; only for
			 * a parameter the body uses as an operation (not a macro's
			 * bit names, SUBI). */
			bput(b, pieces[i], 3);
			bput(b, pieces[i] + 4, lens[i] - 4);
			annotate = 1;
		} else if (lens[i] >= 3 && pieces[i][lens[i] - 2] == '.' && strchr("bwlsdxpBWLSDXP", pieces[i][lens[i] - 1]) &&
			   strspn(pieces[i], "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") == lens[i] - 2) {
			/* a mnemonic with its size (MAC tst.b,V_Mine): as it is,
			 * not a name with a dot (see put_symbol) */
			bput(b, pieces[i], lens[i]);
		} else if (lens[i] > 0 && (isalpha((unsigned char)pieces[i][0]) || pieces[i][0] == '_') &&
			   strspn(pieces[i], "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$") == lens[i] &&
			   clashes(pieces[i], lens[i])) {
			/* a name gas takes for a register (TC: a bit TC,
			 * which the body makes mTC and bTC): as it is, a part
			 * of the names the body makes (NAME__r68 made bTC__r68) */
			bput(b, pieces[i], lens[i]);
		} else if (i < 9 && (twin & (1u << i)) && global_label(pieces[i], lens[i])) {
			/* a global label for a branch target or a displacement
			 * on pc (if.set a,ne.s,There: b\2 \3): its
			 * twin, as outside macros */
			put_twin(b, pieces[i], lens[i]);
		} else if (memchr(pieces[i], '\1', lens[i]) != NULL) {
			char *t = xstrndup(pieces[i], lens[i]);
			for (char *c = t; *c; c++)
				if (*c == '\1')
					*c = '\'';
			operand(b, t, lens[i], 0);
			free(t);
		} else if (i < 9 && (mem & (1u << i)) && pieces[i][0] != '#' && memchr(pieces[i], '(', lens[i]) == NULL &&
			   !register_list(pieces[i], lens[i]) && !(lens[i] > 2 && pieces[i][lens[i] - 2] == '.')) {
			/* an address for a parameter the body uses only as an
			 * address operand (MAC.start 256: move.l
			 * a5,\1): absolute long, as a bare one is */
			operand(b, pieces[i], lens[i], 1);
		} else {
			operand(b, pieces[i], lens[i], 0);
		}
		b = out_b;
		for (size_t c = 0; c < arg.n; c++)
			if (arg.s[c] == '\1')
				arg.s[c] = '\'';
		const char *t = arg.s ? arg.s : "";
		if (lmacro && (angled || strpbrk(t, "!<>") != NULL)) {
			/* through r68L, which passes it on in <> again: its
			 * !, < and > escaped twice (Mac Hdr_,Err!) */
			/* and in <> on the r68L call line too: <!<...!>> (Mac
			 * >150,=,...: gas took a bare = for a keyword argument;
			 * Mac Abcd,k<>0 lost its <>) */
			bputs(b, "<!<");
			for (const char *c = t; *c; c++) {
				if (*c == '!' || *c == '<' || *c == '>')
					bputs(b, "!!!");
				bputc(b, *c);
			}
			bputs(b, "!>>");
		} else if (angled) {
			put_angle(b, t, arg.n);
		} else {
			bput(b, t, arg.n);
		}
		free(arg.s);
		fold_all = save;
	}
	r68x_ok = save_ok;
	free(unesc);
}

/* Whether the macro body in TEXT (up to its endm line) calls a macro
 * through r68L (one using \Ln, so far): its arguments pass two levels of
 * <> (Mac Hdr_,Err! to Sub). */
static int calls_lmacro(const char *text)
{
	if (text == NULL)
		return 0;
	for (const char *p = text; *p;) {
		const char *e = p + strcspn(p, "\r\n");
		const char *w = p;
		if (*w != '*' && *w != ';') {
			if (!isspace((unsigned char)*w))
				while (w < e && !isspace((unsigned char)*w))
					w++;
			while (w < e && isspace((unsigned char)*w))
				w++;
			size_t n = 0;
			while (w + n < e && !isspace((unsigned char)w[n]))
				n++;
			if (n == 4 && strncasecmp(w, "endm", 4) == 0)
				return 0;
			if (n > 0 && n < 64) {
				char op[64];
				memcpy(op, w, n);
				op[n] = '\0';
				if (is_lmacro(op))
					return 1;
			}
		}
		p = e;
		while (*p == '\r' || *p == '\n')
			p++;
	}
	return 0;
}

/* The parameters (bit n-1 for \n) the macro body in TEXT (up to its endm
 * line) uses as an operation: a statement's operation beginning with \n. */
static unsigned op_params(const char *text)
{
	unsigned mask = 0;
	if (text == NULL)
		return 0;
	for (const char *p = text; *p;) {
		const char *e = p + strcspn(p, "\r\n");
		const char *w = p;
		if (*w != '*' && *w != ';') {
			if (!isspace((unsigned char)*w))
				while (w < e && !isspace((unsigned char)*w))
					w++;
			while (w < e && isspace((unsigned char)*w))
				w++;
			if (e - w >= 4 && strncasecmp(w, "endm", 4) == 0 && (e - w == 4 || isspace((unsigned char)w[4])))
				return mask;
			if (e - w >= 2 && w[0] == '\\' && w[1] >= '1' && w[1] <= '9')
				mask |= 1u << (w[1] - '1');
		}
		p = e;
		while (*p == '\r' || *p == '\n')
			p++;
	}
	return mask;
}

/* The parameters (bit n-1 for \n) the macro body in TEXT (up to its endm
 * line) uses only as a whole operand of an instruction that takes an
 * address (memory_instruction), nowhere else. */
static unsigned mem_params(const char *text)
{
	unsigned mem = 0, other = 0;
	if (text == NULL)
		return 0;
	for (const char *p = text; *p;) {
		const char *e = p + strcspn(p, "\r\n");
		const char *w = p;
		if (*w != '*' && *w != ';') {
			if (!isspace((unsigned char)*w))
				while (w < e && !isspace((unsigned char)*w))
					w++;
			while (w < e && isspace((unsigned char)*w))
				w++;
			const char *op = w;
			while (w < e && !isspace((unsigned char)*w))
				w++;
			size_t on = (size_t)(w - op);
			if (on == 4 && strncasecmp(op, "endm", 4) == 0)
				break;
			while (w < e && isspace((unsigned char)*w))
				w++;
			const char *a = w;
			while (w < e && !isspace((unsigned char)*w))
				w++;
			char base[32];
			size_t bn = strcspn(op, ". \t\r\n");
			if (bn >= sizeof base)
				bn = sizeof base - 1;
			for (size_t i = 0; i < bn; i++)
				base[i] = (char)tolower((unsigned char)op[i]);
			base[bn] = '\0';
			int memop = memory_instruction(base);
			const char *pieces[16];
			size_t lens[16];
			int k = split_commas(a, (size_t)(w - a), pieces, lens, 16);
			for (int i = 0; i < k; i++) {
				for (size_t c = 0; c + 1 < lens[i]; c++) {
					if (pieces[i][c] != '\\' || pieces[i][c + 1] < '1' || pieces[i][c + 1] > '9')
						continue;
					unsigned bit = 1u << (pieces[i][c + 1] - '1');
					if (memop && lens[i] == 2)
						mem |= bit;
					else
						other |= bit;
				}
			}
		}
		p = e;
		while (*p == '\r' || *p == '\n')
			p++;
	}
	return mem & ~other;
}

/* The parameters (bit n-1 for \n) the macro body in TEXT (up to its endm
 * line) uses only as a branch target (the last operand of a b<cc>, also
 * one the body makes: b\2 \3) or a displacement on pc (\1(pc)),
 * nowhere else: an argument naming a global label goes as its twin. */
static unsigned twin_params(const char *text)
{
	unsigned twin = 0, other = 0;
	if (text == NULL)
		return 0;
	for (const char *p = text; *p;) {
		const char *e = p + strcspn(p, "\r\n");
		const char *w = p;
		if (*w != '*' && *w != ';') {
			/* in the label (\1:): the macro makes the name */
			if (!isspace((unsigned char)*w))
				for (; w < e && !isspace((unsigned char)*w); w++)
					if (w[0] == '\\' && w + 1 < e && w[1] >= '1' && w[1] <= '9')
						other |= 1u << (w[1] - '1');
			while (w < e && isspace((unsigned char)*w))
				w++;
			const char *op = w;
			while (w < e && !isspace((unsigned char)*w))
				w++;
			size_t on = (size_t)(w - op);
			if (on == 4 && strncasecmp(op, "endm", 4) == 0)
				break;
			while (w < e && isspace((unsigned char)*w))
				w++;
			const char *a = w;
			while (w < e && !isspace((unsigned char)*w))
				w++;
			char base[32];
			size_t bn = strcspn(op, ". \t\r\n");
			if (bn >= sizeof base)
				bn = sizeof base - 1;
			for (size_t i = 0; i < bn; i++)
				base[i] = (char)tolower((unsigned char)op[i]);
			base[bn] = '\0';
			int branch = is_branch(base) || (base[0] == 'b' && base[1] == '\\') ||
				     (base[0] == 'd' && base[1] == 'b');
			const char *pieces[16];
			size_t lens[16];
			int k = split_commas(a, (size_t)(w - a), pieces, lens, 16);
			for (int i = 0; i < k; i++) {
				for (size_t c = 0; c + 1 < lens[i]; c++) {
					if (pieces[i][c] != '\\' || pieces[i][c + 1] < '1' || pieces[i][c + 1] > '9')
						continue;
					unsigned bit = 1u << (pieces[i][c + 1] - '1');
					if (c == 0 && ((branch && i == k - 1 && lens[i] == 2) ||
						       (lens[i] >= 6 && strncasecmp(pieces[i] + 2, "(pc", 3) == 0 &&
							(pieces[i][5] == ')' || pieces[i][5] == ','))))
						twin |= bit;
					else
						other |= bit;
				}
			}
			/* in the operation (b\2): not an argument for a twin */
			for (const char *c = op; c + 1 < op + on; c++)
				if (c[0] == '\\' && c[1] >= '1' && c[1] <= '9')
					other |= 1u << (c[1] - '1');
		}
		p = e;
		while (*p == '\r' || *p == '\n')
			p++;
	}
	return twin & ~other;
}

/* TEXT (N) with its "..." strings as MRI's '...' (a ' in one doubled). */
static void mri_strings(struct buf *b, const char *text, size_t n)
{
	int in = 0;
	for (size_t c = 0; c < n; c++) {
		if (text[c] == '"') {
			bputc(b, '\'');
			in = !in;
		} else if (in && text[c] == '\'') {
			bputs(b, "''");
		} else {
			bputc(b, text[c]);
		}
	}
}

/* Whether the macro body in TEXT (up to its endm line) uses \#. */
static int uses_narg(const char *text)
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
		if (*p != '*')
			for (const char *c = p; c + 1 < e; c++)
				if (c[0] == '\\' && c[1] == '#')
					return 1;
		p = e;
		while (*p == '\r' || *p == '\n')
			p++;
	}
	return 0;
}

/* A call of macro NAME with ARGS whose body uses \#: where an argument is
 * empty, which r68's \# counts and gas's NARG doesn't (MAC
 * X,Y,,,,,,, has 9), r68__narg set to r68's count and r68__nset
 * to 1, on lines before (line_before), which the expansion's first lines
 * take instead of NARG (r68__n\@, so that a call in the body doesn't
 * change it). Not in a macro body, where an argument \n may be empty when
 * the body expands (Flag \9 in a body: none, as NARG says). */
static void narg_set(const char *name, const char *args)
{
	int m = -1;
	for (int i = 0; i < st.nmacros; i++)
		if (ci_eq(st.macros[i], name) || ci_eq(st.macros[i], macro_orig(name)))
			m = i;
	if (m < 0 || !st.macro_narg[m])
		return;
	const char *pieces[64];
	size_t lens[64];
	int k = *args ? split_commas(args, strlen(args), pieces, lens, 64) : 0;
	int empty = 0;
	for (int i = 0; i < k; i++)
		empty |= lens[i] == 0;
	if (!empty || st.in_macro)
		return;
	bprintf(&line_before, "r68__narg set %d\nr68__nset set 1\n", k);
	put_marker(&line_before);
}

/* A call of macro NAME with ARGS whose body uses '\N' (char_param): for
 * each such N, a line before (line_before) setting .Lr68c<macro>_<N> to the
 * value of argument N's characters as r68 takes them (its quotes stripped,
 * the last four characters: 'ecode' is 'code'); an argument \K in a macro
 * body is that macro's alias for K in turn. */
static int constant_value(const char *s, long *v);

/* Before a call of macro M: .Lr68k<m>_<n> for each parameter its body takes
 * as a branch target (branch_const), 1 where the argument is a constant
 * (the branch is written with it as the displacement), else 0. */
static void branch_const_flags(int m, const char *args)
{
	if (st.macro_bconst[m] == 0)
		return;
	const char *pieces[64];
	size_t lens[64];
	int k = *args ? split_commas(args, strlen(args), pieces, lens, 64) : 0;
	for (int n = 1; n <= 9; n++) {
		if (!(st.macro_bconst[m] & (1u << (n - 1))))
			continue;
		long v;
		int c = 0;
		if (n <= k && lens[n - 1] > 0 && !(lens[n - 1] >= 2 && pieces[n - 1][0] == '\\')) {
			char *a = xstrndup(pieces[n - 1], lens[n - 1]);
			c = source_value(a, &v);
			free(a);
		}
		bprintf(&line_before, ".Lr68k%d_%d set %d\n", m, n, c);
	}
}

static void char_aliases(const char *name, const char *args)
{
	int m = -1;
	for (int i = 0; i < st.nmacros; i++)
		if (ci_eq(st.macros[i], name) || ci_eq(st.macros[i], macro_orig(name)))
			m = i;
	if (m >= 0)
		branch_const_flags(m, args);
	if (m < 0 || st.macro_chars[m] == 0)
		return;
	const char *pieces[64];
	size_t lens[64];
	int k = *args ? split_commas(args, strlen(args), pieces, lens, 64) : 0;
	struct buf pre = { 0 };
	for (int n = 1; n <= 9; n++) {
		if (!(st.macro_chars[m] & (1u << (n - 1))))
			continue;
		bprintf(&pre, ".Lr68c%d_%d set ", m, n);
		const char *a = n <= k ? pieces[n - 1] : "";
		size_t an = n <= k ? lens[n - 1] : 0;
		if (st.in_macro && an == 2 && a[0] == '\\' && a[1] >= '1' && a[1] <= '9') {
			char_param(&pre, a[1] - '0');
		} else {
			/* r68's argument text: quotes stripped, \' a quote */
			unsigned long v = 0;
			int whole = an >= 2 && (a[0] == '"' || a[0] == '\'') && a[an - 1] == a[0];
			for (size_t c = whole; c < an - whole; c++) {
				if (a[c] == '"' || (a[c] == '\\' && c + 1 < an && a[c + 1] == '\''))
					continue;
				v = (v << 8 | (unsigned char)a[c]) & 0xffffffffUL;
			}
			bprintf(&pre, "$%lx", v);
		}
		bputc(&pre, '\n');
	}
	if (pre.n > 0 && !st.in_macro)
		put_marker(&pre);
	bput(&line_before, pre.s, pre.n);
	free(pre.s);
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
		 * of its parameters in turn) */
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
		put_marker(&pre);
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
			/* The mask's bit order reversed, by gas itself: a symbol for
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
 * $00000002), gas and Ultra C's r68 the immediate instructions (ANDI:
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
	 * gas and Ultra C's r68 make ADDQ/SUBQ */
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
	int reg = reg_number(pieces[1], lens[1]);
	if (reg < 0 || (reg < 8 && addr) || (reg >= 8 && op != 0xD000 && op != 0x9000))
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

static int names_external(const char *s, size_t n);

/* Whether operand S (N chars) with macro parameters is an expression (an
 * operator outside quotes: $E7E+\1, $E7E+'\1'), so an address where it
 * isn't in parentheses, not a register as a parameter alone may be. */
static int param_address(const char *s, size_t n)
{
	char q = 0;
	if (memchr(s, '(', n) != NULL || memchr(s, '#', n) != NULL)
		return 0;
	for (size_t i = 0; i < n; i++) {
		if (q) {
			if (s[i] == q)
				q = 0;
		} else if (s[i] == '\'' || s[i] == '"') {
			q = s[i];
		} else if (i > 0 && strchr("+-*/&!<>", s[i]) != NULL) {
			return 1;
		}
	}
	return 0;
}

/* Operand E (EN chars) as an effective address of the simple kinds: a data or
 * address register, (An), (An)+, -(An), d(An); its mode and register, and for
 * d(An) the displacement (*DISP, *DN chars, else null). 1 if it is one. */
static int simple_ea(const char *e, size_t en, int *mode, int *reg, const char **disp, size_t *dn)
{
	int r;
	*disp = NULL;
	*dn = 0;
	if ((r = reg_number(e, en)) >= 0) {
		*mode = r >= 8 ? 1 : 0;
	} else if (en >= 4 && e[0] == '(' && e[en - 1] == ')' && (r = reg_number(e + 1, en - 2)) >= 8) {
		*mode = 2;
	} else if (en >= 5 && e[0] == '(' && e[en - 1] == '+' && e[en - 2] == ')' &&
		   (r = reg_number(e + 1, en - 3)) >= 8) {
		*mode = 3;
	} else if (en >= 5 && e[0] == '-' && e[1] == '(' && e[en - 1] == ')' && (r = reg_number(e + 2, en - 3)) >= 8) {
		*mode = 4;
	} else {
		const char *lp = memchr(e, '(', en);
		if (lp == NULL || lp == e || e[en - 1] != ')' || (r = reg_number(lp + 1, (size_t)(e + en - 1 - lp - 1))) < 8)
			return 0;
		*mode = 5;
		*disp = e;
		*dn = (size_t)(lp - e);
	}
	*reg = r & 7;
	return 1;
}

/* Whether immediate V (VN chars) is a constant of the source that isn't known
 * here (a difference of labels, an equate further on): neither known yet,
 * nor naming an external, a macro parameter or the location. */
static int later_value(const char *v, size_t vn)
{
	return !known_value(v, vn) && !names_external(v, vn) && memchr(v, '\\', vn) == NULL &&
		!has_location(v, vn);
}

/* addq/subq #VALUE,<ea> where VALUE isn't known yet but is a constant of the
 * source (a difference of two vsect labels, #OffP-OnP): gas wants the 3-bit
 * field's value at once, r68 takes it; written as words, the value in the
 * field by an expression gas resolves at the end ((v&7)<<9: 8 as 0; no
 * check of 1..8). Data and address registers, (An), (An)+, -(An), d(An).
 * 1 if done. */
static int quick_words(struct buf *out, const char *labs, const char *base, char size, const char *args,
		       const char *rest)
{
	int sub = strcmp(base, "subq") == 0;
	if (!sub && strcmp(base, "addq") != 0)
		return 0;
	const char *pieces[4];
	size_t lens[4];
	if (split_commas(args, strlen(args), pieces, lens, 4) != 2 || pieces[0][0] != '#')
		return 0;
	const char *v = pieces[0] + 1;
	size_t vn = lens[0] - 1;
	int mode, reg;
	const char *disp;
	size_t dn;
	if (!later_value(v, vn) || !simple_ea(pieces[1], lens[1], &mode, &reg, &disp, &dn))
		return 0;
	if (mode == 1 && size == 'b')
		return 0;
	int s = size == 'b' ? 0 : size == 'l' ? 2 : 1;
	unsigned word = 0x5000u | (sub ? 0x100u : 0) | (unsigned)s << 6 | (unsigned)mode << 3 | (unsigned)reg;
	struct buf x = { 0 };
	expression(&x, v, vn);
	bprintf(out, "%s dc.w $%04X!((((%s)&7))<<9)", labs, word, x.s);
	free(x.s);
	if (disp != NULL) {
		bputc(out, ',');
		expression(out, disp, dn);
	}
	bputs(out, rest);
	annotate = 1;
	return 1;
}

/* A byte immediate known only later (andi.b #L1-L2,d0, ori.b #Fw,(a0) with Fw
 * an equate further on) in ori, andi, subi, addi, eori, cmpi .b, to ccr, and
 * or, and, sub, add, eor, cmp .b (which are those; not C 3.2's <ea>,Dn form,
 * c32_immediate): both Microware assemblers write the whole value as the
 * extension word ($FFFE for -2; checked 2026-10-15), gas only its low byte
 * where it learns the value at the end ($00FE). Written as words, the value
 * as a word by an expression gas resolves at the end: the opcode, the value,
 * the displacement of d(An). Data registers, (An), (An)+, -(An), d(An), ccr.
 * 1 if done. */
static int byte_imm_words(struct buf *out, const char *labs, const char *base, char size, const char *args,
			  const char *rest)
{
	static const struct { const char *i, *alu; unsigned op; } ops[] = {
		{ "ori", "or", 0x0000 }, { "andi", "and", 0x0200 }, { "subi", "sub", 0x0400 },
		{ "addi", "add", 0x0600 }, { "eori", "eor", 0x0A00 }, { "cmpi", "cmp", 0x0C00 },
	};
	int k = -1;
	for (int i = 0; i < (int)(sizeof ops / sizeof *ops); i++)
		if (strcmp(base, ops[i].i) == 0 || strcmp(base, ops[i].alu) == 0)
			k = i;
	if (k < 0)
		return 0;
	const char *pieces[4];
	size_t lens[4];
	if (split_commas(args, strlen(args), pieces, lens, 4) != 2 || pieces[0][0] != '#')
		return 0;
	const char *v = pieces[0] + 1;
	size_t vn = lens[0] - 1;
	if (!later_value(v, vn))
		return 0;
	int ccr = (lens[1] == 3 && strncasecmp(pieces[1], "ccr", 3) == 0) ||
		(lens[1] == 2 && strncasecmp(pieces[1], "cc", 2) == 0);
	int mode, reg;
	const char *disp = NULL;
	size_t dn = 0;
	unsigned word;
	if (ccr) {
		/* ori, andi, eori to ccr; the others have no such form */
		if (size == 'l' || (ops[k].op != 0x0000 && ops[k].op != 0x0200 && ops[k].op != 0x0A00))
			return 0;
		word = ops[k].op | 0x3C;
	} else {
		if (size != 'b' || !simple_ea(pieces[1], lens[1], &mode, &reg, &disp, &dn) || mode == 1)
			return 0;
		word = ops[k].op | (unsigned)mode << 3 | (unsigned)reg;
	}
	struct buf x = { 0 };
	expression(&x, v, vn);
	bprintf(out, "%s dc.w $%04X,(%s)", labs, word, x.s);
	free(x.s);
	if (disp != NULL) {
		bputc(out, ',');
		expression(out, disp, dn);
	}
	bputs(out, rest);
	annotate = 1;
	return 1;
}

/* Whether expression S (N chars) names an external (external_name), also
 * through an equate of one (RdyBit equ LibBit, LibBit a library's), and no
 * macro parameter. */
static int names_external_depth(const char *s, size_t n, int depth);

static int names_external(const char *s, size_t n)
{
	return names_external_depth(s, n, 0);
}

/* Expression S (N chars) with each equate that names an external replaced
 * by its value in parentheses, recursively (RdyBit, RdyBit equ LibBit: (LibBit)). */
static char *through_equates(const char *s, size_t n, int depth)
{
	struct buf b = { 0 };
	for (size_t i = 0; i < n;) {
		if (s[i] == '\'') {
			size_t j = i + 1;
			while (j < n && s[j] != '\'')
				j++;
			j = j < n ? j + 1 : n;
			bprintf(&b, "%.*s", (int)(j - i), s + i);
			i = j;
		} else if (s[i] == '$' || s[i] == '%' || isdigit((unsigned char)s[i])) {
			size_t j = i + 1;
			while (j < n && isalnum((unsigned char)s[j]))
				j++;
			bprintf(&b, "%.*s", (int)(j - i), s + i);
			i = j;
		} else if (symstart((unsigned char)s[i])) {
			size_t j = i;
			while (j < n && symchar((unsigned char)s[j]))
				j++;
			const char *eq = depth < 8 && !external_name(s + i, j - i) ? equate_text(s + i, j - i) : NULL;
			if (eq != NULL && names_external(eq, strlen(eq))) {
				char *t = through_equates(eq, strlen(eq), depth + 1);
				bprintf(&b, "(%s)", t);
				free(t);
			} else {
				bprintf(&b, "%.*s", (int)(j - i), s + i);
			}
			i = j;
		} else {
			bputc(&b, s[i++]);
		}
	}
	if (b.s == NULL)
		bputs(&b, "");
	return b.s;
}

static int names_external_depth(const char *s, size_t n, int depth)
{
	int ext = 0;
	for (size_t i = 0; i < n;) {
		if (s[i] == '\'') {
			for (i++; i < n && s[i] != '\''; i++)
				;
			i++;
		} else if (s[i] == '\\') {
			return 0;
		} else if (s[i] == '$' || s[i] == '%' || isdigit((unsigned char)s[i])) {
			for (i++; i < n && isalnum((unsigned char)s[i]); i++)
				;
		} else if (symstart((unsigned char)s[i])) {
			size_t j = i;
			while (j < n && symchar((unsigned char)s[j]))
				j++;
			const char *eq = depth < 8 && !external_name(s + i, j - i) ? equate_text(s + i, j - i) : NULL;
			ext |= external_name(s + i, j - i) || (eq != NULL && names_external_depth(eq, strlen(eq), depth + 1));
			i = j;
		} else {
			i++;
		}
	}
	return ext;
}

/* Whether S (N chars) subtracts a code label of the source (Ext-Label). */
static int less_code_label(const char *s, size_t n)
{
	char prev = 0;
	for (size_t i = 0; i < n;) {
		if (s[i] == '\'') {
			for (i++; i < n && s[i] != '\''; i++)
				;
			i++;
			prev = 'x';
		} else if (s[i] == '$' || s[i] == '%' || isdigit((unsigned char)s[i])) {
			for (i++; i < n && isalnum((unsigned char)s[i]); i++)
				;
			prev = 'x';
		} else if (symstart((unsigned char)s[i])) {
			size_t j = i;
			while (j < n && symchar((unsigned char)s[j]))
				j++;
			if (prev == '-' && label_section(s + i, j - i) == 't')
				return 1;
			i = j;
			prev = 'x';
		} else {
			if (s[i] != ' ' && s[i] != '\t')
				prev = s[i];
			i++;
		}
	}
	return 0;
}

/* The elements of dc.w/dc.l ARGS (BYTES each) that are an external less
 * their own place or a code label (dc.w Ext-*, dc.w Ext-Label): each
 * noted in .r68pcm, a long relocated to the element, after the line
 * (.Lr68pcm set there: gas keeps each use of a set symbol at its value
 * then, so a rept or a macro may repeat it, where a label would be
 * defined again; no macro of the prelude, whose use would count in the
 * source's \@ numbers; MRI's section, as .pushsection needs .mri 0, and
 * gas, which prepares its input ahead in the mode then, takes lines
 * after a .mri 1 at a buffer's end in the wrong one), for elf2rof to
 * write as both Microware
 * assemblers do: the external and a negated reference to the psect's code,
 * where gas makes a PC-relative relocation like an instruction's. */
static void pcm_marks(const char *args, int bytes)
{
	const char *pieces[64];
	size_t lens[64];
	int n = split_commas(args, strlen(args), pieces, lens, 64);
	int any = 0;
	for (int k = 0; k < n; k++) {
		if (lens[k] == 0 || memchr(pieces[k], '\'', lens[k]) != NULL || memchr(pieces[k], '"', lens[k]) != NULL)
			return;	/* a string: the elements' sizes aren't known */
		any |= (has_location(pieces[k], lens[k]) || less_code_label(pieces[k], lens[k])) && names_external(pieces[k], lens[k]);
	}
	if (!any)
		return;
	bputs(&line_after, ".Lr68pcm set *\n section .r68pcm\n");
	for (int k = 0; k < n; k++)
		if ((has_location(pieces[k], lens[k]) || less_code_label(pieces[k], lens[k])) && names_external(pieces[k], lens[k]))
			bprintf(&line_after, " dc.l .Lr68pcm-%d\n", (n - k) * bytes);
	bputs(&line_after, " section .text\n");
	st.used_pcm = 1;
	annotate = 1;
}

/* A record of .r68ord, written at the end (where every label is known; each
 * only if its label is assembled): KIND, then where it is (label POS) and
 * the debug line's start (.Lr68ds<DBG>, 0 for none). elf2rof orders the
 * ROF's references by them as both Microware assemblers do (measured
 * 2026-10-17): the external ones in the order they were made, the local
 * ones those made in the psect (its code, debug lines there) then those
 * made in vsects (their data, debug lines there), each list from the last
 * made. Kinds: 1 a debug line in the code (POS there), 2 one in a vsect
 * (POS in its initialized data), 3 a vsect's start (POS in the code), 4 a
 * vsect's end (POS in its initialized data, where it has some). */
static void order_record(int kind, const char *pos, int dbg)
{
	bprintf(&st.ord, " ifdef %s\n dc.w %d\n dc.l %s,", pos, kind, pos);
	if (dbg)
		bprintf(&st.ord, ".Lr68ds%d\n", dbg);
	else
		bputs(&st.ord, "0\n");
	bputs(&st.ord, " endc\n");
}

/* debug.b/.w/.l ARGS (C 3.2's c68 -g writes them; both Microware assemblers
 * take them): the operands into the ROF's debug area, which follows the
 * remote data, its size in the header, its words and longs at even offsets
 * but its end not padded (measured 2026-10-17); * is the location where the
 * line is. Written into section
 * .r68dbg, which elf2rof takes for the debug area: strings with ascii,
 * other bytes with fill (in MRI mode, gas aligns the next statement after
 * an odd dc.b, a section switch too), words and longs with dc. In a vsect
 * the line is in its initialized data (switched to, as a dc there would
 * be). Each line is noted in .r68ord (order_record), but in a macro body or
 * a rept, where a label would be defined twice. */
static void debug_line(struct buf *out, const char *labs, const char *args, char size, const char *rest)
{
	int labels = !st.in_macro && st.reps == 0;
	int n = ++st.ndebug;
	char loc[32];
	st.used_debug = 1;
	if (*labs)
		bprintf(out, "%s\n", labs);
	if (st.in_vsect) {
		const char *sect = data_section(1);
		if (!same_section(sect))
			bprintf(out, " r68sect %s\n", sect);
		st.section = sect;
		st.vs_init = 1;
	}
	if (labels) {
		/* labels alone, which MRI mode doesn't align at */
		snprintf(loc, sizeof loc, ".Lr68dp%d", n);
		bprintf(out, "%s\n section .r68dbg\n.Lr68ds%d\n", loc, n);
		order_record(st.in_vsect ? 2 : 1, loc, n);
	} else {
		snprintf(loc, sizeof loc, ".Lr68dpos");
		bprintf(out, "%s set *\n section .r68dbg\n", loc);
	}
	int max = 1;
	for (const char *c = args; *c; c++)
		max += *c == ',';
	const char **pieces = xmalloc(max * sizeof *pieces);
	size_t *lens = xmalloc(max * sizeof *lens);
	int k = split_commas(args, strlen(args), pieces, lens, max);
	/* words and longs at an even offset in the area, after a zero byte
	 * where it's odd (as both assemblers write it; its end isn't padded);
	 * by its start's label, .r68dbg's alignment staying 1 (for the link's
	 * .dbg, whose psects' parts the link would align too) */
	if (size != 'b')
		bputs(out, " fill (*-.Lr68dbg0)&1,1,0\n");
	for (int i = 0; i < k; i++) {
		if (size == 'b' && lens[i] > 0 && (pieces[i][0] == '"' || pieces[i][0] == '\'') &&
		    whole_quoted(pieces[i], lens[i])) {
			/* a string: its characters as string_operand reads them, the
			 * ones gas's scrubber might take otherwise in octal */
			char q = pieces[i][0];
			struct buf s = { 0 };
			for (size_t c = 1; c < lens[i]; c++) {
				unsigned char ch = (unsigned char)pieces[i][c];
				if (ch == (unsigned char)q) {
					if (c + 1 < lens[i] && pieces[i][c + 1] == q)
						c++;
					else
						break;
				} else if (ch == '\\' && q == '"' && c + 1 < lens[i]) {
					ch = (unsigned char)pieces[i][++c];
				}
				if (isalnum(ch) || (ch != 0 && strchr(" :/_-.,=()[]+*", ch)))
					bputc(&s, (char)ch);
				else
					bprintf(&s, "\\%03o", ch);
			}
			if (s.n > 0)
				bprintf(out, " ascii \"%s\"\n", s.s);
			free(s.s);
			continue;
		}
		char *at = location_as(pieces[i], lens[i], "r68__dloc");
		struct buf e = { 0 };
		expr_chars = size != 'b';
		expression(&e, at ? at : pieces[i], at ? strlen(at) : lens[i]);
		expr_chars = 0;
		free(at);
		/* the location's placeholder as the line's label */
		bputs(out, size == 'b' ? " fill 1,1," : size == 'w' ? " dc.w " : " dc.l ");
		for (const char *p = e.s ? e.s : "0"; *p;) {
			if (strncmp(p, "r68__dloc", 9) == 0) {
				bputs(out, loc);
				p += 9;
			} else {
				bputc(out, *p++);
			}
		}
		bputc(out, '\n');
		free(e.s);
	}
	free(pieces);
	free(lens);
	bprintf(out, " section %s%s", st.in_vsect ? data_section(1) : ".text", rest);
	annotate = 1;
}

/* Whether instruction BASE (SIZE) with operands ARGS has a byte immediate of
 * an external that the Microware assemblers relocate as the whole extension
 * word (see word_imm): ori, andi, subi, addi, eori, cmpi .b or to ccr; or,
 * and, sub, add, eor, cmp .b with an immediate (which are those; not C 3.2's
 * <ea>,Dn form, c32_immediate); btst, bchg, bclr, bset #n. */
static int word_byte_imm(const char *base, char size, const char *args)
{
	static const char *const imm[] = { "ori", "andi", "subi", "addi", "eori", "cmpi", NULL };
	static const char *const alu[] = { "or", "and", "sub", "add", "eor", "cmp", NULL };
	static const char *const bit[] = { "btst", "bchg", "bclr", "bset", NULL };
	if (args[0] != '#')
		return 0;
	const char *pieces[4];
	size_t lens[4];
	if (split_commas(args, strlen(args), pieces, lens, 4) != 2)
		return 0;
	int ccr = (lens[1] == 3 && strncasecmp(pieces[1], "ccr", 3) == 0) ||
		(lens[1] == 2 && strncasecmp(pieces[1], "cc", 2) == 0);
	int ok = 0;
	for (int i = 0; imm[i] && !ok; i++)
		ok = strcmp(base, imm[i]) == 0 && (size == 'b' || (size != 'l' && ccr));
	for (int i = 0; alu[i] && !ok; i++)
		ok = strcmp(base, alu[i]) == 0 && (size == 'b' || (size != 'l' && ccr));
	for (int i = 0; bit[i] && !ok; i++)
		ok = strcmp(base, bit[i]) == 0;
	return ok && names_external(pieces[0] + 1, lens[0] - 1);
}

/* Translates one statement line into OUT (one line, no newline). */
/* A call of a macro whose body leaves a conditional open (macro_open):
 * r68 assembles the rest of the source in it, which is left out where
 * the body ended in a part left out. r68__mo cleared before the call,
 * set by the body where it ends assembled; ifne r68__mo after it, open
 * to the end of the source (or the macro body the call is in). */
static void open_call(const char *name)
{
	for (int i = st.nmacros; i-- > 0;)
		if (ci_eq(st.macros[i], name) || ci_eq(st.macros[i], macro_orig(name))) {
			if (!st.macro_open[i] && !gm_open(macro_orig(name), 0))
				return;
			bputs(&line_before, "r68__mo set 0\n");
			bputs(&line_after, " ifne r68__mo\n");
			if (st.in_macro)
				st.macro_ifs++;
			else
				st.cond_depth++;
			return;
		}
}

static int dc_split;	/* translating the pieces of a dc line split for "\N" */

/* Whether a dc list ARGS has an element that is a parameter alone in
 * quotes ("\2", '\2'). */
static int param_string_element(const char *args)
{
	const char *pieces[64];
	size_t lens[64];
	int k = split_commas(args, strlen(args), pieces, lens, 64);
	for (int i = 0; i < k; i++)
		if (lens[i] == 4 && (pieces[i][0] == '"' || pieces[i][0] == '\'') && pieces[i][3] == pieces[i][0] &&
		    pieces[i][1] == '\\' && isdigit((unsigned char)pieces[i][2]))
			return 1;
	return 0;
}

/* Whether the source's next statement after this line (st.rest_of_file),
 * past blank and comment lines, has the label NAME (an equ or set of it
 * only as *). */
static int next_is_label(const char *name)
{
	size_t n = strlen(name);
	if (n == 0 || !symstart((unsigned char)name[0]))
		return 0;
	for (size_t i = 0; i < n; i++)
		if (!symchar((unsigned char)name[i]))
			return 0;
	const char *p = st.rest_of_file;
	for (;;) {
		while (*p == '\r' || *p == '\n')
			p++;
		if (*p == '\0')
			return 0;
		const char *e = p;
		while (*e && *e != '\r' && *e != '\n')
			e++;
		const char *t = p;
		while (t < e && (*t == ' ' || *t == '\t'))
			t++;
		if (t == e || *p == '*' || *p == ';' || (t > p && (*t == '*' || *t == ';'))) {
			p = e;
			continue;
		}
		if (t > p || (size_t)(e - p) < n || strncmp(p, name, n) != 0)
			return 0;
		const char *a = p + n;
		if (a < e && *a == ':')
			a++;
		if (a < e && *a != ' ' && *a != '\t')
			return 0;
		while (a < e && (*a == ' ' || *a == '\t'))
			a++;
		if (!((e - a >= 3 && (strncasecmp(a, "equ", 3) == 0 || strncasecmp(a, "set", 3) == 0)) &&
		      (e - a == 3 || a[3] == ' ' || a[3] == '\t')))
			return 1;
		/* NAME equ *: the next location too */
		a += 3;
		while (a < e && (*a == ' ' || *a == '\t'))
			a++;
		return a < e && *a == '*' && (a + 1 == e || a[1] == ' ' || a[1] == '\t');
	}
}

/* An immediate PC-relative to an external, the instruction's first
 * operand (movea.l #Ext-*-(End-Here+2),a2: a linker symbol's distance
 * from the instruction), in instruction OP with operands ARGS, labels LABS
 * and comment REST, size SIZE (l or w): both r68s write a PC-relative
 * reference (R_68K_PC32, PC16) on the immediate; gas can't take an
 * external less *. Written into OUT as the instruction with 0 there and
 * the relocation by .reloc, the addend (2, the field's offset from *, and
 * the rest of the expression) an equate, which may name later labels.
 * Not after byte data with labels (gas would align between the labels and
 * the instruction's own); 0 if ARGS isn't of that form. */
static int pc_immediate(const char *op, const char *args, const char *labs, const char *rest, char size,
			struct buf *out)
{
	const char *pieces[8];
	size_t lens[8];
	int k = split_commas(args, strlen(args), pieces, lens, 8);
	if (k < 2 || lens[0] < 4 || pieces[0][0] != '#' || !symstart((unsigned char)pieces[0][1]))
		return 0;
	const char *s = pieces[0] + 1, *e = pieces[0] + lens[0];
	const char *q = s;
	while (q < e && symchar((unsigned char)*q))
		q++;
	if (e - q < 2 || q[0] != '-' || q[1] != '*' || (q + 2 < e && q[2] != '+' && q[2] != '-'))
		return 0;
	char *name = xstrndup(s, (size_t)(q - s));
	int ext = short_external(name);
	if (!ext || (*labs && st.maybe_odd)) {
		free(name);
		return 0;
	}
	static int npi;
	npi++;
	if (*labs)
		bprintf(out, "%s\n", labs);
	bprintf(out, ".Lr68pi%d %s #0+r68__0", npi, op);
	for (int i = 1; i < k; i++) {
		bputc(out, ',');
		operand(out, pieces[i], lens[i], 0);
	}
	bprintf(out, "%s\n.Lr68pa%d equ 2+(", rest, npi);
	struct buf r = { 0 };
	bprintf(&r, "0%.*s", (int)(e - q - 2), q + 2);
	expression(out, r.s, r.n);
	free(r.s);
	bputc(out, ')');
	struct buf t = { 0 };
	put_symbol(&t, name, strlen(name));
	bprintf(out, "\n reloc .Lr68pi%d+2,R_68K_PC%d,%s+.Lr68pa%d", npi, size == 'l' ? 32 : 16,
		t.s, npi);
	free(t.s);
	free(name);
	return 1;
}

static void translate_line(const char *text, struct buf *out, const char *path, int lineno, FILE *fp, int depth)
{
	struct line l;
	log_path = path;
	log_line = lineno;
	string_params = st.in_macro;
	st.aligning = 0;
	/* \' in a macro body is a quote when the body expands
	 * (MAC.w #\'@7\'), which gas would read as an escaped quote and run
	 * the string past endm: a quote here; \\' too (MAC.l #\\'????\\',
	 * whose escaped backslash came out as \' as well) */
	char *unesc = NULL;
	if (st.in_macro && strstr(text, "\\'")) {
		unesc = xstrdup(text);
		char *w = unesc;
		for (const char *r = text; *r; r++) {
			if (r[0] == '\\' && r[1] == '\\' && r[2] == '\'')
				r++;
			else if (!(r[0] == '\\' && r[1] == '\''))
				*w++ = *r;
		}
		*w = '\0';
		text = unesc;
	}
	if (st.narg_pending && st.in_macro) {
		/* the first line of a body that uses \#: the call's count */
		bputs(&line_before, "r68__n\\@ set NARG\n ifne r68__nset\nr68__n\\@ set r68__narg\nr68__nset set 0\n endc\n");
		st.narg_pending = 0;
	}
	if (!split_line(text, &l)) {
		bputs(out, text);
		free(unesc);
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
	/* rept blocks open (no labels of debug_line and order_record there) */
	if (strcmp(base, "rept") == 0)
		st.reps++;
	else if (strcmp(base, "endr") == 0 && st.reps > 0)
		st.reps--;
	/* No link-time expression where the field must be a number (r68
	 * refuses an external there too): fields of the instruction word that
	 * aren't a byte (quick values, shift counts), sizes and counts,
	 * conditionals (see fold_equates). moveq's and trap's are bytes:
	 * relocated, as r68 does. */
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
		st.npsects++;
		free(st.psect_args);
		st.psect_args = xstrdup(l.args);
		{
			/* its name, for label_alias */
			size_t pn = strcspn(l.args, ",");
			while (pn > 0 && isspace((unsigned char)l.args[pn - 1]))
				pn--;
			free(st.cur_psect);
			st.cur_psect = xstrndup(l.args, pn);
		}
		/* which psect statement is assembled (one in ifdef
		 * X / else): r68__psn, the epilogue's header from it */
		bprintf(&line_before, "r68__psn set %d\n", st.npsects);
		{
			/* the header values as they are here, where r68 takes
			 * them (a descriptor template's Attr_Rev, set again by
			 * the source after it: r68 has the earlier value), as
			 * r68__ps<n> on lines before, which the epilogue's
			 * __os9_ symbols name */
			const char *pieces[8];
			size_t lens[8];
			int k = split_commas(l.args, strlen(l.args), pieces, lens, 8);
			for (int i = 1; i < k && i < 7; i++) {
				/* equ of the expression in parentheses: gas keeps it
				 * as an expression, to be resolved at the end, of the
				 * set symbols as they are here (a bare name set to
				 * something not yet known would be its value now, 0) */
				char *t = xstrndup(pieces[i], lens[i]);
				st.ps_captured[i] = (char)expr_complete(t);
				free(t);
				if (!st.ps_captured[i])
					continue;
				bprintf(&line_before, "r68__ps%d set ", i);
				expression(&line_before, pieces[i], lens[i]);
				bputc(&line_before, '\n');
			}
			st.ps_all = xrealloc(st.ps_all, st.npsects * sizeof *st.ps_all);
			st.ps_all_captured = xrealloc(st.ps_all_captured, st.npsects * sizeof *st.ps_all_captured);
			st.ps_all[st.npsects - 1] = xstrdup(l.args);
			memcpy(st.ps_all_captured[st.npsects - 1], st.ps_captured, sizeof st.ps_captured);
			if (!st.in_macro)
				put_marker(&line_before);
		}
		st.section = ".text";
		bprintf(out, " section .text%s\nr68__sid set 1", rest);
		goto done;
	}
	if (strcmp(base, "vsect") == 0) {
		st.in_vsect = 1;
		st.remote = ci_eq(l.args, "remote");
		st.used_remote |= st.remote;
		st.vs_init = 0;
		if (!st.in_macro && st.reps == 0) {
			/* where it is in the code, for the order of the references
			 * (order_record): a label alone, which MRI mode doesn't
			 * align at */
			char v[32];
			snprintf(v, sizeof v, ".Lr68vs%d", ++st.nvsects);
			bprintf(&line_before, "%s\n", v);
			order_record(3, v, 0);
		}
		/* r68__vs: where a macro's dc and ds go (see there) */
		bprintf(out, "r68__vs set %d\t;%s", st.remote ? 2 : 1, text);
		goto done;
	}
	if (strcmp(base, "ends") == 0 || strcmp(base, "endsect") == 0) {
		if (st.in_vsect) {
			if (!st.in_macro && st.reps == 0 && st.vs_init && st.nvsects > 0) {
				/* where its initialized data ends (order_record) */
				const char *sect = data_section(1);
				char v[32];
				snprintf(v, sizeof v, ".Lr68ve%d", st.nvsects);
				if (!same_section(sect))
					bprintf(&line_before, " r68sect %s\n", sect);
				bprintf(&line_before, "%s\n", v);
				st.section = sect;
				order_record(4, v, 0);
			}
			st.in_vsect = 0;
			st.remote = 0;
			bputs(&line_before, "r68__vs set 0\n");
			if (!st.in_macro)
				put_marker(&line_before);
			if (same_section(".text"))
				bprintf(out, "*%s", text);
			else
				bprintf(out, " r68sect .text%s", rest);
			st.section = ".text";
		} else {
			st.in_psect = 0;
			/* a label alone, which gas's MRI mode doesn't align at
			 * (a set would be): whether this ends is assembled, for
			 * the padding (r68__ended); in a macro body once, for
			 * the first expansion that gets there */
			++st.nends;
			if (st.in_macro)
				bprintf(&line_before, " ifndef .Lr68ends%d\n.Lr68ends%d\n endc\n", st.nends, st.nends);
			else
				bprintf(&line_before, ".Lr68ends%d\n", st.nends);
			if (st.cond_depth > 0 || st.in_macro)
				st.ends_cond = 1;
			bprintf(out, "*%s", text);
		}
		goto done;
	}
	if (strcmp(base, "debug") == 0 && (size == 'b' || size == 'w' || size == 'l')) {
		debug_line(out, labs, l.args ? l.args : "", size, rest);
		goto done;
	}
	/* spanreg: Ultra C's scratch register for r68's branch sizing. */
	if (strcmp(base, "nam") == 0 || strcmp(base, "pag") == 0 || strcmp(base, "opt") == 0 || strcmp(base, "end") == 0 ||
	    strcmp(base, "spanreg") == 0) {
		bprintf(out, "*%s", text);
		goto done;
	}
	if (strcmp(base, "ttl") == 0 || strcmp(base, "spc") == 0 || strcmp(base, "page") == 0) {
		/* after byte data a comment: gas aligns at a directive there
		 * (a ttl after a use file's Name dc.b ...,0) */
		if (st.maybe_odd)
			bprintf(out, "*%s", text);
		else
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
			int include = separate && !st.maybe_odd && !st.in_macro;
			if (include) {
				/* -s: the used file translated into an output of its
				 * own, with .mri 1 and the macros it defines moved to
				 * its start (as the inline output's to the source's),
				 * and included. Inline after byte data (gas aligns at
				 * an include there, see maybe_odd) and in a macro body
				 * (gas would include it at each expansion). */
				struct buf saved = hoisted;
				memset(&hoisted, 0, sizeof hoisted);
				FILE *t = temp_file();
				if (t == NULL)
					die("cannot make a temporary file");
				translate_file(found, t, depth + 1);
				struct buf u = { 0 };
				bputs(&u, " .mri 1\n");
				if (hoisted.n > 0)
					bput(&u, hoisted.s, hoisted.n);
				rewind(t);
				char chunk[4096];
				size_t got;
				while ((got = fread(chunk, 1, sizeof chunk, t)) > 0)
					bput(&u, chunk, got);
				fclose(t);
				free(hoisted.s);
				hoisted = saved;
				bprintf(out, " include %s", separate_output(found, u.s));
				free(u.s);
			} else {
				bprintf(out, "*%s", text);
			}
			fprintf(fp, "%s\n", out->s);
			out->n = 0;
			out->s[0] = '\0';
			if (!include)
				translate_file(found, fp, depth + 1);
			/* The next line is the including file's next; after byte
			 * data the marker is owed (gas aligns at it, see maybe_odd)
			 * and written before the next statement that aligns. */
			if (st.maybe_odd) {
				bputs(out, "*");
				st.marker_owed = 1;
			} else {
				bprintf(out, "# %d \"%s\"", lineno + 1, path);
			}
		}
		free(found);
		goto done;
	}
	if (strcmp(base, "fail") == 0) {
		/* An error with the rest of the line as its text (gas's fail
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
		 * rept and endr lines comments). In MRI mode gas aligns before
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
		note_offset(base, NULL, l.args, size);
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
		if (!st.in_macro && strcmp(base, "equ") == 0 && l.args && *l.args) {
			eq_names = xrealloc(eq_names, (neq + 1) * sizeof *eq_names);
			eq_texts = xrealloc(eq_texts, (neq + 1) * sizeof *eq_texts);
			eq_names[neq] = xstrdup(l.label);
			eq_texts[neq++] = xstrndup(l.args, expression_length(l.args, strlen(l.args)) ?
						   expression_length(l.args, strlen(l.args)) : strlen(l.args));
		}
		if (!st.in_macro && (strcmp(base, "equ") == 0 || strcmp(base, "set") == 0) && l.args) {
			int c = expr_complete(l.args);
			int had = name_in(st.complete, st.ncomplete, l.label, strlen(l.label));
			if (c && !had) {
				st.complete = xrealloc(st.complete, (st.ncomplete + 1) * sizeof *st.complete);
				st.complete[st.ncomplete++] = xstrdup(l.label);
			} else if (!c && had) {
				for (int i = 0; i < st.ncomplete; i++)
					if (strcmp(st.complete[i], l.label) == 0)
						st.complete[i][0] = '\0';
			}
		}
		/* set where it is assembled: not in a macro definition, whose
		 * lines run where it is called */
		if (st.in_macro && strcmp(base, "set") == 0 && l.args && *l.args && strchr(l.label, '\\') == NULL) {
			/* (any set symbol: one used before its first set may be
			 * found only after the macro's definition, a descriptor's
			 * template defined before its dc.b of the symbol) */
			/* in a macro body (a descriptor template's Heads set 0 for
			 * an autosize device): the first pass ran it too, where
			 * the body expands; its value as is */
			struct buf nm = { 0 };
			put_symbol(&nm, l.label, strlen(l.label));
			bprintf(&line_before, "%s__p1 set ", nm.s);
			expression(&line_before, l.args, strlen(l.args));
			bputc(&line_before, '\n');
			free(nm.s);
			if (!name_in(st.p1s, st.np1s, l.label, strlen(l.label))) {
				st.p1s = xrealloc(st.p1s, (st.np1s + 1) * sizeof *st.p1s);
				st.p1s[st.np1s++] = xstrdup(l.label);
			}
		}
		if (!st.in_macro) {
			if (strcmp(base, "set") == 0 && l.args)
				p1_set(l.label, l.args);
			st.set_done = xrealloc(st.set_done, (st.nset_done + 1) * sizeof *st.set_done);
			st.set_done[st.nset_done++] = xstrdup(l.label);
		}
	}
	if (strcmp(base, "do") == 0 || strcmp(base, "lo") == 0) {
		note_offset(base, l.label, l.args, size);
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
	if (st.in_macro && !st.macro_variant && !dc_split && strcmp(base, "dc") == 0 && size == 'b' &&
	    param_string_element(l.args)) {
		/* A string of a parameter alone ("\2"): r68 makes no byte of
		 * an empty argument, gas reads '' as a quote. The line split,
		 * each such string in ifnc '\N','' (exempt from MRI's
		 * alignment after an odd dc.b, as conditionals are). */
		const char *pieces[64];
		size_t lens[64];
		int k = split_commas(l.args, strlen(l.args), pieces, lens, 64);
		struct buf src = { 0 }, part = { 0 };
		int first = 1;
		dc_split = 1;
		for (int i = 0; i <= k; i++) {
			int ps = i < k && lens[i] == 4 && (pieces[i][0] == '"' || pieces[i][0] == '\'') &&
				 pieces[i][3] == pieces[i][0] && pieces[i][1] == '\\' && isdigit((unsigned char)pieces[i][2]);
			if ((ps || i == k) && (part.n > 0 || (first && l.label))) {
				/* the elements before, the label on the first line */
				/* a label before a "\N" first: on a dz.b 0, which
				 * goes where the dc goes (a vsect's or the code) */
				src.n = 0;
				bprintf(&src, "%s%s %s %s", first && l.label ? l.label : "", first && l.global ? ":" : "",
					part.n ? l.op : "dz.b", part.n ? part.s : "0");
				if (!first)
					bputc(out, '\n');
				translate_line(src.s, out, path, lineno, fp, depth);
				first = 0;
				part.n = 0;
			}
			if (i == k)
				break;
			if (ps) {
				/* the conditional as it is (not a line of the
				 * source: its quotes aren't character constants) */
				if (!first)
					bputc(out, '\n');
				first = 0;
				bprintf(out, " ifnc '\\%c',''\n", pieces[i][2]);
				src.n = 0;
				bprintf(&src, " %s %.*s", l.op, (int)lens[i], pieces[i]);
				translate_line(src.s, out, path, lineno, fp, depth);
				bputs(out, "\n endc");
			} else {
				if (part.n)
					bputc(&part, ',');
				bput(&part, pieces[i], lens[i]);
			}
		}
		dc_split = 0;
		bputs(out, rest);
		free(src.s);
		free(part.s);
		goto done;
	}
	if (st.in_macro && !st.macro_variant &&
	    (strcmp(base, "ds") == 0 || strcmp(base, "dc") == 0 || strcmp(base, "dz") == 0)) {
		/* In a macro body: r68 puts a dc or ds where the macro is
		 * called, in a vsect (remote or not) or in the code; the
		 * translation for each, chosen by r68__vs when the body
		 * expands. A global label (_C\1:) is exported in a vsect too. */
		struct buf v[3] = { { 0 } };
		int sv = st.in_vsect, sr = st.remote;
		st.macro_variant = 1;
		for (int k = 0; k < 3; k++) {
			st.in_vsect = k < 2;
			st.remote = k == 1;
			st.section = NULL;
			if (k < 2 && l.label && l.global) {
				struct buf nm = { 0 };
				put_symbol(&nm, l.label, strlen(l.label));
				bprintf(&v[k], " xdef %s\n", nm.s);
				free(nm.s);
			}
			translate_line(text, &v[k], path, lineno, fp, depth);
		}
		st.macro_variant = 0;
		st.in_vsect = sv;
		st.remote = sr;
		st.section = NULL;
		bprintf(out, " ifeq r68__vs-2\n%s\n else\n ifne r68__vs\n%s\n else\n%s\n endc\n endc", v[1].s ? v[1].s : "",
			v[0].s ? v[0].s : "", v[2].s ? v[2].s : "");
		for (int k = 0; k < 3; k++)
			free(v[k].s);
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
		if (strcmp(base, "ds") != 0)
			st.vs_init = 1;
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
	if (!st.in_vsect && !st.in_macro && strcmp(base, "align") == 0) {
		/* align in the code: Ultra C's r68 fills a zero byte where
		 * the location is odd (as MRI's pending alignment does at
		 * balignw), then nop, with -m2 and later trapf ($51FC), a
		 * 4-byte rest trapf.w ($51FA $0000), 6 bytes each trapf.l
		 * ($51FB and a long 0; tested 2026-10-13): balignw with nop
		 * or trapf, the same up to align 4. align (2): balign 1,
		 * which makes no frag of its own but sets off MRI's pending
		 * alignment (the zero byte) where an odd dc.b/ds.b left one:
		 * an alignment frag between a label and * would make their
		 * distance unknown to a conditional (ifgt 16-(*-lab)) */
		if (!*l.args || strcmp(l.args, "2") == 0 || strcmp(l.args, "1") == 0) {
			bprintf(out, "%s balign 1%s", labs, rest);
			annotate = 1;
			goto done;
		}
		bprintf(out, "%s balignw ", labs);
		expression(out, *l.args ? l.args : "2", strlen(*l.args ? l.args : "2"));
		bprintf(out, ",$%04X%s", st.cpu_given && st.cpu_level >= 20 ? 0x51FC : 0x4E71, rest);
		annotate = 1;
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
		if (size != 'b' && !st.in_vsect)
			pcm_marks(l.args, size == 'l' ? 4 : 2);
		goto done;
	}
	if (strcmp(base, "equ") == 0 && l.label && strcmp(l.args, "*") == 0 && !st.in_vsect) {
		/* label equ *: a plain label (gas can't relax a branch to a
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
			/* r68's macro names are case sensitive, gas's aren't: a
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
		 * board's definitions file), gas doesn't: dropped. */
		if (strcmp(base, "macro") == 0) {
			st.in_macro = 1;
			st.macro_ifs = 0;
		} else if (strcmp(base, "endm") == 0) {
			st.in_macro = 0;
			/* conditionals the body leaves open (an iflt ... else
			 * without endc): r68 keeps them open after the call,
			 * the rest of the source in them; gas refuses the
			 * macro. Closed here, r68__mo set where the body ends
			 * in an assembled part; each call followed by ifne
			 * r68__mo, open to the end of the source (macro_open). */
			if (st.macro_ifs > 0) {
				bprintf(&line_before, "r68__mo set 1\n* closing %d conditional%s the macro body leaves open\n",
					st.macro_ifs, st.macro_ifs > 1 ? "s" : "");
				if (st.nmacros > 0)
					st.macro_open[st.nmacros - 1] = 1;
			}
			for (; st.macro_ifs > 0; st.macro_ifs--)
				bputs(&line_before, " endc\n");
		} else if (!st.in_macro && is_cond(base)) {
			st.cond_depth++;
		} else if (!st.in_macro && strcmp(base, "endc") == 0) {
			/* An endc that closes nothing (also
			 * outside macros): r68 ignores it, gas doesn't: dropped. */
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
		/* Where a conditional or macro leaves the section isn't known. */
		if (strcmp(base, "equ") != 0 && strcmp(base, "set") != 0 && strcmp(base, "ds") != 0 &&
		    strcmp(base, "os9") != 0 && strcmp(base, "tcall") != 0 && strcmp(base, "align") != 0)
			st.section = NULL;
		if ((strcmp(base, "ifdef") == 0 || strcmp(base, "ifndef") == 0) && is_macro(l.args)) {
			/* ifndef Flag, a macro's name (a use file's guard): r68 knows
			 * macros there, gas only symbols: the one each macro
			 * statement sets */
			struct buf nm = { 0 };
			put_symbol(&nm, macro_orig(l.args), strlen(macro_orig(l.args)));
			bprintf(out, "%s %s r68__m_%s%s", labs, l.op, nm.s, rest);
			free(nm.s);
			annotate = 1;
			goto done;
		}
		if (strcmp(base, "ifp1") == 0) {
			/* r68's first pass only (defsfile: ifp1, use
			 * oskdefs.d, endc): its definitions hold in the
			 * second; gas has one pass */
			bprintf(out, "%s ifne 1%s", labs, rest);
			annotate_op = 1;
			goto done;
		}
		bprintf(out, "%s %s ", labs, l.op);
		if (strcmp(base, "macro") == 0 && l.label) {
			st.macros = xrealloc(st.macros, (st.nmacros + 1) * sizeof *st.macros);
			st.macro_narg = xrealloc(st.macro_narg, (st.nmacros + 1) * sizeof *st.macro_narg);
			st.macro_narg[st.nmacros] = uses_narg(st.rest_of_file);
			st.macro_ops = xrealloc(st.macro_ops, (st.nmacros + 1) * sizeof *st.macro_ops);
			st.macro_ops[st.nmacros] = op_params(st.rest_of_file);
			st.macro_viaL = xrealloc(st.macro_viaL, (st.nmacros + 1) * sizeof *st.macro_viaL);
			st.macro_viaL[st.nmacros] = (char)calls_lmacro(st.rest_of_file);
			st.macro_chars = xrealloc(st.macro_chars, (st.nmacros + 1) * sizeof *st.macro_chars);
			st.macro_chars[st.nmacros] = 0;
			st.macro_bconst = xrealloc(st.macro_bconst, (st.nmacros + 1) * sizeof *st.macro_bconst);
			st.macro_bconst[st.nmacros] = 0;
			st.macro_mem = xrealloc(st.macro_mem, (st.nmacros + 1) * sizeof *st.macro_mem);
			st.macro_mem[st.nmacros] = mem_params(st.rest_of_file);
			st.macro_twin = xrealloc(st.macro_twin, (st.nmacros + 1) * sizeof *st.macro_twin);
			st.macro_twin[st.nmacros] = twin_params(st.rest_of_file);
			st.macro_open = xrealloc(st.macro_open, (st.nmacros + 1) * sizeof *st.macro_open);
			st.macro_open[st.nmacros] = 0;
			st.narg_pending = st.macro_narg[st.nmacros];
			st.macro_shapes = xrealloc(st.macro_shapes, (st.nmacros + 1) * sizeof *st.macro_shapes);
			st.macro_shapes[st.nmacros] = (struct buf){ 0 };
			int renamed = st.nmren > 0 && strcmp(st.mren_from[st.nmren - 1], l.label) == 0 &&
				lab.s != NULL && strcmp(st.mren_to[st.nmren - 1], lab.s) == 0;
			st.macros[st.nmacros++] = xstrdup(renamed ? lab.s : l.label);
			{
				/* r68__m_NAME: for ifdef NAME (see there), set
				 * where the macro is defined */
				struct buf nm = { 0 };
				put_symbol(&nm, l.label, strlen(l.label));
				bprintf(&line_before, "r68__m_%s set 1\n", nm.s);
				free(nm.s);
				if (!st.in_macro)
					put_marker(&line_before);
			}
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
			 * gas would take for a parameter list */
			if (*l.args)
				bprintf(out, ";%s", l.args);
		} else if (strcmp(base, "ifc") == 0 || strcmp(base, "ifnc") == 0) {
			bputs(out, l.args);
		} else if ((strcmp(base, "set") == 0 || strcmp(base, "equ") == 0) && !st.in_macro &&
			   dot_less_vsect(out, l.label, l.args, alen)) {
			/* Size equ .-Label: in the label's section */
		} else if (st.c32 && (strcmp(base, "set") == 0 || strcmp(base, "equ") == 0) &&
			   (len = expression_length(l.args, alen)) > 0 && len < alen && l.args[len] != '\\') {
			/* -d c32: junk after the value, which C 3.2's R68 ignores
			 * (Ultra C's r68 refuses the line); not a macro's \@ or
			 * parameter (Len\@ set *-Beg\@) */
			value_mode = 1;
			expression(out, l.args, len);
			value_mode = 0;
			annotate = 1;
		} else {
			value_mode = strcmp(base, "equ") == 0 || strcmp(base, "set") == 0;
			expression_list(out, l.args, 0);
			value_mode = 0;
		}
		bputs(out, rest);
		goto done;
	}
	/* An instruction (or a macro call): its operands. */
	{
		/* A macro named like an instruction (cpusha, a 68040 one): the
		 * macro, as in r68; none of the instructions' cases below. */
		if (is_macro(l.op))
			base[0] = '\0';
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
		int bdb, bop = size != 'l' ? branch_code(base, &bdb) : -1;
		if (bop >= 0) {
			/* A branch to a constant (an equate, a number): both r68s
			 * take it for the displacement itself (bhi.w with $000E
			 * for an equate of 14), as a branch to an address; written
			 * as data words, without a relocation. In a macro body a
			 * parameter as the target: both forms, chosen by a flag
			 * each call sets (branch_const_flags). */
			const char *tgt = l.args, *comma = NULL;
			if (bdb) {
				comma = strchr(l.args, ',');
				tgt = comma ? comma + 1 : "";
			}
			int dreg = bdb && comma ? reg_number(l.args, (size_t)(comma - l.args)) : -1;
			if (dreg >= 8)
				dreg = -1;
			long v;
			int sh = !bdb && (size == 's' || size == 'b');
			int param = st.in_macro && tgt[0] == '\\' && tgt[1] >= '1' && tgt[1] <= '9' && tgt[2] == '\0';
			if ((!bdb || dreg >= 0) && !st.in_macro && *tgt && source_value(tgt, &v)) {
				if (sh)
					bprintf(out, "%s dc.w $%04X%s", labs, bop | ((unsigned long)v & 0xff), rest);
				else
					bprintf(out, "%s dc.w $%04X,$%04lX%s", labs, bop | (dreg > 0 ? dreg : 0),
						(unsigned long)v & 0xffff, rest);
				annotate = 1;
				goto done;
			}
			if ((!bdb || dreg >= 0) && param && st.nmacros > 0) {
				int m = st.nmacros - 1, n = tgt[1] - '0';
				st.macro_bconst[m] |= 1u << (n - 1);
				bprintf(&line_before, " ifne .Lr68k%d_%d\n", m, n);
				if (sh)
					bprintf(&line_before, "%s dc.w $%04X+((\\%d)&$FF)\n else\n", labs, bop, n);
				else
					bprintf(&line_before, "%s dc.w $%04X,\\%d\n else\n", labs, bop | (dreg > 0 ? dreg : 0), n);
				bputs(&line_after, " endc\n");
			}
		}
		if (st.c32 && !st.in_macro && is_branch(base) && (size == 's' || size == 'b') && short_external(l.args)) {
			/* -d c32: a short branch to an external (bcs.s ext): C 3.2's
			 * R68 writes an 8-bit PC-relative reference that l68
			 * resolves (Ultra C's refuses it); gas computes the byte in
			 * place and refuses. The opcode and a 0, the relocation
			 * by .reloc: gas checks even a PC-relative byte against an
			 * undefined symbol for its range, its position subtracted
			 * (a branch beyond the section's first 128 bytes refused) */
			static const char *const cc[] = { "ra", "sr", "hi", "ls", "cc", "cs", "ne", "eq", "vc", "vs", "pl",
				"mi", "ge", "lt", "gt", "le", NULL };
			const char *c = strcmp(base + 1, "hs") == 0 ? "cc" : strcmp(base + 1, "lo") == 0 ? "cs" : base + 1;
			int k = 0;
			while (cc[k] && strcmp(cc[k], c) != 0)
				k++;
			static int nsb;
			struct buf t = { 0 };
			put_symbol(&t, l.args, strlen(l.args));
			if (*labs)
				bprintf(out, "%s\n", labs);
			nsb++;
			bprintf(out, "r68__sb%d dc.b $%02X,0%s\n reloc r68__sb%d+1,R_68K_PC8,%s-1", nsb,
				0x60 + k, rest, nsb, t.s);
			free(t.s);
			annotate = 1;
			goto done;
		}
		if (st.branch_sizing && !st.in_macro && size != 'l' && is_branch(base) && next_is_label(l.args)) {
			/* -b: a branch to the next statement's label, nothing
			 * but blank and comment lines between (bra.s Done, then
			 * Done): r68 leaves it out, bsr too (Ultra C's r68,
			 * tested 2026-10-13); gas would write a word branch */
			bprintf(out, "%s%s", *labs ? labs : "*", rest);
			annotate = 1;
			goto done;
		}
		if (!st.in_macro && (size == 'l' || size == 'w') && !is_branch(base) &&
		    pc_immediate(l.op, l.args, labs, rest, size, out)) {
			annotate = 1;
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
		} else if (long_refs && is_branch(base)) {
			/* -y: every branch to a label 32-bit, an explicit .s or .w
			 * too (r68's -y; to a constant as above) */
			snprintf(opb, sizeof opb, "%s.l", base);
			op = opb;
			annotate_op = 1;
		} else if (st.branch_sizing && size != 'l' && is_branch(base) && local_label(l.args)) {
			/* r68 -b sizes every branch to a label of this source, an
			 * explicit .s or .w too; in MRI mode gas sizes only jbra,
			 * jb<cc> and jbsr. Not to an external label (gas makes an
			 * absolute jmp on the 68000), nor to a global one unless gas
			 * relaxes those (-B). */
			/* gas has no jblo, jbhs */
			snprintf(opb, sizeof opb, "j%s", strcmp(base, "blo") == 0 ? "bcs" : strcmp(base, "bhs") == 0 ? "bcc" : base);
			op = opb;
		} else if (strcmp(base, "movea") == 0 && size == 0 && !is_macro(l.op)) {
			/* movea without a size: long in both Microware
			 * assemblers (gas: word); adda, suba, cmpa and move to an
			 * address register stay word in both (tested) */
			snprintf(opb, sizeof opb, "movea.l");
			op = opb;
			annotate_op = 1;
		} else if ((strcmp(base, "exg") == 0 || strcmp(base, "swap") == 0) && size) {
			/* exg.l, swap.l: r68 takes the
			 * size, gas doesn't */
			snprintf(opb, sizeof opb, "%s", base);
			op = opb;
			annotate_op = 1;
		} else if (strcmp(base, "addi") == 0 || strcmp(base, "subi") == 0) {
			/* r68 makes addi/subi #1..8 addq/subq; gas does that for
			 * add/sub. */
			snprintf(opb, sizeof opb, "%.3s%s", base, l.op + 4);
			op = opb;
			annotate_op = 1;
		}
		if (strcmp(base, "movep") == 0 && (l.args[0] == '(' || strstr(l.args, ",(") != NULL)) {
			/* movep (a1),d0: r68 takes (An) as 0(An), gas wants the
			 * displacement */
			struct buf a = { 0 };
			for (const char *c = l.args; *c; c++) {
				if (*c == '(' && (c == l.args || c[-1] == ','))
					bputc(&a, '0');
				bputc(&a, *c);
			}
			free(l.args);
			l.args = a.s;
			annotate = 1;
		}
		/* move.l #n,Dn: r68 doesn't make it moveq, gas would. */
		int no_moveq = strcmp(base, "move") == 0 && size == 'l';
		/* move.b with a negative immediate: r68 writes the extension
		 * word $00nn, as $FFnn (both $FFnn for andi.b and the others). */
		int byte_imm = strcmp(base, "move") == 0 && size == 'b' && l.args[0] == '#';
		/* A byte immediate of an external in the immediate instructions
		 * (andi.b #Ext,d0, andi #Ext,ccr; and.b #Ext,(a0), which are
		 * them) and a bit number (btst #Ext,<ea>): both Microware
		 * assemblers relocate the whole extension word (16 bits, $FFFE
		 * for -2), gas only the low byte (move.b keeps a byte). #0, and
		 * before the instruction the value relocated as the 16-bit word
		 * after its first word: MRI's reloc (no .mri 0 around .reloc,
		 * see pcm_marks) through .Lr68w set to the value (gas keeps each
		 * use of a set symbol at its value then; no prelude macro, whose
		 * use would count in the source's \@ numbers). Not with a macro
		 * parameter (unknown). */
		int word_imm = word_byte_imm(base, size, l.args) && !is_macro(l.op);
		pc_ext_offset = strcmp(base, "movem") == 0 || strcmp(base, "fmovem") == 0 ||
			((strcmp(base, "btst") == 0 || strcmp(base, "bchg") == 0 || strcmp(base, "bclr") == 0 ||
			  strcmp(base, "bset") == 0) && l.args[0] == '#') ? 4 : 2;
		if (is_lmacro(l.op)) {
			/* through r68L, which counts the arguments' lengths; the
			 * number of arguments set before (r68__Ln), as r68 counts
			 * them: NARG leaves empty ones out (MAC ,Arg
			 * called its macro with one argument) */
			const char *pc[64];
			size_t pl[64];
			int k = *l.args ? split_commas(l.args, strlen(l.args), pc, pl, 64) : 0;
			if (!st.in_macro) {
				/* Outside macro bodies the arguments are known: their
				 * lengths set here (r68__L1..8, as r68 counts them:
				 * quotes stripped), the macro called directly. r68L's
				 * irpc aligns after an odd dc.b, and its ifnc took no
				 * argument with a quote (MAC #"'"Init"'"). */
				for (int a = 0; a < 8; a++) {
					size_t len = 0;
					if (a < k) {
						const char *t = pc[a];
						size_t tn = pl[a];
						int whole = tn >= 2 && (t[0] == '"' || t[0] == '\'') && t[tn - 1] == t[0];
						for (size_t c = whole; c < tn - whole; c++) {
							if (t[c] == '"' || (t[c] == '\\' && c + 1 < tn && t[c + 1] == '\''))
								continue;
							len++;
						}
					}
					bprintf(&line_before, "r68__L%d set %zu\n", a + 1, len);
				}
				put_marker(&line_before);
				struct buf nm = { 0 };
				put_symbol(&nm, l.op, strlen(l.op));
				bprintf(out, "%s %s__r68 ", labs, nm.s);
				free(nm.s);
				direct_lcall = 1;
				macro_arguments(out, l.args, l.op);
				direct_lcall = 0;
				macro_aliases(l.op, l.args);
				narg_set(l.op, l.args);
				char_aliases(l.op, l.args);
				st.section = NULL;
				bputs(out, rest);
				goto done;
			}
			bprintf(&line_before, "r68__Ln set %d\n", k + 1);
			bprintf(out, "%s r68L %s", labs, l.op);
			if (*l.args) {
				bputc(out, ',');
				macro_arguments(out, l.args, l.op);
				macro_aliases(l.op, l.args);
			}
			narg_set(l.op, l.args);
			char_aliases(l.op, l.args);
			/* where its lines leave the section isn't known */
			st.section = NULL;
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
		if (!is_macro(l.op) && quick_words(out, labs, base, size, l.args, rest))
			goto done;
		if (!is_macro(l.op) && byte_imm_words(out, labs, base, size, l.args, rest))
			goto done;
		size_t start = out->n;
		bprintf(out, "%s %s ", labs, op);
		int need = cpu_needed(base, size, l.args);
		if (is_macro(l.op)) {
			macro_arguments(out, l.args, l.op);
			macro_aliases(l.op, l.args);
			narg_set(l.op, l.args);
			char_aliases(l.op, l.args);
			open_call(l.op);
			/* where its lines leave the section isn't known */
			st.section = NULL;
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
				 * assemblers, gas takes a short one for a small value.
				 * Not a branch's target. */
				int addr_op = memory_instruction(base);
				/* the address without a bit field ({offset:width}:
				 * bftst d0{29:3}) */
				size_t alen = lens[i];
				if (alen > 2 && pieces[i][alen - 1] == '}' && memchr(pieces[i], '{', alen) != NULL)
					alen = (size_t)((const char *)memchr(pieces[i], '{', alen) - pieces[i]);
				/* (a macro parameter alone may be a register; one in an
				 * expression, $E7E+\1, is part of an address: absolute
				 * long in both r68s, checked 2026-10-16) */
				int abs_here = (absolute ? i == 0 : addr_op && (memchr(pieces[i], '\\', lens[i]) == NULL ||
										param_address(pieces[i], lens[i]))) &&
					pieces[i][0] != '#' && alen > 0 &&
					(memchr(pieces[i], '(', alen) == NULL || paren_address(pieces[i], alen)) &&
					!register_list(pieces[i], alen) && !(alen > 2 && pieces[i][alen - 2] == '.') &&
					/* ccr, sr: the registers, also where the source
					 * defines a name ccr (ori #Carry,ccr) */
					!is_reg(pieces[i], alen);
				if (lens[i] == 2 && strncasecmp(pieces[i], "cc", 2) == 0) {
					/* cc: r68's other name of ccr (ori
					 * #Carry,cc) */
					bputs(out, "ccr");
					annotate = 1;
				} else if (lens[i] >= 2 && pieces[i][0] == '"' && pieces[i][lens[i] - 1] == '"') {
					/* No instruction takes a string: a call of a macro
					 * defined later (SBFDesc ...,"sbgiga" in a macro body),
					 * whose quoted argument r68 passes without quotes. */
					put_angle(out, pieces[i] + 1, lens[i] - 2);
				} else if (strcmp(base, "moveq") == 0 && i == 0 && pieces[i][0] == '#' &&
					   known_value(pieces[i] + 1, lens[i] - 1) && !small_number(pieces[i] + 1, lens[i] - 1)) {
					/* moveq #128..#255: r68 takes the byte (moveq #$80
					 * gives -128), gas wants -128..127. */
					struct buf e = { 0 };
					expression(&e, pieces[i] + 1, lens[i] - 1);
					bprintf(out, "#((%s)&$ff)-(((%s)&$80)<<1)", e.s, e.s);
					free(e.s);
					annotate = 1;
				} else if (strcmp(base, "moveq") == 0 && i == 0 && pieces[i][0] == '#' &&
					   moveq_high_sum(pieces[i] + 1, lens[i] - 1)) {
					/* moveq #$80+Ext (an external): the same byte
					 * with the constant less 256, which gas takes */
					bputs(out, "#-$100+(");
					expression(out, pieces[i] + 1, lens[i] - 1);
					bputc(out, ')');
					annotate = 1;
				} else if ((strcmp(base, "moveq") == 0 || byte_imm) && i == 0 && pieces[i][0] == '#' &&
					   macro_equ_imm(pieces[i] + 1, lens[i] - 1, strcmp(base, "moveq") == 0)) {
					bputs(out, "#.Lr68q");
					annotate = 1;
				} else if (word_imm && i == 0) {
					/* equates of externals by their values: gas's .reloc
					 * refuses a symbol that only aliases an external */
					char *v = through_equates(pieces[i] + 1, lens[i] - 1, 0);
					struct buf e = { 0 };
					expression(&e, v, strlen(v));
					free(v);
					bputs(&line_before, ".Lr68w set ");
					bput(&line_before, e.s, e.n);
					bputs(&line_before, "\n reloc *+2,R_68K_16,.Lr68w\n");
					free(e.s);
					bputs(out, "#0");
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
				} else if (abs_here && alen < lens[i]) {
					operand(out, pieces[i], alen, 1);
					operand(out, pieces[i] + alen, lens[i] - alen, 0);
				} else
					operand(out, pieces[i], lens[i], abs_here);
				/* only where gas could take it for moveq: a data register;
				 * and not with * in the value (#exit-*-8: a PC-relative
				 * value, which gas can't add another undefined term to) */
				int r1 = k == 2 ? reg_number(pieces[1], lens[1]) : -1;
				if (no_moveq && i == 0 && pieces[i][0] == '#' && r1 >= 0 && r1 < 8 &&
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
		/* the statement in <>, its !, < and > escaped
		 * (Name: dc.b ">>>>") */
		struct buf ab = { 0 };
		if (*b) {
			bputc(&ab, ',');
			put_angle(&ab, b, strlen(b));
		}
		const char *a = ab.s ? ab.s : "";
		if (wrap_global == 2)
			bprintf(out, " r68xlab %s%s%s", nm.s, a, rest);
		else if (*b && (strcmp(base, "equ") == 0 || strcmp(base, "set") == 0))
			/* NAME: equ value: NAME equ value on one line */
			bprintf(out, " r68gdef %s%s%s", nm.s, a, rest);
		else
			bprintf(out, " r68glab %s%s%s", nm.s, a, rest);
		free(ab.s);
		free(nm.s);
		free(body);
	}
	if (l.op && !st.in_macro) {
		static const char *const quiet[] = { "equ", "set", "macro", "endm", "ifeq", "ifne", "iflt", "ifle",
			"ifgt", "ifge", "ifdef", "ifndef", "ifp1", "else", "endc", "opt", "nam", "ttl", "pag", "page",
			"spc", "use", "xdef", "xref", "org", "do", "lo", "fail", "end", "spanreg", NULL };
		int q = 0;
		for (int i = 0; quiet[i] && !q; i++)
			q = strcmp(base, quiet[i]) == 0;
		if ((strcmp(base, "dc") == 0 || strcmp(base, "ds") == 0 || strcmp(base, "dz") == 0 ||
		     strcmp(base, "dcb") == 0) && size == 'b')
			st.maybe_odd = 1;
		else if (is_macro(l.op) || is_lmacro(l.op))
			st.maybe_odd = 1;
		else if (!q) {
			st.aligning = !st.in_vsect;
			st.maybe_odd = 0;
		}
	}
	if (st.in_vsect && l.label && !st.in_macro && st.section != NULL &&
	    (!l.op || strcmp(base, "ds") == 0 || strcmp(base, "dc") == 0 || strcmp(base, "dz") == 0 ||
	     strcmp(base, "dcb") == 0)) {
		/* for dot_less_vsect */
		st.vlabs = xrealloc(st.vlabs, (st.nvlabs + 1) * sizeof *st.vlabs);
		st.vsecs = xrealloc(st.vsecs, (st.nvlabs + 1) * sizeof *st.vsecs);
		st.vlabs[st.nvlabs] = xstrdup(l.label);
		st.vsecs[st.nvlabs++] = st.section;
	}
	free(lab.s);
	free_line(&l);
	free(unesc);
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
	/* after byte data (a use file of equates after a descriptor's
	 * dc.b "Rbf",0) the marker is owed: gas aligns at it (maybe_odd) */
	if (depth > 0 && st.maybe_odd)
		st.marker_owed = 1;
	else
		fprintf(out, "# 1 \"%s\"\n", path);
	struct buf line = { 0 };
	int lineno = 0;
	int hoisting = 0;
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
		int was_macro = st.in_macro, cond = st.cond_depth;
		translate_line(raw, &line, path, lineno, out, depth);
		/* A macro defined outside conditionals and other macros: moved
		 * before the source (hoisted), gas's MRI mode aligning at its
		 * macro statement after an odd dc.b (descriptor templates:
		 * Mgr dc.b "Scf",0 then a Desc macro), which r68 doesn't;
		 * a comment line keeps the source's line numbers. */
		if (!was_macro && st.in_macro && cond == 0 && !hoisting) {
			hoisting = 1;
			bprintf(&hoisted, "# %d \"%s\"\n", lineno, path);
		}
		if (line_before.n > 0) {
			if (hoisting && was_macro)
				bput(&hoisted, line_before.s, line_before.n);
			else
				fputs(line_before.s, out);
			line_before.n = 0;
		}
		if (st.marker_owed && st.aligning) {
			fprintf(out, "# %d \"%s\"\n", lineno, path);
			st.marker_owed = 0;
		}
		struct buf whole = { 0 };
		if ((annotate || annotate_op) && line.s && orig_stmt.n) {
			/* on the same line, right after the operands, before the
			 * line's own comment (a comment in MRI mode after ;) */
			size_t ln = strlen(line.s), rn = line_rest.n;
			if (rn == 0 || rn > ln || strcmp(line.s + ln - rn, line_rest.s) != 0)
				rn = 0;
			bprintf(&whole, "%.*s\t;[r68: %s]%s\n", (int)(ln - rn), line.s, annotate ? orig_stmt.s : orig_op.s,
				line.s + ln - rn);
		} else {
			bprintf(&whole, "%s\n", line.s ? line.s : "");
		}
		if (line_after.n > 0) {
			bput(&whole, line_after.s, line_after.n);
			line_after.n = 0;
		}
		if (hoisting) {
			bput(&hoisted, whole.s, whole.n);
			fputs("*\n", out);
			if (!st.in_macro)
				hoisting = 0;
		} else {
			fputs(whole.s, out);
			/* A statement translated into several lines (psect: its
			 * section and r68__sid): the next line's number, else gas
			 * counts each later line too high; after byte data owed
			 * (gas aligns at a marker), not in a macro body (replayed
			 * by each expansion) */
			const char *nl = strchr(whole.s, '\n');
			if (nl != NULL && nl[1] != '\0' && !st.in_macro) {
				if (st.maybe_odd)
					st.marker_owed = 1;
				else
					fprintf(out, "# %d \"%s\"\n", lineno + 1, path);
			}
		}
		free(whole.s);
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
	"* the section as is in (r68__sid: 1 .text, 2 .data, 3 .bss, 4 .remote.data,\n"
	"* 5 .remote.bss); r68sect switches only to another one: a section\n"
	"* statement after an odd dc.b aligns in MRI mode, also to the same\n"
	"* section, a macro call doesn't\n"
	"r68__sid set 1\n"
	"r68sect macro\n"
	" ifc '\\1','.text'\n"
	" ifne r68__sid-1\n"
	" section .text\n"
	"r68__sid set 1\n"
	" endc\n"
	" endc\n"
	" ifc '\\1','.data'\n"
	" ifne r68__sid-2\n"
	" section .data\n"
	"r68__sid set 2\n"
	" endc\n"
	" endc\n"
	" ifc '\\1','.bss'\n"
	" ifne r68__sid-3\n"
	" section .bss\n"
	"r68__sid set 3\n"
	" endc\n"
	" endc\n"
	" ifc '\\1','.remote.data'\n"
	" ifne r68__sid-4\n"
	" section .remote.data\n"
	"r68__sid set 4\n"
	" endc\n"
	" endc\n"
	" ifc '\\1','.remote.bss'\n"
	" ifne r68__sid-5\n"
	" section .remote.bss\n"
	"r68__sid set 5\n"
	" endc\n"
	" endc\n"
	" endm\n"
	"* vsect contents: uninitialized (ds), initialized (dc, dz), labels\n"
	"r68ds macro\n"
	" r68sect \\1\n"
	"\\3 ds.\\2 \\4\n"
	" endm\n"
	"r68fill macro\n"
	" r68sect \\1\n"
	"\\2 fill \\3,1,0\n"
	" endm\n"
	"r68dc macro\n"
	" r68sect \\1\n"
	"\\3 dc.\\2 \\4\n"
	" endm\n"
	"r68dz macro\n"
	" r68sect \\1\n"
	"\\3 dcb.\\2 \\4,0\n"
	" endm\n"
	"r68lab macro\n"
	" r68sect \\1\n"
	"\\2\n"
	" endm\n"
	"r68align macro\n"
	" r68sect \\1\n"
	" align \\3\n"
	" r68sect \\2\n"
	" align \\3\n"
	" endm\n"
	"* the offset counter: org, do, lo, and . in expressions\n"
	"r68__ofs set 0\n"
	"* \\# where a call has an empty argument (narg_set)\n"
	"r68__nset set 0\n"
	"* where a macro's dc and ds go: 0 code, 1 vsect, 2 remote vsect\n"
	"r68__vs set 0\n"
	"* the arguments of a call through r68L, the name too\n"
	"r68__Ln set 0\n"
	"r68__psn set 0\n"
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
	" section .data\n"
	"r68__bdata\n"
	" section .bss\n"
	"r68__bbss\n"
	" section .text\n"
	"r68__btext\n";

/* Whether a macro named NAME would clash in gas, whose macro names ignore
 * case: with a prelude macro or with a macro defined before whose name
 * differs from NAME only in case. */
static int macro_name_clash(const char *name)
{
	/* gas's own directives, which MRI mode knows without a dot and finds
	 * before a macro (a macro ERROR: gas's error, "argument must be a
	 * string") */
	static const char *const directives[] = { "error", "warning", "print", "fail", "abort", "include",
		"incbin", "list", "nolist", "format", "noformat", "page", "nopage", "title", "sbttl", "eject",
		"space", "skip", "fill", "zero", "string", "ascii", "asciz", "byte", "word", "long", "short",
		"int", "quad", "octa", "float", "double", "single", "comm", "lcomm", "global", "globl",
		"extern", "weak", "local", "section", "text", "data", "bss", "offset", "struct", "irp",
		"irpc", "rept", "rep", "mexit", "purgem", "exitm", "line", "file", "ident", "version", "err",
		"chip", "cpu", "even", "reg", "save", "restore", "mask2", "comline", "name", "llen", "plen",
		"psize", "lflags", "func", "endfunc", "for", "endf", "repeat", "until", "while", "endw",
		"endi", "elseif", "equiv", "eqv", "sleb128", "uleb128", "p2align", "balign", "balignw",
		"balignl", "ds", "dcb", "dx", "dsb", "xcom", "xdef", "xref", "size", "type", "tag", "val",
		"desc", "dim", "scl", "def", "endef", "loc", "debug", NULL };
	for (const char *const *d = directives; *d; d++)
		if (strcasecmp(name, *d) == 0)
			return 1;
	/* and the instructions: r68's macro names are case sensitive, so
	 * a macro ANDI leaves the instruction andi alone, gas's call it */
	static const char *const insns[] = { "abcd", "add", "adda", "addi", "addq", "addx", "and", "andi", "asl",
		"asr", "bchg", "bclr", "bset", "btst", "bra", "bsr", "bhi", "bls", "bcc", "bcs", "bhs", "blo",
		"bne", "beq", "bvc", "bvs", "bpl", "bmi", "bge", "blt", "bgt", "ble", "chk", "clr", "cmp", "cmpa",
		"cmpi", "cmpm", "divs", "divu", "eor", "eori", "exg", "ext", "illegal", "jmp", "jsr", "lea", "link",
		"lsl", "lsr", "move", "movea", "movem", "movep", "moveq", "muls", "mulu", "nbcd", "neg", "negx",
		"nop", "not", "or", "ori", "pea", "reset", "rol", "ror", "roxl", "roxr", "rte", "rtr", "rts",
		"sbcd", "stop", "sub", "suba", "subi", "subq", "subx", "swap", "tas", "trap", "trapv", "tst",
		"unlk", "dbra", "dbf", "dbt", "st", "sf", "movec", "moves", "rtd", "extb", NULL };
	for (const char *const *d = insns; *d; d++)
		if (strcasecmp(name, *d) == 0 && strcmp(name, *d) != 0)
			return 1;
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
	printf("usage: %s [-a SYM[=VAL]]... [-m N] [-b] [-r] [-y] [-s] [-x EXT] [-u DIR]... [-o OUTPUT] SOURCE\n", progname);
	printf("  Translates a Microware r68 source to a gas source for m68k-elfos9-as --mri\n");
	printf("  (\".mri 1\" inside), keeping its line numbers; see r68togas.md.\n");
	printf("  -a SYM[=VAL]  define SYM (1 without VAL) in the prelude (r68's -a=)\n");
	printf("  -m N       the CPU in the prelude (r68's -m: 0 68000 ... 6 68060); also the\n");
	printf("             padding of the sections (ucc: 2 with -m0, else 4, nop without -m)\n");
	printf("  -b         r68's -b/-bt: branches to this source's labels sized by gas\n");
	printf("  -B         with -b: also to its global labels (gas relaxes those)\n");
	printf("  -r         r68's -r: registers only written with %% (%%d0); d0 is a symbol\n");
	printf("  -y         r68's -y with -b (68020 and later): branches and PC-relative\n");
	printf("             references to labels 32-bit\n");
	printf("  -d D       the dialect: ucc (Ultra C's r68, the default), c32 (C 3.2's R68:\n");
	printf("             its encodings where they differ, sections padded to 2) or ucc95\n");
	printf("             (Ultra C's r68 of 1994-95: as ucc, the code padded with zeros)\n");
	printf("  -u DIR     search DIR for use files (r68's -u)\n");
	printf("  -o OUTPUT  write to OUTPUT (default: standard output); a directory: the\n");
	printf("             output there, named after the source (.a gives .s, .s .gas.s)\n");
	printf("  -s         each use file translated into a file of its own, next to the\n");
	printf("             output and included (default: inline); without -o the output\n");
	printf("             in the working directory, named after the source\n");
	printf("  -x EXT     the extension of the outputs named after their sources\n");
}

/* What the Microware assemblers pad each section of a psect to: 2 for C
 * 3.2's R68 (-d c32) and Ultra C's r68 -m0 and -m1 (-m1 measured
 * 2026-10-13), 4 for Ultra C's r68 otherwise (m68k-os9-r68's
 * section_alignment follows the same rule). */
static int pad_alignment(void)
{
	return st.c32 || (st.cpu_given && st.cpu_level <= 10) ? 2 : 4;
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
			/* a name of the source, a constant: not external in a sum
			 * of two (-a=BASE=.. -a=SIZE=..: #BASE+SIZE). As an
			 * equate, not a set symbol: a set symbol used before its
			 * last set is SYM__final, defined at the end, which a
			 * conditional (ifdef SYM, ifne SYM) sees too late */
			char *name = xstrndup(d, n);
			add_equate(name);
			note_value(name, eq ? eq + 1 : "1");
			free(name);
		} else if (strcmp(argv[i], "-b") == 0) {
			st.branch_sizing = 1;
		} else if (strcmp(argv[i], "-B") == 0) {
			st.relax_globals = 1;
		} else if (strcmp(argv[i], "-r") == 0) {
			reg_prefix_only = 1;
		} else if (strcmp(argv[i], "-y") == 0) {
			long_refs = 1;
		} else if (strcmp(argv[i], "-s") == 0) {
			separate = 1;
		} else if (strncmp(argv[i], "-x", 2) == 0) {
			const char *d = argv[i][2] ? argv[i] + 2 + (argv[i][2] == '=') : (i + 1 < argc ? argv[++i] : NULL);
			if (d == NULL || *d == '\0' || strchr(d, '/') != NULL)
				die("-x takes an extension");
			out_ext = *d == '.' ? d + 1 : d;
		} else if (strncmp(argv[i], "-d", 2) == 0) {
			/* -d c32 | ucc | ucc95: the dialect (whose encodings) */
			const char *d = argv[i][2] ? argv[i] + 2 + (argv[i][2] == '=') : (i + 1 < argc ? argv[++i] : NULL);
			if (d == NULL || (strcmp(d, "c32") != 0 && strcmp(d, "ucc") != 0 && strcmp(d, "ucc95") != 0))
				die("-d takes c32, ucc or ucc95");
			st.c32 = strcmp(d, "c32") == 0;
			st.ucc95 = strcmp(d, "ucc95") == 0;
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
			full_format = d[0] >= '2';
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
	/* -o naming a directory, or -s without -o: the output named after the
	 * source (output_name) there, or in the working directory; then never
	 * over an existing file, which only an explicit -o file may be */
	int named = output != NULL && !is_directory(output);
	if (output != NULL && !named)
		output = join(output, strlen(output), output_name(source));
	else if (output == NULL && separate)
		output = output_name(source);
	if (output != NULL && (strcmp(output, source) == 0 || same_file(output, source)))
		die("%s: won't write over the source", output);
	if (output != NULL && !named && file_exists(output))
		die("%s: exists; written only where -o names it", output);
	if (separate) {
		const char *slash = strrchr(output, '/');
		out_dir = slash ? xstrndup(output, (size_t)(slash - output)) : xstrdup(".");
		if (*out_dir == '\0') {
			free(out_dir);
			out_dir = xstrdup("/");
		}
	}
	FILE *out = stdout;
	char *temp = NULL, *psect_name = NULL;
	if (output != NULL) {
		temp = xmalloc(strlen(output) + 8);
		sprintf(temp, "%s.tmp", output);
		out = fopen(temp, "w");
		if (out == NULL)
			die("cannot write %s", temp);
		note_made(temp);
	}
	/* The translation goes to a temporary file first: the psect's name,
	 * known at its end, is written first, as .file before .mri 1 (a .mri 0
	 * later would make gas expand earlier MRI macros' strings wrongly). */
	FILE *final = out;
	out = temp_file();
	if (out == NULL)
		die("cannot make a temporary file");
	if (arch != NULL)
		fprintf(out, " .arch %s\n", arch);
	fputs(prelude, out);
	/* r68glab NAME[,<rest>]: a global label in a conditional, with a marker;
	 * the label on the statement's line, so that it moves with the
	 * statement's alignment after odd data, as in r68 */
	fputs("r68glab macro\nr68__g_\\1 set 1\n\\1 \\2\n endm\n", out);
	/* r68imml OPCODE,VALUE: an instruction word and a long immediate (-d c32) */
	fputs("r68imml macro\n dc.w \\1\n dc.l \\2\n endm\n", out);
	/* r68immb OPCODE,VALUE: an instruction word and a byte immediate of an
	 * external (-d c32) */
	fputs("r68immb macro\n dc.w \\1\n dc.b 0,\\2\n endm\n", out);
	/* r68com NAME,SIZE: a common block */
	fputs("r68com macro\n .mri 0\n .comm \\1,\\2,2\n .mri 1\n endm\n", out);
	/* r68xlab NAME[,<rest>]: a global label made by a macro expansion */
	fputs("r68xlab macro\n xdef \\1\n\\1 \\2\n endm\n", out);
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
		fprintf(out, " ifeq r68__Ln-%d\n \\1__r68", n);
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
			/* -B: gas sizes branches to global labels too */
			for (int j = 0; j < nglobals; j++) {
				st.locals = xrealloc(st.locals, (st.nlocals + 1) * sizeof *st.locals);
				st.locals[st.nlocals++] = globals[j];
			}
		}
	}
	long source_at = ftell(out);
	translate_file(source, out, 0);
	r68x_ok = 0;
	/* A psect without ends: the end of its code, before the line marker
	 * (which gas aligns after an odd dc.b, as any directive) */
	/* Without ends where the source has none outside conditionals; where
	 * one is in a conditional or a macro body (after a call of a macro
	 * leaving a conditional open, which can leave the rest of the source
	 * out), gas tells: r68__ended. */
	int open_conds = st.cond_depth;
	int maybe_ends = pad_alignment() == 4 && !st.in_vsect && (st.section == NULL || same_section(".text"));
	int no_ends = maybe_ends && st.in_psect && st.cond_depth == 0;
	int dyn_ends = maybe_ends && !no_ends && st.npsects > 0 && (st.ends_cond || st.cond_depth > 0);
	/* r68 ends a source in a conditional without a word
	 * (ifne DEBUG): closed here, gas doesn't. */
	if (st.cond_depth > 0)
		fprintf(out, "* closing %d conditional%s the source leaves open, as r68 does\n", st.cond_depth,
			st.cond_depth > 1 ? "s" : "");
	for (; st.cond_depth > 0; st.cond_depth--)
		fputs(" endc\n", out);
	if (no_ends || dyn_ends)
		fputs("r68__pend ds.b 0\n", out);
	fprintf(out, "# 1 \"r68.epilogue\"\n");
	if (dyn_ends) {
		fputs("r68__ended set 0\n", out);
		for (int k = 1; k <= st.nends; k++)
			fprintf(out, " ifdef .Lr68ends%d\nr68__ended set 1\n endc\n", k);
	}
	(void)open_conds;
	/* Each section padded as the Microware assemblers pad a psect's
	 * (measured 2026-10-09, -m1 2026-10-13): C 3.2's R68 and Ultra C's
	 * r68 -m0 and -m1 to 2; Ultra C's r68 otherwise to 4, the code with
	 * nop (without -m) or trapf (-m2..6), after a zero byte where the size
	 * is odd; Ultra C's r68 of 1994-95 (-d ucc95) with zeros (measured
	 * 2026-10-15 on the objects of an OS-9 V3.0 port). m68k-os9-r68 gives
	 * the sections that alignment too. */
	{
		int to = pad_alignment();
		unsigned fill = st.ucc95 ? 0 : st.cpu_given ? 0x51FC : 0x4E71;
		fprintf(out, "* padding: each section to %d, as %s\n", to,
			st.c32 ? "C 3.2's R68" : to == 2 ? "Ultra C's r68 -m0/-m1" : st.ucc95 ? "Ultra C's r68 of 1994-95" :
			st.cpu_given ? "Ultra C's r68 -m2..6" : "Ultra C's r68");
		if (to == 2) {
			fprintf(out, " section .text\n balign 2,0\n");
		} else if (dyn_ends) {
			fprintf(out, "* no ends assembled: an odd size filled with zeros\n section .text\n"
				" ifeq r68__ended\n"
				" ds.b ((r68__pend-r68__btext)&1)*(3-((r68__pend-r68__btext)&3))\n"
				" endc\n"
				" balignw %d,$%04X\n", to, fill);
		} else if (no_ends) {
			/* A psect without ends (descriptors of 1992-1994 ported
			 * trees): Ultra C's r68 fills an odd size with zeros to
			 * 4, an even one as with ends (measured 2026-10-11). The
			 * line marker added the zero byte (gas's MRI alignment
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
		/* (Ultra C's r68 of 1994-95 pads the vsect's sections to 4 as
		 * well, but aligns them to 2: m68k-os9-r68 sets the alignment;
		 * measured 2026-10-15 on 10 modules) */
		fprintf(out, " section .data\n balign %d,0\n section .bss\n balign %d\n", to, to);
		if (st.used_remote)
			fprintf(out, " section .remote.data\n balign %d,0\n section .remote.bss\n balign %d\n", to, to);
		fputs(" section .text\n", out);
	}
	if (st.ord.n > 0)
		fprintf(out, "* the order of the references (order_record)\n section .r68ord\n%s section .text\n", st.ord.s);
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
		/* (a NAME__p1 a conditional left out: NAME) */
		if (name_in(st.p1s, st.np1s, st.finals[i], strlen(st.finals[i])))
			fprintf(out, " ifdef %s__p1\n%s__final equ %s__p1\n else\n%s__final equ %s\n endc\n", nm.s, nm.s,
				nm.s, nm.s, nm.s);
		else
			fprintf(out, "%s__final equ %s\n", nm.s, nm.s);
		free(nm.s);
	}
	/* psect name,type/lang,attr/rev,edition,stack,entry[,trap entry]: the
	 * module header symbols, as rof2elf makes them; of the psect statement
	 * assembled (r68__psn) where there are more */
	for (int j = 0; j < st.npsects; j++) {
		const char *pieces[8];
		size_t lens[8];
		int k = split_commas(st.ps_all[j], strlen(st.ps_all[j]), pieces, lens, 8);
		static const char *const names[] = { NULL, "__os9_typelang", "__os9_attrev", "__os9_edition",
			"__os9_stack", "__os9_entry", "__os9_trapent" };
		memcpy(st.ps_captured, st.ps_all_captured[j], sizeof st.ps_captured);
		if (st.npsects > 1)
			fprintf(out, " ifeq r68__psn-%d\n", j + 1);
		if (k >= 2) {
			/* r68__ps<n>: the values at the psect statement where
			 * they were known there (expr_complete), else the
			 * expression at the end, as before. No header symbols
			 * for a type/language 0 (a library's psect). */
			struct buf tl = { 0 };
			if (st.ps_captured[1])
				bputs(&tl, "r68__ps1");
			else
				expression(&tl, pieces[1], lens[1]);
			fprintf(out, " ifne %s\n", tl.s);
			free(tl.s);
			for (int i = 1; i < k && i < 7; i++) {
				/* The entry points are code offsets: a number is one
				 * from the start of the code, as in rof2elf's object. */
				int code = (i == 5 || i == 6) && is_number(pieces[i], lens[i]);
				struct buf e = { 0 };
				if (st.ps_captured[i])
					bprintf(&e, "r68__ps%d", i);
				else
					expression(&e, pieces[i], lens[i]);
				fprintf(out, " xdef %s\n%s equ %s%s\n", names[i], names[i], code ? "r68__btext+" : "", e.s);
				free(e.s);
			}
			/* type/language 0: attributes, revision, edition and stack
			 * as local symbols, for elf2rof's ROF header (__os9l_*) */
			static const char *const lnames[] = { NULL, NULL, "__os9l_attrev", "__os9l_edition", "__os9l_stack" };
			fprintf(out, " else\n");
			for (int i = 2; i < k && i < 5; i++) {
				struct buf e = { 0 };
				if (st.ps_captured[i])
					bprintf(&e, "r68__ps%d", i);
				else
					expression(&e, pieces[i], lens[i]);
				fprintf(out, "%s equ %s\n", lnames[i], e.s);
				free(e.s);
			}
			fprintf(out, " endc\n");
		}
		if (st.npsects > 1)
			fprintf(out, " endc\n");
		/* without a name (a bare psect), "program", as both Microware
		 * assemblers name it in the ROF (measured 2026-10-17) */
		if (j == st.npsects - 1)
			psect_name = k >= 1 && lens[0] > 0 ? xstrndup(pieces[0], lens[0]) : xstrdup("program");
	}
	fprintf(final, "# 1 \"r68.prelude\"\n");
	if (psect_name != NULL)
		fprintf(final, " .file \"%s\"\n", psect_name);
	if (r68x_notes.s != NULL)
		/* the link-time expressions' definitions (r68x_note.h), not loaded */
		fprintf(final, " .section " R68X_SECTION ",\"\"\n .ascii \"%s\"\n .text\n", r68x_notes.s);
	if (st.ndlvs > 0) {
		/* the .-LABEL symbols (dot_less_vsect), which gas leaves out: each
		 * one's value as a relocation in .r68dlv, its name and whether
		 * global in .r68dlvn, for m68k-os9-r68 to add it */
		fputs(" .section .r68dlv,\"\"\n", final);
		for (int i = 0; i < st.ndlvs; i++) {
			struct buf nm = { 0 };
			put_symbol(&nm, st.dlvs[i], strlen(st.dlvs[i]));
			fprintf(final, " .long %s\n", nm.s);
			free(nm.s);
		}
		fputs(" .section .r68dlvn,\"\"\n", final);
		for (int i = 0; i < st.ndlvs; i++) {
			struct buf nm = { 0 };
			put_symbol(&nm, st.dlvs[i], strlen(st.dlvs[i]));
			char *needle = xmalloc(nm.n + 16);
			sprintf(needle, " xdef %s\n", nm.s);
			int global = st.globals.s != NULL && strstr(st.globals.s, needle) != NULL;
			fprintf(final, " .asciz \"%s %c\"\n", st.dlvs[i], global ? 'g' : 'l');
			free(needle);
			free(nm.s);
		}
		fputs(" .text\n", final);
	}
	if (st.used_remote)
		/* The remote data's sections with their ELF flags (MRI's section
		 * has none to give): allocated, writable, .remote.bss without
		 * contents, as GCC's. */
		fprintf(final, " .section .remote.data,\"aw\"\n .section .remote.bss,\"aw\",@nobits\n .text\n");
	if (st.used_pcm)
		/* pcm_marks's section, not allocated */
		fputs(" .section .r68pcm,\"\"\n .text\n", final);
	if (st.used_debug)
		/* debug_line's section, not allocated, a label at its start */
		fputs(" .section .r68dbg,\"\"\n.Lr68dbg0:\n .text\n", final);
	if (st.ord.n > 0)
		/* order_record's section, not allocated */
		fputs(" .section .r68ord,\"\"\n .text\n", final);
	if (st.aliases.s != NULL) {
		/* the sections' aliases the link-time expressions name
		 * (label_alias): hidden globals at their start (nothing is in
		 * the sections yet here) */
		static const char secs[] = "tdbDB";
		static const char *const sections[] = { ".text", ".data", ".bss", ".remote.data", ".remote.bss" };
		for (const char *a = st.aliases.s; *a;) {
			const char *e = strchr(a, '\n');
			const char *w = strchr(secs, *a);
			fprintf(final, " .section %s\n%.*s:\n .globl %.*s\n .hidden %.*s\n", sections[w - secs], (int)(e - a - 2),
				a + 2, (int)(e - a - 2), a + 2, (int)(e - a - 2), a + 2);
			a = e + 1;
		}
		fputs(" .text\n", final);
	}
	rewind(out);
	char chunk[8192];
	size_t got;
	/* the prelude, the hoisted macro definitions, the source */
	for (long left = source_at; left > 0 && (got = fread(chunk, 1, left < (long)sizeof chunk ? (size_t)left : sizeof chunk, out)) > 0;
	     left -= (long)got)
		fwrite(chunk, 1, got, final);
	if (hoisted.n > 0)
		fwrite(hoisted.s, 1, hoisted.n, final);
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

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '@')
			bputs(b, "_at_");
		else
			bputc(b, s[i]);
	}
	if (clashes(s, n))
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

static void skipsp(struct lexer *lx)
{
	(void)lx;
}

/* One operand: a number, symbol, character constant, '*', '.', a macro
 * parameter, a parenthesized expression, or a unary operator before one. */
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
		else
			bput(b, p, q - p);
		lx->p = q;
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
		if (expr_chars || count != 1)
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
		put_symbol(b, p, q - p);
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

/* Translates expression text E (N chars) into B; returns 0 when it isn't
 * one r68 would take whole (then copied as it is). */
static int expression(struct buf *b, const char *e, size_t n)
{
	char *s = xstrndup(e, n);
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

/* ----- Operands ----- */

/* Whether text S (N chars) is a register as r68 writes one in an address
 * mode: Dn, An, sp, pc, pcr, with an index size (.w, .l) and scale (*n). */
static int register_part(const char *s, size_t n)
{
	size_t k = 0;
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

/* Translates one instruction operand. ABSOLUTE: a bare expression operand
 * is an absolute address (r68's jsr/jmp/lea/pea label: absolute long). */
static void operand(struct buf *b, const char *s, size_t n, int absolute)
{
	if (n == 0)
		return;
	if (*s == '#') {
		bputc(b, '#');
		expression(b, s + 1, n - 1);
		return;
	}
	if (register_list(s, n)) {
		bput(b, s, n);
		return;
	}
	if (n >= 4 && s[0] == '-' && s[1] == '(' && s[n - 1] == ')') {
		bput(b, s, n);
		return;
	}
	if (n >= 4 && s[0] == '(' && s[n - 2] == ')' && s[n - 1] == '+') {
		bput(b, s, n);
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
				expression(b, s, open);
				bputc(b, ')');
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
						bput(b, pieces[i], lens[i]);
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
static void string_operand(struct buf *b, const char *s, size_t n)
{
	char q = s[0];
	bputc(b, '\'');
	for (size_t i = 1; i < n; i++) {
		char c = s[i];
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
	const char *section;	/* the current section, null if not known */
	char *psect;		/* the psect line's fields */
	char *psect_args;
	struct buf globals;	/* xdef lines for the epilogue */
	int masks;		/* r68__mask<n> symbols made */
	struct buf epilogue_more;
	const char *search[64];	/* -u directories */
	int nsearch;
	int errors;
	char **macros;		/* the macros defined so far */
	int nmacros;
};

static struct state st;

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

/* Expressions separated by commas (dc, the if directives' operands...). */
static void expression_list(struct buf *b, const char *args, int chars)
{
	const char *pieces[256];
	size_t lens[256];
	int k = split_commas(args, strlen(args), pieces, lens, 256);
	expr_chars = chars;
	for (int i = 0; i < k; i++) {
		if (i)
			bputc(b, ',');
		if (lens[i] > 0 && (pieces[i][0] == '"' || (pieces[i][0] == '\'' && !chars)))
			string_operand(b, pieces[i], lens[i]);
		else
			expression(b, pieces[i], lens[i]);
	}
	expr_chars = 0;
}

static void translate_file(const char *path, FILE *out, int depth);

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
	char *found = NULL;
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
static int is_macro(const char *op)
{
	for (int i = 0; i < st.nmacros; i++)
		if (ci_eq(st.macros[i], op))
			return 1;
	return 0;
}

/* A macro call's arguments: a quoted one passes its text without the
 * quotes in r68, as <...> does in MRI; the others as operands. */
static void macro_arguments(struct buf *b, const char *args)
{
	const char *pieces[64];
	size_t lens[64];
	int k = split_commas(args, strlen(args), pieces, lens, 64);
	for (int i = 0; i < k; i++) {
		if (i)
			bputc(b, ',');
		if (lens[i] >= 2 && pieces[i][0] == '"' && pieces[i][lens[i] - 1] == '"') {
			bputc(b, '<');
			bput(b, pieces[i] + 1, lens[i] - 2);
			bputc(b, '>');
		} else if (lens[i] >= 4 && (strncasecmp(pieces[i], "addi", 4) == 0 || strncasecmp(pieces[i], "subi", 4) == 0) &&
			   (lens[i] == 4 || pieces[i][4] == '.')) {
			/* A mnemonic argument (DTDEST subi.w,#1,V_Counter): as in
			 * an instruction, add/sub for r68's quick forms. */
			bput(b, pieces[i], 3);
			bput(b, pieces[i] + 4, lens[i] - 4);
		} else {
			operand(b, pieces[i], lens[i], 0);
		}
	}
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
			operand(b, pieces[i], lens[i], 0);
		}
	}
}

/* Translates one statement line into OUT (one line, no newline). */
static void translate_line(const char *text, struct buf *out, const char *path, int lineno, FILE *fp, int depth)
{
	struct line l;
	if (!split_line(text, &l)) {
		bputs(out, text);
		return;
	}
	char base[32], size;
	if (l.op)
		op_parts(l.op, base, sizeof base, &size);
	else
		base[0] = '\0', size = 0;
	/* Outside a psect (definition files used before it) r68 exports
	 * nothing. */
	if (l.label && l.global && st.in_psect)
		add_global(l.label);

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
	if (strcmp(base, "rept") == 0) {
		st.section = NULL;
		bprintf(out, "%s rept ", labs);
		expression(out, l.args, strlen(l.args));
		goto done;
	}
	if (strcmp(base, "com") == 0) {
		fprintf(stderr, "%s:%d: com (common blocks) isn't supported\n", path, lineno);
		st.errors++;
		bprintf(out, "*%s", text);
		goto done;
	}
	if (strcmp(base, "org") == 0) {
		bputs(out, "r68__ofs set ");
		expression(out, l.args, strlen(l.args));
		bputs(out, rest);
		goto done;
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
			if (same)
				bprintf(out, "%s dc.%c ", labs, sz);
			else
				bprintf(out, " r68dc %s,%c,%s,<", sect, sz, nm.s ? nm.s : "");
			expression_list(out, l.args, sz != 'b');
			if (!same)
				bputc(out, '>');
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
		expression_list(out, l.args, size != 'b');
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
	    strncmp(base, "if", 2) == 0 || strcmp(base, "align") == 0 || strcmp(base, "else") == 0 ||
	    strcmp(base, "endc") == 0 || strcmp(base, "macro") == 0 || strcmp(base, "endm") == 0 ||
	    strcmp(base, "endr") == 0 || strcmp(base, "os9") == 0 || strcmp(base, "tcall") == 0) {
		/* Where a conditional or macro leaves the section isn't known. */
		if (strcmp(base, "equ") != 0 && strcmp(base, "set") != 0 && strcmp(base, "ds") != 0 &&
		    strcmp(base, "os9") != 0 && strcmp(base, "tcall") != 0 && strcmp(base, "align") != 0)
			st.section = NULL;
		bprintf(out, "%s %s ", labs, l.op);
		if (strcmp(base, "macro") == 0 && l.label) {
			st.macros = xrealloc(st.macros, (st.nmacros + 1) * sizeof *st.macros);
			st.macros[st.nmacros++] = xstrdup(l.label);
		}
		if (strcmp(base, "ifc") == 0 || strcmp(base, "ifnc") == 0 || strcmp(base, "macro") == 0)
			bputs(out, l.args);
		else
			expression_list(out, l.args, 0);
		bputs(out, rest);
		goto done;
	}
	/* An instruction (or a macro call): its operands. */
	{
		/* The caches of cinv and cpush (bc, dc, ic, nc) aren't symbols. */
		if (strncmp(base, "cinv", 4) == 0 || strncmp(base, "cpush", 5) == 0) {
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
		} else if (strcmp(base, "addi") == 0 || strcmp(base, "subi") == 0) {
			/* r68 makes addi/subi #1..8 addq/subq; as does that for
			 * add/sub. */
			snprintf(opb, sizeof opb, "%.3s%s", base, l.op + 4);
			op = opb;
		}
		/* move.l #n,Dn: r68 doesn't make it moveq, as would. */
		int no_moveq = strcmp(base, "move") == 0 && size == 'l';
		/* move.b with a negative immediate: r68 writes the extension
		 * word $00nn, as $FFnn (both $FFnn for andi.b and the others). */
		int byte_imm = strcmp(base, "move") == 0 && size == 'b' && l.args[0] == '#' && l.args[1] == '-';
		pc_ext_offset = strcmp(base, "movem") == 0 || strcmp(base, "fmovem") == 0 ||
			((strcmp(base, "btst") == 0 || strcmp(base, "bchg") == 0 || strcmp(base, "bclr") == 0 ||
			  strcmp(base, "bset") == 0) && l.args[0] == '#') ? 4 : 2;
		bprintf(out, "%s %s ", labs, op);
		if (is_macro(l.op)) {
			macro_arguments(out, l.args);
		} else if (strcmp(base, "movem") == 0 || strcmp(base, "fmovem") == 0) {
			movem_operands(out, l.args);
		} else {
			const char *pieces[32];
			size_t lens[32];
			int k = split_commas(l.args, strlen(l.args), pieces, lens, 32);
			for (int i = 0; i < k; i++) {
				if (i)
					bputc(out, ',');
				int abs_here = absolute && i == 0 && pieces[i][0] != '#' && strchr(pieces[i], '(') == NULL &&
					!register_list(pieces[i], lens[i]) && !(lens[i] > 2 && pieces[i][lens[i] - 2] == '.');
				if (lens[i] > 0 && pieces[i][0] == '"')
					string_operand(out, pieces[i], lens[i]);
				else if (byte_imm && i == 0 && is_number(pieces[i] + 1, lens[i] - 1)) {
					bputs(out, "#(");
					expression(out, pieces[i] + 1, lens[i] - 1);
					bputs(out, ")&$ff");
				} else
					operand(out, pieces[i], lens[i], abs_here);
				if (no_moveq && i == 0 && pieces[i][0] == '#')
					bputs(out, "+r68__0");
			}
		}
		bputs(out, rest);
	}
done:
	free(lab.s);
	free_line(&l);
}

/* Reads PATH and writes its translation, behind a linemarker, to OUT.
 * Lines end in CR, LF or CR LF; their count is kept. */
static void translate_file(const char *path, FILE *out, int depth)
{
	if (depth > 16)
		die("%s: use nested too deeply", path);
	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		die("cannot read %s", path);
	struct buf text = { 0 };
	char chunk[8192];
	size_t n;
	while ((n = fread(chunk, 1, sizeof chunk, fp)) > 0)
		bput(&text, chunk, n);
	fclose(fp);
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
		translate_line(raw, &line, path, lineno, out, depth);
		fprintf(out, "%s\n", line.s ? line.s : "");
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
	" trap #\\1\n"
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

static void usage(void)
{
	printf("usage: %s [-a SYM[=VAL]]... [-m N] [-u DIR]... [-o OUTPUT] SOURCE\n", progname);
	printf("  Translates a Microware r68 source to a GNU as source for as --mri\n");
	printf("  (\".mri 1\" inside), keeping its line numbers; see r68togas.md.\n");
	printf("  -a SYM[=VAL]  define SYM (1 without VAL) in the prelude (r68's -a=)\n");
	printf("  -m N       the CPU in the prelude (r68's -m: 0 68000 ... 6 68060)\n");
	printf("  -u DIR     search DIR for use files (r68's -u)\n");
	printf("  -o OUTPUT  write to OUTPUT (default: standard output)\n");
}

int main(int argc, char **argv)
{
	const char *output = NULL, *source = NULL;
	struct buf defs = { 0 };
	const char *arch = NULL;
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
	char *temp = NULL;
	if (output != NULL) {
		temp = xmalloc(strlen(output) + 8);
		sprintf(temp, "%s.tmp", output);
		out = fopen(temp, "w");
		if (out == NULL)
			die("cannot write %s", temp);
	}
	fprintf(out, "# 1 \"r68.prelude\"\n");
	if (arch != NULL)
		fprintf(out, " .arch %s\n", arch);
	fputs(prelude, out);
	fputs(defs.s ? defs.s : "", out);
	translate_file(source, out, 0);
	fprintf(out, "# 1 \"r68.epilogue\"\n");
	fputs(st.globals.s ? st.globals.s : "", out);
	fputs(st.epilogue_more.s ? st.epilogue_more.s : "", out);
	fputs("r68__0 equ 0\n", out);
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
			fprintf(out, " .mri 0\n .file \"%.*s\"\n", (int)lens[0], pieces[0]);
	}
	if (output != NULL) {
		if (fclose(out) != 0)
			die("cannot write %s", temp);
		remove(output);
		if (rename(temp, output) != 0)
			die("cannot write %s", output);
	}
	return st.errors ? 1 : 0;
}

/* gastor68: GCC's GNU as output to Microware r68 sources.
 *
 * The reverse of r68togas, for what GCC writes (m68k-elfos9-gcc -S with
 * m68k-os9-xcc's flags): sections as the psect and vsects, registers
 * without '%', Motorola operands in r68's form, j<cc> as b<cc>.w, movem
 * masks as register lists, data and strings as dc. What has no r68 form is
 * an error naming the construct. See gastor68.md.
 *
 * MIT License; see elf2mod.c.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *progname = "gastor68";
static const char *input_name;
static int lineno, errors;

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

/* An error at the current input line; the translation goes on, and no
 * output is kept. */
static void error(const char *format, ...)
{
	va_list ap;
	fprintf(stderr, "%s:%d: ", input_name, lineno);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fputc('\n', stderr);
	errors++;
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

static char *xstrndup(const char *s, size_t n)
{
	char *d = xmalloc(n + 1);
	memcpy(d, s, n);
	d[n] = '\0';
	return d;
}

static char *xstrdup(const char *s)
{
	return xstrndup(s, strlen(s));
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

/* ----- Name sets ----- */

struct names {
	char **v;
	int n;
};

static int has(const struct names *s, const char *name, size_t n)
{
	for (int i = 0; i < s->n; i++)
		if (strlen(s->v[i]) == n && strncmp(s->v[i], name, n) == 0)
			return 1;
	return 0;
}

static void add(struct names *s, const char *name, size_t n)
{
	if (has(s, name, n))
		return;
	s->v = xrealloc(s->v, (s->n + 1) * sizeof *s->v);
	s->v[s->n++] = xstrndup(name, n);
}

/* ----- What the pre-pass collects ----- */

static struct names globals;	/* .globl */
static char *file_name;		/* .file */
static char *header[6];		/* __os9_typelang ... __os9_entry, as written */
static const char *const header_names[] = { "__os9_typelang", "__os9_attrev", "__os9_edition", "__os9_stack",
	"__os9_entry", "__os9_trapent" };

/* ----- Tokens ----- */

/* GNU's numeric local labels (1:, 1b, 1f): the definitions of each
 * number so far; label N's k-th definition is r68's gN.k. */
static int local_count[100];

static int symstart(int c)
{
	return isalpha(c) || c == '_' || c == '.' || c == '$';
}

static int symchar(int c)
{
	return isalnum(c) || c == '_' || c == '.' || c == '$';
}

/* GCC's local labels (.L5, .LC0) as r68 names: "g" before them, which no C
 * name has (C names have no '.'); others as they are. */
static void put_symbol(struct buf *b, const char *s, size_t n)
{
	if (n >= 2 && s[0] == '.' && s[1] == 'L')
		bputc(b, 'g');
	else if (s[0] == '.' || s[0] == '$')
		error("symbol %.*s: not a name r68 takes", (int)n, s);
	bput(b, s, n);
}

/* The registers, without their '%'. */
static int register_name(const char *s, size_t n, struct buf *b)
{
	static const char *const regs[] = {
		"d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
		"sp", "pc", "sr", "ccr", "usp", "vbr", "sfc", "dfc", "cacr", "caar", "msp", "isp",
		"fp0", "fp1", "fp2", "fp3", "fp4", "fp5", "fp6", "fp7", "fpcr", "fpsr", "fpiar", "zpc", NULL,
	};
	if (n == 2 && strncmp(s, "fp", 2) == 0) {
		/* To GNU as, %fp is a6 (the frame pointer). */
		bputs(b, "a6");
		return 1;
	}
	for (const char *const *r = regs; *r; r++)
		if (strlen(*r) == n && strncmp(*r, s, n) == 0) {
			bput(b, s, n);
			return 1;
		}
	return 0;
}

/* Translates text S (N chars), an operand or expression, token by token:
 * %reg to reg, 0x to $, symbols renamed, MIT and Motorola address modes as
 * r68's (done by the caller). */
static void tokens(struct buf *b, const char *s, size_t n)
{
	size_t i = 0;
	while (i < n) {
		char c = s[i];
		if (c == '%') {
			size_t j = i + 1;
			while (j < n && isalnum((unsigned char)s[j]))
				j++;
			if (!register_name(s + i + 1, j - i - 1, b))
				error("register %.*s: not known", (int)(j - i), s + i);
			/* an index register's size (%d0.l) */
			if (j + 1 < n && s[j] == '.' && strchr("wlWL", s[j + 1])) {
				bput(b, s + j, 2);
				j += 2;
			}
			i = j;
		} else if (c == '0' && i + 1 < n && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
			size_t j = i + 2;
			while (j < n && isxdigit((unsigned char)s[j]))
				j++;
			bputc(b, '$');
			bput(b, s + i + 2, j - i - 2);
			i = j;
		} else if (isdigit((unsigned char)c)) {
			size_t j = i;
			while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '.'))
				j++;
			size_t k = i;
			while (k < j && isdigit((unsigned char)s[k]))
				k++;
			if (k == j - 1 && (s[k] == 'f' || s[k] == 'b') && k - i <= 2) {
				/* a GNU local label reference: 1f, 1b */
				int num = atoi(s + i);
				bprintf(b, "g%d.%d", num, local_count[num] + (s[k] == 'f'));
			} else {
				bput(b, s + i, j - i);
			}
			i = j;
		} else if (symstart((unsigned char)c)) {
			size_t j = i;
			while (j < n && symchar((unsigned char)s[j]))
				j++;
			/* A size after a name (sym:w) is GNU's; r68 needs none. */
			put_symbol(b, s + i, j - i);
			i = j;
		} else if (c == ':' && i + 1 < n && strchr("wlWL", s[i + 1]) && (i + 2 == n || !isalnum((unsigned char)s[i + 2]))) {
			/* (sym:w,%pc): the size of a displacement */
			i += 2;
		} else {
			bputc(b, c);
			i++;
		}
	}
}

/* Splits S at top-level commas; returns the number of pieces. */
static int split_commas(const char *s, size_t n, const char **piece, size_t *len, int max)
{
	int count = 0, depth = 0;
	char quote = 0;
	size_t start = 0;
	for (size_t i = 0; i <= n; i++) {
		char c = i < n ? s[i] : ',';
		if (quote && i < n) {
			if (c == '\\' && i + 1 < n)
				i++;
			else if (c == quote)
				quote = 0;
			continue;
		}
		if (c == '"' && i < n) {
			quote = c;
			continue;
		}
		if (c == '(' || c == '{')
			depth++;
		else if (c == ')' || c == '}')
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

/* One operand in r68's form: (d,An) as d(An), (d,An,Xn) as d(An,Xn),
 * (An) and the others as they are; MIT's An@(d,Xn:s) as d(An,Xn.s). */
static void operand(struct buf *b, const char *s, size_t n)
{
	/* MIT: %pc@(2,%d0:w), %a0@(4), %a0@, %a0@+, %a0@- */
	const char *at = memchr(s, '@', n);
	if (at != NULL && s[0] == '%') {
		const char *base = s;
		size_t bn = at - s;
		const char *rest = at + 1;
		size_t rn = n - (rest - s);
		if (rn == 0) {
			bputc(b, '(');
			tokens(b, base, bn);
			bputc(b, ')');
			return;
		}
		if (rn == 1 && (*rest == '+' || *rest == '-')) {
			if (*rest == '-')
				bputc(b, '-');
			bputc(b, '(');
			tokens(b, base, bn);
			bputc(b, ')');
			if (*rest == '+')
				bputc(b, '+');
			return;
		}
		if (rest[0] == '(' && rest[rn - 1] == ')') {
			const char *pieces[4];
			size_t lens[4];
			int k = split_commas(rest + 1, rn - 2, pieces, lens, 4);
			tokens(b, pieces[0], lens[0]);
			bputc(b, '(');
			tokens(b, base, bn);
			for (int i = 1; i < k; i++) {
				bputc(b, ',');
				/* %d0:w is d0.w */
				struct buf x = { 0 };
				tokens(&x, pieces[i], lens[i]);
				for (char *c = x.s; c && *c; c++)
					if (*c == ':')
						*c = '.';
				bputs(b, x.s ? x.s : "");
				free(x.s);
			}
			bputc(b, ')');
			return;
		}
		error("operand %.*s: not a form r68 has", (int)n, s);
		bput(b, s, n);
		return;
	}
	/* A bit field: d0{#30:#2} is r68's d0{30:2}. */
	const char *brace = memchr(s, '{', n);
	if (brace != NULL) {
		struct buf t = { 0 };
		for (size_t i = 0; i < n; i++)
			if (!(s[i] == '#' && i > 0 && (s[i - 1] == '{' || s[i - 1] == ':')))
				bputc(&t, s[i]);
		size_t at = memchr(t.s, '{', t.n) - (void *)t.s;
		operand(b, t.s, at);
		tokens(b, t.s + at, t.n - at);
		free(t.s);
		return;
	}
	/* d(An...) with a number that doesn't fit (below): as (d,An...) */
	if (n > 2 && s[n - 1] == ')' && (isdigit((unsigned char)s[0]) || s[0] == '-')) {
		const char *open = memchr(s, '(', n);
		if (open != NULL && open > s) {
			struct buf t = { 0 };
			bputc(&t, '(');
			bput(&t, s, open - s);
			bputc(&t, ',');
			bput(&t, open + 1, n - (open - s) - 1);
			char *d = xstrndup(s, open - s), *end;
			long v = strtol(d, &end, 0);
			int number = *end == '\0';
			free(d);
			int index = memchr(open, ',', n - (open - s)) != NULL;
			if (number && (index ? (v < -128 || v > 127) : (v < -32768 || v > 32767))) {
				operand(b, t.s, t.n);
				free(t.s);
				return;
			}
			free(t.s);
		}
	}
	/* Motorola: (d,An...) with a displacement first */
	if (n >= 3 && s[0] == '(' && s[n - 1] == ')') {
		const char *pieces[4];
		size_t lens[4];
		int k = split_commas(s + 1, n - 2, pieces, lens, 4);
		if (k >= 2 && pieces[0][0] != '%') {
			/* A number that doesn't fit the 8 bits of an index mode or
			 * the 16 of d(An): r68's (d,An,Xn), which has 32 (68020). */
			char *d = xstrndup(pieces[0], lens[0]);
			char *end;
			long v = strtol(d, &end, 0);
			int number = *end == '\0';
			free(d);
			if (number && (k >= 3 ? (v < -128 || v > 127) : (v < -32768 || v > 32767))) {
				bputc(b, '(');
				for (int i = 0; i < k; i++) {
					if (i)
						bputc(b, ',');
					tokens(b, pieces[i], lens[i]);
				}
				bputc(b, ')');
				return;
			}
			tokens(b, pieces[0], lens[0]);
			bputc(b, '(');
			for (int i = 1; i < k; i++) {
				if (i > 1)
					bputc(b, ',');
				tokens(b, pieces[i], lens[i]);
			}
			bputc(b, ')');
			return;
		}
	}
	tokens(b, s, n);
}

/* ----- movem masks ----- */

/* The register list of a movem mask (NOT the predecrement order: bit 0 is
 * d0); FP: the list of an fmovem mask (bit 0 is fp7 unless PREDEC, where
 * bit 0 is fp0). */
static void register_list(struct buf *b, unsigned mask, int fp, int predec)
{
	int first = 1;
	int count = fp ? 8 : 16;
	for (int r = 0; r < count;) {
		int bit = fp ? (predec ? r : 7 - r) : (predec ? 15 - r : r);
		if (!(mask & (1u << bit))) {
			r++;
			continue;
		}
		int end = r;
		while (end + 1 < count) {
			int nb = fp ? (predec ? end + 1 : 6 - end) : (predec ? 14 - end : end + 1);
			/* a run stays within the data or the address registers */
			if (!(mask & (1u << nb)) || (!fp && end + 1 == 8))
				break;
			end++;
		}
		if (!first)
			bputc(b, '/');
		first = 0;
		const char *names = fp ? "fp" : r < 8 ? "d" : "a";
		bprintf(b, "%s%d", names, fp ? r : r % 8);
		if (end > r)
			bprintf(b, "-%s%d", names, fp ? end : end % 8);
		r = end + 1;
	}
	if (first)
		error("movem with an empty register mask");
}

/* movem/fmovem operands: a #mask as a register list. */
static void movem_operands(struct buf *b, const char *args, int fp)
{
	const char *pieces[2];
	size_t lens[2];
	int k = split_commas(args, strlen(args), pieces, lens, 2);
	if (k != 2) {
		error("movem with %d operands", k);
		bputs(b, args);
		return;
	}
	int predec = lens[1] > 1 && pieces[1][0] == '-' && pieces[1][1] == '(';
	for (int i = 0; i < 2; i++) {
		if (i)
			bputc(b, ',');
		if (pieces[i][0] == '#') {
			char *end;
			char *text = xstrndup(pieces[i] + 1, lens[i] - 1);
			unsigned long mask = strtoul(text, &end, 0);
			if (*end != '\0')
				error("movem mask %s: not a number", text);
			register_list(b, (unsigned)mask, fp, predec);
			free(text);
		} else {
			operand(b, pieces[i], lens[i]);
		}
	}
}

/* ----- Sections ----- */

enum sect { S_NONE, S_CODE, S_DATA, S_BSS, S_RDATA, S_RBSS, S_SKIP };

static enum sect section_of(const char *name)
{
	size_t n = strcspn(name, ", \t");
	char *s = xstrndup(name, n);
	enum sect r;
	if (strcmp(s, ".text") == 0 || strncmp(s, ".text.", 6) == 0 || strcmp(s, ".rodata") == 0 ||
	    strncmp(s, ".rodata.", 8) == 0)
		r = S_CODE;
	else if (strcmp(s, ".data") == 0 || strncmp(s, ".data.", 6) == 0)
		r = S_DATA;
	else if (strcmp(s, ".bss") == 0 || strncmp(s, ".bss.", 5) == 0)
		r = S_BSS;
	else if (strcmp(s, ".remote.data") == 0)
		r = S_RDATA;
	else if (strcmp(s, ".remote.bss") == 0)
		r = S_RBSS;
	else if (strncmp(s, ".debug", 6) == 0 || strncmp(s, ".note", 5) == 0 || strcmp(s, ".comment") == 0 ||
		 strncmp(s, ".gnu", 4) == 0)
		r = S_SKIP;
	else {
		error("section %s: no r68 equivalent", s);
		r = S_SKIP;
	}
	free(s);
	return r;
}

/* ----- The translation ----- */

static int mode_ucc;		/* -mode=ucc */
static enum sect cur = S_CODE;	/* the section of the lines being read */
static enum sect open_vsect = S_NONE;	/* the vsect being written, if any */
static struct buf out;

/* Switches the output to section S: a vsect for data, closed for the
 * code or another vsect kind. */
static void switch_to(enum sect s)
{
	if (s == S_SKIP)
		return;
	int remote = s == S_RDATA || s == S_RBSS;
	int want = s == S_CODE ? S_NONE : remote ? S_RDATA : S_DATA;
	if (open_vsect != S_NONE && open_vsect != (enum sect)want) {
		bputs(&out, " ends\n");
		open_vsect = S_NONE;
	}
	if (want != S_NONE && open_vsect == S_NONE) {
		bputs(&out, remote ? " vsect remote\n" : " vsect\n");
		open_vsect = (enum sect)want;
	}
}

static int is_bss(void)
{
	return cur == S_BSS || cur == S_RBSS;
}

/* A string of .ascii/.string as dc.b operands: printable characters in
 * "...", with r68's \" and \\, the others as numbers (r68 has no C
 * escapes). Returns the number of bytes. */
static size_t string_bytes(struct buf *b, const char *s, int nul)
{
	size_t count = 0;
	int in = 0, any = 0;
	const char *p = s;
	if (*p != '"') {
		error("string expected: %s", s);
		return 0;
	}
	for (p++; *p && *p != '"'; p++) {
		unsigned c = (unsigned char)*p;
		if (c == '\\') {
			p++;
			switch (*p) {
			case 'n': c = 10; break;
			case 't': c = 9; break;
			case 'r': c = 13; break;
			case 'b': c = 8; break;
			case 'f': c = 12; break;
			case 'v': c = 11; break;
			case 'a': c = 7; break;
			case '\\': c = '\\'; break;
			case '"': c = '"'; break;
			case '\'': c = '\''; break;
			case 'x': {
				char *end;
				c = (unsigned)strtoul(p + 1, &end, 16) & 0xff;
				p = end - 1;
				break;
			}
			default:
				if (*p >= '0' && *p <= '7') {
					c = 0;
					for (int k = 0; k < 3 && *p >= '0' && *p <= '7'; k++, p++)
						c = c * 8 + (*p - '0');
					p--;
				} else {
					c = (unsigned char)*p;
				}
			}
		}
		count++;
		if (c >= 32 && c < 127) {
			if (!in) {
				if (any)
					bputc(b, ',');
				bputc(b, '"');
				in = 1;
			}
			if (c == '"' || c == '\\')
				bputc(b, '\\');
			bputc(b, (char)c);
		} else {
			if (in) {
				bputc(b, '"');
				in = 0;
			}
			if (any)
				bputc(b, ',');
			bprintf(b, "%u", c);
		}
		any = 1;
	}
	if (in)
		bputc(b, '"');
	if (nul) {
		if (any)
			bputc(b, ',');
		bputc(b, '0');
		count++;
	}
	if (count == 0)
		error("empty string");
	return count;
}

/* The j<cc> mnemonics of GCC (j<cc> sized by GNU as) as r68's b<cc>.w. */
static const char *branch(const char *op)
{
	static char r[40];
	if (strcmp(op, "jbsr") == 0)
		return "bsr.w";
	if (strcmp(op, "jra") == 0 || strcmp(op, "jbra") == 0)
		return "bra.w";
	static const char *const cc[] = { "hi", "ls", "cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi", "ge", "lt",
		"gt", "le", "hs", "lo", NULL };
	const char *c = op[0] == 'j' ? op + 1 : (op[0] == 'f' && op[1] == 'j') ? op + 2 : NULL;
	if (c == NULL)
		return NULL;
	if (op[0] == 'f') {
		/* fj<cc>: the 68881's conditions */
		if (strlen(c) < 2 || strlen(c) > 4)
			return NULL;
		snprintf(r, sizeof r, "fb%s.w", c);
		return r;
	}
	for (const char *const *p = cc; *p; p++)
		if (strcmp(c, *p) == 0) {
			snprintf(r, sizeof r, "b%s.w", c);
			return r;
		}
	return NULL;
}

/* Translates the instruction OP ARGS. */
static void instruction(const char *label, const char *op, const char *args)
{
	char lower[32];
	size_t i;
	for (i = 0; op[i] && i + 1 < sizeof lower; i++)
		lower[i] = tolower((unsigned char)op[i]);
	lower[i] = '\0';
	const char *b = branch(lower);
	if (cur != S_CODE)
		error("instruction %s outside the code", op);
	bprintf(&out, "%s %s", label, b != NULL ? b : op);
	if (*args) {
		bputc(&out, ' ');
		if (strncmp(lower, "movem", 5) == 0)
			movem_operands(&out, args, 0);
		else if (strncmp(lower, "fmovem", 6) == 0)
			movem_operands(&out, args, 1);
		else {
			const char *pieces[8];
			size_t lens[8];
			int k = split_commas(args, strlen(args), pieces, lens, 8);
			for (int j = 0; j < k; j++) {
				if (j)
					bputc(&out, ',');
				operand(&out, pieces[j], lens[j]);
			}
		}
	}
	bputc(&out, '\n');
}

/* The label of a line, as r68 writes it: global with ':', column 1. */
static void label_text(struct buf *b, const char *name, size_t n)
{
	put_symbol(b, name, n);
	if (has(&globals, name, n))
		bputc(b, ':');
}

static struct names locals;	/* .local, for .comm */

/* A directive; LABEL is the line's label (r68 form), if any. */
static void directive(const char *label, const char *op, const char *args)
{
	static enum sect stack[16];
	static int depth;
	if (strcmp(op, ".pushsection") == 0) {
		if (depth < 16)
			stack[depth++] = cur;
		cur = section_of(args);
		switch_to(cur);
		return;
	}
	if (strcmp(op, ".popsection") == 0) {
		if (depth == 0) {
			error(".popsection without .pushsection");
			return;
		}
		cur = stack[--depth];
		switch_to(cur);
		return;
	}
	if (strcmp(op, ".section") == 0 || strcmp(op, ".text") == 0 || strcmp(op, ".data") == 0 ||
	    strcmp(op, ".bss") == 0) {
		cur = strcmp(op, ".section") == 0 ? section_of(args) : section_of(op);
		switch_to(cur);
		return;
	}
	if (strcmp(op, ".globl") == 0 || strcmp(op, ".global") == 0 || strcmp(op, ".type") == 0 ||
	    strcmp(op, ".size") == 0 || strcmp(op, ".ident") == 0 || strcmp(op, ".file") == 0 ||
	    strcmp(op, ".loc") == 0 || strncmp(op, ".cfi_", 5) == 0 || strcmp(op, ".swbeg") == 0 ||
	    strcmp(op, ".local") == 0 || strcmp(op, ".hidden") == 0) {
		if (*label)
			bprintf(&out, "%s\n", label);
		return;
	}
	if (cur == S_SKIP)
		return;
	if (strcmp(op, ".set") == 0 || strcmp(op, ".equ") == 0) {
		const char *pieces[2];
		size_t lens[2];
		if (split_commas(args, strlen(args), pieces, lens, 2) != 2) {
			error("%s without a value", op);
			return;
		}
		struct buf l = { 0 };
		label_text(&l, pieces[0], lens[0]);
		bprintf(&out, "%s equ ", l.s);
		tokens(&out, pieces[1], lens[1]);
		bputc(&out, '\n');
		free(l.s);
		return;
	}
	if (strcmp(op, ".byte") == 0 || strcmp(op, ".word") == 0 || strcmp(op, ".short") == 0 ||
	    strcmp(op, ".long") == 0 || strcmp(op, ".int") == 0 || strcmp(op, ".2byte") == 0 ||
	    strcmp(op, ".4byte") == 0) {
		char size = op[1] == 'b' ? 'b' : (op[1] == 'w' || op[1] == 's' || op[1] == '2') ? 'w' : 'l';
		if (is_bss())
			error("%s in uninitialized data", op);
		bprintf(&out, "%s dc.%c ", label, size);
		tokens(&out, args, strlen(args));
		bputc(&out, '\n');
		return;
	}
	if (strcmp(op, ".ascii") == 0 || strcmp(op, ".string") == 0 || strcmp(op, ".asciz") == 0) {
		bprintf(&out, "%s dc.b ", label);
		const char *pieces[64];
		size_t lens[64];
		int k = split_commas(args, strlen(args), pieces, lens, 64);
		for (int j = 0; j < k; j++) {
			if (j)
				bputc(&out, ',');
			char *one = xstrndup(pieces[j], lens[j]);
			string_bytes(&out, one, strcmp(op, ".ascii") != 0);
			free(one);
		}
		bputc(&out, '\n');
		return;
	}
	if (strcmp(op, ".zero") == 0 || strcmp(op, ".skip") == 0 || strcmp(op, ".space") == 0) {
		/* ds only in uninitialized data: elsewhere it would move to
		 * the uninitialized counter (and is illegal in the psect). */
		if (strchr(args, ',') != NULL)
			error("%s with a fill value", op);
		bprintf(&out, "%s %s ", label, is_bss() ? "ds.b" : "dz.b");
		tokens(&out, args, strlen(args));
		bputc(&out, '\n');
		return;
	}
	if (strcmp(op, ".align") == 0 || strcmp(op, ".balign") == 0 || strcmp(op, ".balignw") == 0 ||
	    strcmp(op, ".even") == 0) {
		/* m68k's .align counts bytes. C 3.2's r68 aligns to even only. */
		long n = strcmp(op, ".even") == 0 ? 2 : strtol(args, NULL, 0);
		if (*label)
			bprintf(&out, "%s\n", label);
		if (n <= 2 || !mode_ucc)
			bputs(&out, " align\n");
		else
			bprintf(&out, " align %ld\n", n);
		return;
	}
	if (strcmp(op, ".comm") == 0 || strcmp(op, ".lcomm") == 0) {
		/* .local + .comm (a function's static): a vsect ds, not
		 * global; r68's com is an overlaid common block. */
		const char *pieces[3];
		size_t lens[3];
		int k = split_commas(args, strlen(args), pieces, lens, 3);
		if (k < 2) {
			error("%s without a size", op);
			return;
		}
		if (strcmp(op, ".comm") == 0 && !has(&locals, pieces[0], lens[0])) {
			error(".comm %.*s: a common symbol (not .local), which r68 can't make", (int)lens[0], pieces[0]);
			return;
		}
		enum sect was = cur;
		switch_to(S_BSS);
		if (k == 3 && strtol(pieces[2], NULL, 0) > 1)
			bputs(&out, " align\n");
		put_symbol(&out, pieces[0], lens[0]);
		bputs(&out, " ds.b ");
		tokens(&out, pieces[1], lens[1]);
		bputc(&out, '\n');
		switch_to(was);
		return;
	}
	if (strcmp(op, ".weak") == 0 || strcmp(op, ".macro") == 0 || strcmp(op, ".rept") == 0 || strcmp(op, ".irp") == 0 ||
	    strcmp(op, ".uleb128") == 0 || strcmp(op, ".sleb128") == 0) {
		error("%s: no r68 equivalent", op);
		return;
	}
	error("directive %s: not translated", op);
}

/* A label read alone in a vsect, for the next data line. */
static char *pending;

static void flush_pending(void)
{
	if (pending != NULL)
		bprintf(&out, "%s\n", pending);
	free(pending);
	pending = NULL;
}

static int data_directive(const char *op)
{
	static const char *const ops[] = { ".byte", ".word", ".short", ".long", ".int", ".2byte", ".4byte", ".ascii",
		".string", ".asciz", ".zero", ".skip", ".space", NULL };
	for (const char *const *o = ops; *o; o++)
		if (strcmp(op, *o) == 0)
			return 1;
	return 0;
}

static int section_directive(const char *op)
{
	return strcmp(op, ".section") == 0 || strcmp(op, ".text") == 0 || strcmp(op, ".data") == 0 ||
		strcmp(op, ".bss") == 0 || strcmp(op, ".pushsection") == 0 || strcmp(op, ".popsection") == 0 ||
		strcmp(op, ".comm") == 0 || strcmp(op, ".lcomm") == 0 || strcmp(op, ".align") == 0 ||
		strcmp(op, ".balign") == 0 || strcmp(op, ".balignw") == 0 || strcmp(op, ".even") == 0;
}

/* One input line. */
static void translate_line(char *line)
{
	/* Comments: GCC's "|" (outside strings), #APP/#NO_APP. */
	if (line[0] == '#') {
		bprintf(&out, "*%s\n", line + 1);
		return;
	}
	char *comment = NULL;
	char quote = 0;
	for (char *p = line; *p; p++) {
		if (quote) {
			if (*p == '\\' && p[1])
				p++;
			else if (*p == quote)
				quote = 0;
		} else if (*p == '"') {
			quote = '"';
		} else if (*p == '|') {
			comment = p + 1;
			*p = '\0';
			break;
		}
	}
	/* The label */
	char *p = line;
	struct buf label = { 0 };
	while (*p == ' ' || *p == '\t')
		p++;
	char *q = p;
	while (*q && symchar((unsigned char)*q))
		q++;
	int numeric = q > p && q - p <= 2 && isdigit((unsigned char)p[0]) && (q - p == 1 || isdigit((unsigned char)p[1]));
	if (numeric && *q == ':') {
		int num = atoi(p);
		bprintf(&label, "g%d.%d", num, ++local_count[num]);
		p = q + 1;
		while (*p == ' ' || *p == '\t')
			p++;
	} else if (q > p && *q == ':') {
		label_text(&label, p, q - p);
		p = q + 1;
		while (*p == ' ' || *p == '\t')
			p++;
	}
	const char *lab = label.s ? label.s : "";
	/* The operator and its operands */
	q = p;
	while (*q && !isspace((unsigned char)*q))
		q++;
	char *op = xstrndup(p, q - p);
	while (*q && isspace((unsigned char)*q))
		q++;
	char *args = xstrdup(q);
	for (size_t n = strlen(args); n > 0 && isspace((unsigned char)args[n - 1]);)
		args[--n] = '\0';
	if (*op == '\0') {
		if (*lab && cur != S_SKIP) {
			/* In a vsect, a label alone takes the initialized data's
			 * counter in r68: it goes onto the next data line. */
			flush_pending();
			if (cur == S_CODE)
				bprintf(&out, "%s\n", lab);
			else
				pending = xstrdup(lab);
		} else if (comment != NULL) {
			bprintf(&out, "*%s\n", comment);
		}
	} else if (op[0] == '.') {
		if (*lab || !data_directive(op)) {
			if (*lab || section_directive(op))
				flush_pending();
			directive(lab, op, args);
		} else {
			char *p2 = pending;
			pending = NULL;
			directive(p2 != NULL ? p2 : "", op, args);
			free(p2);
		}
	} else if (cur != S_SKIP) {
		char *p2 = *lab ? NULL : pending;
		if (*lab)
			flush_pending();
		pending = NULL;
		instruction(p2 != NULL ? p2 : lab, op, args);
		free(p2);
	}
	free(op);
	free(args);
	free(label.s);
}

/* ----- The pre-pass ----- */

static void prepass(char **lines, int n)
{
	for (int i = 0; i < n; i++) {
		char *s = lines[i];
		while (*s == ' ' || *s == '\t')
			s++;
		if (strncmp(s, ".globl", 6) == 0 || strncmp(s, ".global", 7) == 0 || strncmp(s, ".local", 6) == 0) {
			int local = s[1] == 'l';
			s += strcspn(s, " \t");
			while (*s) {
				while (*s == ' ' || *s == '\t' || *s == ',')
					s++;
				size_t k = 0;
				while (s[k] && symchar((unsigned char)s[k]))
					k++;
				if (k == 0)
					break;
				add(local ? &locals : &globals, s, k);
				s += k;
			}
		} else if (strncmp(s, ".file", 5) == 0 && file_name == NULL) {
			char *a = strchr(s, '"'), *b = a ? strchr(a + 1, '"') : NULL;
			if (a && b)
				file_name = xstrndup(a + 1, b - a - 1);
		} else if (strncmp(s, ".set", 4) == 0 || strncmp(s, ".equ", 4) == 0 || strncmp(s, "__os9_", 6) == 0) {
			if (*s == '.')
				s += 4;
			while (*s == ' ' || *s == '\t')
				s++;
			for (int h = 0; h < 6; h++) {
				size_t k = strlen(header_names[h]);
				if (strncmp(s, header_names[h], k) == 0 && !symchar((unsigned char)s[k])) {
					char *v = s + k + strspn(s + k, " \t,=");
					size_t vn = strcspn(v, "|");
					while (vn > 0 && isspace((unsigned char)v[vn - 1]))
						vn--;
					free(header[h]);
					header[h] = xstrndup(v, vn);
				}
			}
		}
	}
}

/* ----- main ----- */

static void usage(void)
{
	printf("usage: %s [-mode=c32|ucc] [-n=<psect>] [-o <output>] <source>\n", progname);
	printf("  Translates GCC's GNU as output (m68k-elfos9-gcc -S) to a Microware r68\n");
	printf("  source; see gastor68.md.\n");
	printf("  -mode=  the style of the psect line: C 3.2's (default) or Ultra C's\n");
	printf("  -n=     the psect's name (default: from .file, else the source's name)\n");
	printf("  -o      the output (default: the source with .s as .a)\n");
}

int main(int argc, char **argv)
{
	const char *source = NULL, *output = NULL, *psect = NULL;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (strcmp(a, "-o") == 0 && i + 1 < argc)
			output = argv[++i];
		else if (strncmp(a, "-o=", 3) == 0)
			output = a + 3;
		else if (strncmp(a, "-n=", 3) == 0)
			psect = a + 3;
		else if (strcmp(a, "-mode=c32") == 0)
			mode_ucc = 0;
		else if (strcmp(a, "-mode=ucc") == 0)
			mode_ucc = 1;
		else if (strcmp(a, "-?") == 0 || strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			usage();
			return 0;
		} else if (a[0] == '-' && a[1])
			die("unknown option %s", a);
		else
			source = a;
	}
	if (source == NULL) {
		usage();
		return 1;
	}
	input_name = source;
	char *made = NULL;
	int explicit_output = output != NULL;
	if (output == NULL) {
		size_t n = strlen(source);
		made = xmalloc(n + 3);
		if (n > 2 && strcmp(source + n - 2, ".s") == 0)
			sprintf(made, "%.*s.a", (int)(n - 2), source);
		else
			sprintf(made, "%s.a", source);
		output = made;
	}
	if (strcmp(output, "-") != 0 && strcmp(output, source) == 0)
		die("%s: won't write over the source", output);
	if (!explicit_output) {
		FILE *t = fopen(output, "rb");
		if (t != NULL) {
			fclose(t);
			die("%s exists: name it with -o to write over it", output);
		}
	}

	FILE *fp = fopen(source, "rb");
	if (fp == NULL)
		die("cannot read %s", source);
	struct buf text = { 0 };
	char chunk[8192];
	size_t n;
	while ((n = fread(chunk, 1, sizeof chunk, fp)) > 0)
		bput(&text, chunk, n);
	fclose(fp);
	char **lines = NULL;
	int nlines = 0;
	for (char *s = text.s; s != NULL && *s;) {
		char *e = s + strcspn(s, "\r\n");
		lines = xrealloc(lines, (nlines + 1) * sizeof *lines);
		lines[nlines++] = xstrndup(s, e - s);
		s = e + (*e == '\r' && e[1] == '\n' ? 2 : *e ? 1 : 0);
	}
	prepass(lines, nlines);

	/* The psect: its name from -n=, .file or the source; '.' as '_' in
	 * C 3.2's style (psect testos9c_c), kept in Ultra C's. */
	char *name;
	if (psect != NULL)
		name = xstrdup(psect);
	else {
		const char *base = file_name != NULL ? file_name : source;
		const char *slash = strrchr(base, '/');
		name = xstrdup(slash ? slash + 1 : base);
		if (file_name == NULL) {
			size_t k = strlen(name);
			if (k > 2 && strcmp(name + k - 2, ".s") == 0)
				name[k - 2] = '\0';
		}
	}
	if (!mode_ucc)
		for (char *c = name; *c; c++)
			if (!isalnum((unsigned char)*c) && *c != '_' && *c != '$' && *c != '@')
				*c = '_';
	bprintf(&out, " psect %s", name);
	for (int h = 0; h < 5; h++) {
		bputc(&out, ',');
		if (header[h] != NULL)
			tokens(&out, header[h], strlen(header[h]));
		else
			bputc(&out, '0');
	}
	if (header[5] != NULL) {
		bputc(&out, ',');
		tokens(&out, header[5], strlen(header[5]));
	}
	bputc(&out, '\n');
	if (!mode_ucc)
		bprintf(&out, " nam %s\n", name);

	for (lineno = 1; lineno <= nlines; lineno++)
		translate_line(lines[lineno - 1]);
	flush_pending();
	switch_to(S_CODE);
	bputs(&out, " ends\n");
	if (errors)
		return 1;

	FILE *o = strcmp(output, "-") == 0 ? stdout : fopen(output, "w");
	if (o == NULL)
		die("cannot write %s", output);
	fputs(out.s, o);
	if (o != stdout && fclose(o) != 0)
		die("cannot write %s", output);
	return 0;
}

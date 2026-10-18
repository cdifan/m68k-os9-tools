/* gastor68: GCC's gas output to Microware r68 sources.
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
		/* To gas, %fp is a6 (the frame pointer). */
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
				/* %d0:w is d0.w, %d0:l:4 d0.l*4 (before tokens(), which
				 * drops a :w as a displacement's size; r68 takes an
				 * index without a size as .l) */
				char *x = xstrndup(pieces[i], lens[i]);
				int colons = 0;
				for (char *c = x; *c; c++)
					if (*c == ':')
						*c = colons++ ? '*' : '.';
				tokens(b, x, strlen(x));
				free(x);
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
				/* With an index, a number of 16 bits gets r68's .w, as
				 * gas sizes it (r68 makes it 32 bits otherwise). */
				int word = k >= 3 && v >= -32768 && v <= 32767;
				bputc(b, '(');
				for (int i = 0; i < k; i++) {
					if (i)
						bputc(b, ',');
					tokens(b, pieces[i], lens[i]);
					if (i == 0 && word)
						bputs(b, ".w");
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
static struct buf out;		/* the lines being written: the code's or the vsects' */
static struct buf other;	/* the other of the two */
static int in_vsects;		/* out has the vsects' lines */
static struct buf comms;	/* .local + .comm, for the end */

/* Switches the output to section S. The vsects are written apart and go
 * after the code, at the end of the psect: in the code a vsect after an odd
 * dc.b (GCC's string, then a static's data) would make the translation for
 * gas (MRI mode) align the code there, which r68 doesn't. A vsect for data,
 * closed for another vsect kind; open while the code is written. */
static void switch_to(enum sect s)
{
	if (s == S_SKIP)
		return;
	int remote = s == S_RDATA || s == S_RBSS;
	int want = s == S_CODE ? S_NONE : remote ? S_RDATA : S_DATA;
	if ((want != S_NONE) != in_vsects) {
		struct buf t = out;
		out = other;
		other = t;
		in_vsects = !in_vsects;
	}
	if (want == S_NONE)
		return;
	if (open_vsect != S_NONE && open_vsect != (enum sect)want) {
		bputs(&out, " ends\n");
		open_vsect = S_NONE;
	}
	if (open_vsect == S_NONE) {
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

/* The j<cc> mnemonics of GCC (j<cc> sized by gas) as r68's b<cc>.w. */
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
		/* Kept for the end of the file, where gas puts them (after
		 * all of .bss: its subsection 1), so that the layout is
		 * GCC's. */
		if (k == 3 && strtol(pieces[2], NULL, 0) > 1)
			bputs(&comms, " align\n");
		put_symbol(&comms, pieces[0], lens[0]);
		bputs(&comms, " ds.b ");
		tokens(&comms, pieces[1], lens[1]);
		bputc(&comms, '\n');
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

/* ----- r68togas's output: the source back ----- */

/* Input that r68togas made (it starts with its prelude's linemarker) is
 * given back as the r68 source it was made from: the prelude, the epilogue
 * and the lines r68togas adds (its own names: r68__..., .Lr68..., markers,
 * relocations) dropped, an inlined use file as its use statement, a moved
 * macro definition in its place, a statement noted ;[r68: ...] as noted,
 * the rest through the inverse of r68togas's rewrites: names, operands,
 * expressions (in r68's precedence, parentheses only where it needs them),
 * strings, its prelude's macros. See gastor68.md, "r68togas's output". */

static int is_r68togas(char **lines, int n)
{
	return n > 0 && strcmp(lines[0], "# 1 \"r68.prelude\"") == 0;
}

static int starts(const char *s, const char *t)
{
	return strncmp(s, t, strlen(t)) == 0;
}

/* r68togas renamed an @ (its line r68__at set 1): _at_ is one; else a
 * name's own (not_at_top) */
static int at_names;

/* A name as the source wrote it, from r68togas's (put_symbol, its twins
 * NAME__r68pc, NAME__final, NAME__p1, a renamed macro NAME__r68m<n>, and
 * its own names for r68's \#, \Ln, '\n' and the offset counter). */
static void unname(struct buf *b, const char *s, size_t n)
{
	if (n == 8 && strncmp(s, "r68__ofs", 8) == 0) {
		bputc(b, '.');
		return;
	}
	if (n == 8 && strncmp(s, "r68__n\\@", 8) == 0) {
		bputs(b, "\\#");
		return;
	}
	if (n == 7 && strncmp(s, "r68__L", 6) == 0 && isdigit((unsigned char)s[6])) {
		bprintf(b, "\\L%c", s[6]);
		return;
	}
	if (n > 6 && strncmp(s, ".Lr68c", 6) == 0) {
		const char *u = memchr(s, '_', n);
		if (u != NULL && u + 2 == s + n) {
			bprintf(b, "'\\%c'", u[1]);
			return;
		}
	}
	size_t k = n;
	for (;;) {
		if (k > 7 && strncmp(s + k - 7, "__r68pc", 7) == 0)
			k -= 7;
		else if (k > 7 && strncmp(s + k - 7, "__final", 7) == 0)
			k -= 7;
		else if (k > 4 && strncmp(s + k - 4, "__p1", 4) == 0)
			k -= 4;
		else
			break;
	}
	size_t j = k;
	while (j > 0 && isdigit((unsigned char)s[j - 1]))
		j--;
	if (j < k && j > 6 && strncmp(s + j - 6, "__r68m", 6) == 0)
		k = j - 6;
	else if (k > 5 && strncmp(s + k - 5, "__r68", 5) == 0)
		k -= 5;
	for (size_t i = 0; i < k;) {
		if (i + 7 <= k && strncmp(s + i, "__dot__", 7) == 0) {
			bputc(b, '.');
			i += 7;
		} else if (at_names && i + 4 <= k && strncmp(s + i, "_at_", 4) == 0) {
			bputc(b, '@');
			i += 4;
		} else {
			bputc(b, s[i++]);
		}
	}
}

/* -- Expressions -- */

struct ex {
	char kind;		/* 'l' a leaf, 'u' unary, 'b' binary */
	char op[3];
	char *text;		/* a leaf's */
	struct ex *a, *b;
};

static struct ex *ex_new(char kind, const char *op, struct ex *a, struct ex *b)
{
	struct ex *e = xmalloc(sizeof *e);
	memset(e, 0, sizeof *e);
	e->kind = kind;
	snprintf(e->op, sizeof e->op, "%s", op);
	e->a = a;
	e->b = b;
	return e;
}

static void ex_free(struct ex *e)
{
	if (e == NULL)
		return;
	ex_free(e->a);
	ex_free(e->b);
	free(e->text);
	free(e);
}

/* r68's precedence, lowest first */
static int ex_prec(const char *op)
{
	if (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0)
		return 1;
	if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0)
		return 2;
	if (strcmp(op, "*") == 0 || strcmp(op, "/") == 0)
		return 3;
	return 4;	/* & ! | */
}

static int ex_symchar(int c)
{
	return isalnum(c) || c == '_' || c == '.' || c == '$' || c == '@';
}

static struct ex *ex_binary(const char **p, int prec);

static struct ex *ex_unary(const char **p)
{
	const char *s = *p;
	if (*s == '-' || *s == '~' || *s == '^' || *s == '+') {
		char op[2] = { *s, 0 };
		*p = s + 1;
		struct ex *a = ex_unary(p);
		if (a == NULL)
			return NULL;
		return ex_new('u', op, a, NULL);
	}
	if (*s == '(') {
		*p = s + 1;
		struct ex *e = ex_binary(p, 1);
		if (e == NULL || **p != ')') {
			ex_free(e);
			return NULL;
		}
		(*p)++;
		return e;
	}
	const char *q = s;
	if (*s == '$' && isxdigit((unsigned char)s[1])) {
		for (q = s + 1; isxdigit((unsigned char)*q); q++)
			;
	} else if (*s == '%' && (s[1] == '0' || s[1] == '1')) {
		for (q = s + 1; *q == '0' || *q == '1'; q++)
			;
	} else if (*s == '%' && isalpha((unsigned char)s[1])) {
		/* a register with r68's % (-r) */
		for (q = s + 1; isalnum((unsigned char)*q); q++)
			;
	} else if (isdigit((unsigned char)*s)) {
		for (q = s; isdigit((unsigned char)*q); q++)
			;
		if (*q == '.' && isdigit((unsigned char)q[1]))
			for (q++; isdigit((unsigned char)*q); q++)
				;
		if ((*q == 'e' || *q == 'E') && (isdigit((unsigned char)q[1]) || ((q[1] == '-' || q[1] == '+') && isdigit((unsigned char)q[2]))))
			for (q += 2; isdigit((unsigned char)*q); q++)
				;
	} else if (*s == '\'') {
		/* a character constant (MRI: '' a quote) */
		for (q = s + 1; *q; q++) {
			if (*q == '\'') {
				if (q[1] == '\'')
					q++;
				else
					break;
			}
		}
		if (*q != '\'')
			return NULL;
		q++;
	} else if (*s == '*') {
		q = s + 1;
	} else if (isalpha((unsigned char)*s) || *s == '_' || *s == '.' || *s == '@' || *s == '\\') {
		/* a name, with a macro's \n or \@ in it */
		while (*q) {
			if (*q == '\\' && q[1] != '\0')
				q += 2;
			else if (ex_symchar((unsigned char)*q))
				q++;
			else
				break;
		}
	}
	if (q == s)
		return NULL;
	struct ex *e = ex_new('l', "", NULL, NULL);
	e->text = xstrndup(s, q - s);
	*p = q;
	return e;
}

static const char *ex_op(const char *s, int *len)
{
	static char op[3];
	*len = 0;
	if ((s[0] == '<' && s[1] == '<') || (s[0] == '>' && s[1] == '>')) {
		op[0] = s[0], op[1] = s[1], op[2] = 0;
		*len = 2;
		return op;
	}
	if (s[0] && strchr("+-*/&!|", s[0])) {
		op[0] = s[0], op[1] = 0;
		*len = 1;
		return op;
	}
	return NULL;
}

static struct ex *ex_binary(const char **p, int prec)
{
	if (prec > 4)
		return ex_unary(p);
	struct ex *l = ex_binary(p, prec + 1);
	if (l == NULL)
		return NULL;
	for (;;) {
		int len;
		const char *op = ex_op(*p, &len);
		if (op == NULL || ex_prec(op) != prec)
			return l;
		char o[3];
		snprintf(o, sizeof o, "%s", op);
		*p += len;
		struct ex *r = ex_binary(p, prec + 1);
		if (r == NULL) {
			ex_free(l);
			return NULL;
		}
		l = ex_new('b', o, l, r);
	}
}

static int ex_is(const struct ex *e, const char *text)
{
	return e != NULL && e->kind == 'l' && strcasecmp(e->text, text) == 0;
}

/* r68togas's 32-bit sign extension of L: ((L+$80000000)&$ffffffff)-$80000000 */
static struct ex *ex_sx(struct ex *e)
{
	if (e == NULL || e->kind != 'b' || strcmp(e->op, "-") != 0 || !ex_is(e->b, "$80000000"))
		return NULL;
	struct ex *m = e->a;
	if (m->kind != 'b' || strcmp(m->op, "&") != 0 || !ex_is(m->b, "$ffffffff"))
		return NULL;
	struct ex *s = m->a;
	if (s->kind != 'b' || strcmp(s->op, "+") != 0 || !ex_is(s->b, "$80000000"))
		return NULL;
	return s->a;
}

/* The tree with r68togas's rewrites undone (in place where it can). */
static struct ex *ex_undo(struct ex *e)
{
	if (e == NULL)
		return NULL;
	e->a = ex_undo(e->a);
	e->b = ex_undo(e->b);
	if (e->kind == 'l') {
		if (isalpha((unsigned char)e->text[0]) || e->text[0] == '_' || e->text[0] == '.') {
			struct buf b = { 0 };
			unname(&b, e->text, strlen(e->text));
			free(e->text);
			e->text = b.s;
		}
		return e;
	}
	if (e->kind == 'u' && e->op[0] == '~')
		e->op[0] = '^';
	if (e->kind != 'b')
		return e;
	/* .-LABEL of a vsect label: S+(.-(LABEL-S)), S its section's start */
	if (strcmp(e->op, "+") == 0 && e->a->kind == 'l' && (strcmp(e->a->text, "r68__bbss") == 0 ||
	    strcmp(e->a->text, "r68__bdata") == 0) && e->b->kind == 'b' && strcmp(e->b->op, "-") == 0 &&
	    ex_is(e->b->a, ".") && e->b->b->kind == 'b' && strcmp(e->b->b->op, "-") == 0 &&
	    e->b->b->b->kind == 'l' && strcmp(e->b->b->b->text, e->a->text) == 0) {
		struct ex *r = ex_new('b', "-", e->b->a, e->b->b->a);
		e->b->a = NULL;
		e->b->b->a = NULL;
		ex_free(e);
		return r;
	}
	/* a shift count modulo 32 (Ultra C's r68): (N)&31 */
	if ((strcmp(e->op, "<<") == 0 || strcmp(e->op, ">>") == 0) && e->b->kind == 'b' &&
	    strcmp(e->b->op, "&") == 0 && ex_is(e->b->b, "31")) {
		struct ex *n = e->b->a;
		e->b->a = NULL;
		ex_free(e->b);
		e->b = n;
	}
	/* r68's arithmetic >>: (X-(X&M))/D, X the sign extension of L */
	if (strcmp(e->op, "/") == 0 && e->a->kind == 'b' && strcmp(e->a->op, "-") == 0 && e->a->b->kind == 'b' &&
	    strcmp(e->a->b->op, "&") == 0) {
		struct ex *l = ex_sx(e->a->a);
		if (l != NULL && ex_sx(e->a->b->a) != NULL) {
			struct ex *count = NULL;
			if (e->b->kind == 'l' && isdigit((unsigned char)e->b->text[0])) {
				unsigned long d = strtoul(e->b->text, NULL, 10);
				int k = 0;
				while (k < 32 && (1ul << k) != d)
					k++;
				if (k < 32) {
					count = ex_new('l', "", NULL, NULL);
					count->text = xmalloc(4);
					sprintf(count->text, "%d", k);
				}
			} else if (e->b->kind == 'b' && strcmp(e->b->op, "<<") == 0 && ex_is(e->b->a, "1")) {
				count = e->b->b;
				e->b->b = NULL;
			}
			if (count != NULL) {
				/* detach L from the tree before freeing it */
				struct ex *s = e->a->a->a->a;
				s->a = NULL;
				struct ex *r = ex_new('b', ">>", l, count);
				ex_free(e);
				return r;
			}
		}
	}
	return e;
}

static void ex_print(struct buf *b, const struct ex *e, int prec, int right)
{
	if (e->kind == 'l') {
		bputs(b, e->text);
		return;
	}
	if (e->kind == 'u') {
		bputs(b, e->op);
		int par = e->a->kind == 'b';
		if (par)
			bputc(b, '(');
		ex_print(b, e->a, 0, 0);
		if (par)
			bputc(b, ')');
		return;
	}
	int p = ex_prec(e->op);
	int par = p < prec || (p == prec && right);
	if (par)
		bputc(b, '(');
	ex_print(b, e->a, p, 0);
	bputs(b, e->op);
	ex_print(b, e->b, p, 1);
	if (par)
		bputc(b, ')');
}

/* Name characters of a gas line's text as the source's, the rest as it is
 * (what isn't an expression). */
static void unname_text(struct buf *b, const char *s, size_t n)
{
	for (size_t i = 0; i < n;) {
		if (s[i] == '\'') {
			size_t j = i + 1;
			while (j < n && s[j] != '\'')
				j++;
			j = j < n ? j + 1 : n;
			bput(b, s + i, j - i);
			i = j;
		} else if ((isalpha((unsigned char)s[i]) || s[i] == '_' || s[i] == '.') && (i == 0 || !ex_symchar((unsigned char)s[i - 1]))) {
			size_t j = i;
			while (j < n && (ex_symchar((unsigned char)s[j]) || (s[j] == '\\' && j + 1 < n && s[j + 1] == '@')))
				j += s[j] == '\\' ? 2 : 1;
			unname(b, s + i, j - i);
			i = j;
		} else {
			bputc(b, s[i++]);
		}
	}
}

/* Expression text S (N chars) of r68togas's as the source's. */
static void undo_expr(struct buf *b, const char *s, size_t n)
{
	char *t = xstrndup(s, n);
	const char *p = t;
	struct ex *e = ex_binary(&p, 1);
	if (e != NULL && *p == '\0') {
		e = ex_undo(e);
		ex_print(b, e, 0, 0);
	} else {
		unname_text(b, s, n);
	}
	ex_free(e);
	free(t);
}

/* -- Operands -- */

/* the epilogue's movem masks: "NAME equ ...   ORIGINAL" */
static struct names mask_names, mask_texts;

/* Splits S (N chars) at commas outside parentheses, MRI quotes and <...>. */
static int split_args(const char *s, size_t n, const char **piece, size_t *len, int max)
{
	int count = 0, depth = 0, angle = 0;
	int quote = 0;
	size_t start = 0;
	for (size_t i = 0; i <= n; i++) {
		char c = i < n ? s[i] : ',';
		if (quote && i < n) {
			if (c == '\'')
				quote = 0;
			continue;
		}
		if (angle && i < n) {
			if (c == '!' && i + 1 < n)
				i++;
			else if (c == '<')
				angle++;
			else if (c == '>')
				angle--;
			continue;
		}
		if (c == '\'' && i < n)
			quote = 1;
		else if (c == '<' && i < n && (i == start))
			angle = 1;
		else if (c == '(')
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

/* The matching ')' of the '(' at S[I] in S (N chars), or N. */
static size_t close_paren(const char *s, size_t n, size_t i)
{
	int depth = 0;
	for (; i < n; i++) {
		if (s[i] == '(')
			depth++;
		else if (s[i] == ')' && --depth == 0)
			return i;
	}
	return n;
}

static int is_regtext(const char *s, size_t n)
{
	static const char *const r[] = { "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a0", "a1", "a2", "a3", "a4",
		"a5", "a6", "a7", "sp", "pc", "zpc", NULL };
	if (n > 0 && s[0] == '%')
		s++, n--;
	size_t k = 0;
	while (k < n && isalnum((unsigned char)s[k]))
		k++;
	int ok = 0;
	for (const char *const *p = r; *p && !ok; p++)
		ok = strlen(*p) == k && strncasecmp(*p, s, k) == 0;
	if (!ok && k == 2 && s[0] == '\\' )
		ok = 1;
	return ok;
}

/* r68togas -r (r68's: registers only with %): its bare register names as
 * %d0 (a symbol named so is NAME__r68 there) */
static int regs_percent;

static char *percent_regs(const char *s, size_t n)
{
	static const char *const regs[] = { "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a0", "a1", "a2", "a3", "a4",
		"a5", "a6", "a7", "sp", "pc", "usp", "sr", "ccr", "vbr", "sfc", "dfc", "cacr", "caar", "msp", "isp", "fp0",
		"fp1", "fp2", "fp3", "fp4", "fp5", "fp6", "fp7", "fpcr", "fpsr", "fpiar", "zpc", NULL };
	struct buf b = { 0 };
	bputs(&b, "");
	for (size_t i = 0; i < n;) {
		if (s[i] == '\'') {
			size_t j = i + 1;
			while (j < n && s[j] != '\'')
				j++;
			j = j < n ? j + 1 : n;
			bput(&b, s + i, j - i);
			i = j;
			continue;
		}
		if (isalpha((unsigned char)s[i]) && (i == 0 || !(ex_symchar((unsigned char)s[i - 1]) || s[i - 1] == '\\' || s[i - 1] == '%'))) {
			size_t j = i;
			while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '_'))
				j++;
			int reg = 0;
			if (j == n || !(s[j] == '_' || s[j] == '$' || s[j] == '@'))
				for (const char *const *r = regs; *r && !reg; r++)
					reg = strlen(*r) == j - i && strncasecmp(*r, s + i, j - i) == 0;
			/* a name with a dot (lab.x): not a register */
			if (reg && j < n && s[j] == '.' && j + 1 < n && !strchr("wlbWLB", s[j + 1]))
				reg = 0;
			if (reg)
				bputc(&b, '%');
			bput(&b, s + i, j - i);
			i = j;
			continue;
		}
		bputc(&b, s[i++]);
	}
	return b.s;
}

/* One operand of r68togas's as the source's. */
static void undo_operand_text(struct buf *b, const char *s, size_t n);

static void undo_operand(struct buf *b, const char *s, size_t n)
{
	if (regs_percent && n > 0 && s[0] != '<') {
		char *p = percent_regs(s, n);
		undo_operand_text(b, p, strlen(p));
		free(p);
		return;
	}
	undo_operand_text(b, s, n);
}

static void undo_operand_text(struct buf *b, const char *s, size_t n)
{
	if (n == 0)
		return;
	if (s[0] == '#') {
		for (int i = 0; i < mask_names.n; i++)
			if (strlen(mask_names.v[i]) == n - 1 && strncmp(mask_names.v[i], s + 1, n - 1) == 0) {
				bputs(b, mask_texts.v[i]);
				return;
			}
		if (n > 6 && strncmp(s + n - 5, "__rev", 5) == 0) {
			bputc(b, '#');
			unname(b, s + 1, n - 6);
			return;
		}
		bputc(b, '#');
		undo_expr(b, s + 1, n - 1);
		return;
	}
	if (s[0] == '<' && s[n - 1] == '>') {
		/* a macro argument in <>: as r68 takes it, in quotes; in <>
		 * twice for a call through r68L (<!<...!>>) */
		struct buf t = { 0 };
		for (size_t i = 1; i + 1 < n; i++) {
			if (s[i] == '!' && i + 2 < n)
				i++;
			bputc(&t, s[i]);
		}
		static int twice;
		if (t.n >= 2 && t.s[0] == '<' && t.s[t.n - 1] == '>') {
			twice = 1;
			undo_operand(b, t.s, t.n);
			twice = 0;
		} else if (t.n == 0) {
			;
		} else if (twice && strpbrk(t.s, "<>!") != NULL && strpbrk(t.s, " \t,") == NULL) {
			/* angled for its < > ! (in a call through r68L) */
			bputs(b, t.s);
		} else if (memchr(t.s, '"', t.n) == NULL) {
			bprintf(b, "\"%s\"", t.s);
		} else {
			bputs(b, t.s);
		}
		free(t.s);
		return;
	}
	/* register lists, -(An), (An)+, (An): as they are */
	if (s[0] == '-' && n > 3 && s[1] == '(' && is_regtext(s + 2, n - 3)) {
		bput(b, s, n);
		return;
	}
	if (s[0] == '(' && close_paren(s, n, 0) + 1 >= n - 1 && n >= 3 && is_regtext(s + 1, close_paren(s, n, 0) - 1)) {
		bput(b, s, n);
		return;
	}
	/* ((E).s,R,...): r68's full format (68020) */
	if (s[0] == '(' && n > 1 && s[1] == '(' && s[n - 1] == ')') {
		size_t c = close_paren(s, n, 1);
		if (c + 3 < n && s[c + 1] == '.' && s[c + 3] == ',') {
			char size = (char)tolower((unsigned char)s[c + 2]);
			bputc(b, '(');
			if (size == 'w')
				bputc(b, '(');
			undo_expr(b, s + 2, c - 2);
			if (size == 'w')
				bputs(b, ").w");
			const char *rest = s + c + 3;
			size_t rn = n - 1 - (c + 3);
			/* without the suppressed index r68togas added */
			if (rn >= 7 && strncmp(rest + rn - 7, ",zd0.w", 6) == 0)
				rn -= 6;
			if (rn >= 6 && strncmp(rest + rn - 6, ",zd0.w", 6) == 0)
				rn -= 6;
			bput(b, rest, rn);
			bputc(b, ')');
			return;
		}
	}
	/* (E).s(R...) and (E).s alone */
	if (s[0] == '(') {
		size_t c = close_paren(s, n, 0);
		if (c + 2 < n && s[c + 1] == '.' && strchr("bwlBWL", s[c + 2])) {
			if (c + 3 < n && s[c + 3] == '{' && tolower((unsigned char)s[c + 2]) == 'l') {
				/* (E).l{o:w}: a bare address with a bit field */
				undo_expr(b, s + 1, c - 1);
				bput(b, s + c + 3, n - c - 3);
				return;
			}
			if (c + 3 == n) {
				/* (E).l: a bare address, absolute long in r68 */
				if (tolower((unsigned char)s[c + 2]) == 'l') {
					undo_expr(b, s + 1, c - 1);
				} else {
					undo_expr(b, s + 1, c - 1);
					bput(b, s + c + 1, 2);
				}
				return;
			}
			if (s[c + 3] == '(') {
				undo_expr(b, s + 1, c - 1);
				bput(b, s + c + 3, n - c - 3);
				return;
			}
		}
	}
	/* E(R...) */
	if (s[n - 1] == ')') {
		size_t o = n - 1;
		int depth = 0;
		for (size_t i = n; i-- > 0;) {
			if (s[i] == ')')
				depth++;
			else if (s[i] == '(' && --depth == 0) {
				o = i;
				break;
			}
		}
		if (o > 0 && o < n - 1 && is_regtext(s + o + 1, n - o - 2 > 0 ? n - o - 2 : 0)) {
			undo_expr(b, s, o);
			bput(b, s + o, n - o);
			return;
		}
	}
	undo_expr(b, s, n);
}

/* A dc list of r68togas's as the source's: MRI strings ('...', '' a quote)
 * as r68 strings "...", the rest as expressions. (A string with a
 * backslash, which r68togas writes as $5c between its parts, is noted.) */
static void undo_list(struct buf *b, const char *s, size_t n)
{
	const char *pc[512];
	size_t pl[512];
	int k = split_args(s, n, pc, pl, 512);
	for (int i = 0; i < k; i++) {
		if (i)
			bputc(b, ',');
		if (pl[i] >= 2 && pc[i][0] == '\'' && pc[i][pl[i] - 1] == '\'') {
			bputc(b, '"');
			for (size_t c = 1; c + 1 < pl[i]; c++) {
				char ch = pc[i][c];
				if (ch == '\'' && pc[i][c + 1] == '\'')
					c++;
				if (ch == '"' || ch == '\\')
					bputc(b, '\\');
				bputc(b, ch);
			}
			bputc(b, '"');
		} else {
			undo_operand(b, pc[i], pl[i]);
		}
	}
}

/* -- Lines -- */

struct gline {
	char *label;	/* as written, with its ':' */
	char *op;
	char *args;
	char *tag;	/* ;[r68: ...]'s text, null if none */
	char *rest;	/* the comment, with its blank */
};

static void gl_free(struct gline *g)
{
	free(g->label);
	free(g->op);
	free(g->args);
	free(g->tag);
	free(g->rest);
}

/* Splits a gas (MRI) line of r68togas's: label, operation, operands (to a
 * blank outside quotes and <...>), the note, the comment. */
static void gl_split(const char *line, struct gline *g)
{
	memset(g, 0, sizeof *g);
	const char *p = line;
	if (*p && !isspace((unsigned char)*p)) {
		const char *q = p;
		while (*q && !isspace((unsigned char)*q))
			q++;
		g->label = xstrndup(p, q - p);
		p = q;
	}
	while (*p == ' ' || *p == '\t')
		p++;
	const char *q = p;
	if (*p != ';')
		while (*q && !isspace((unsigned char)*q))
			q++;
	g->op = xstrndup(p, q - p);
	p = q;
	const char *blank = p;
	while (*p == ' ' || *p == '\t')
		p++;
	q = p;
	int quote = 0, angle = 0;
	if (*p != ';')
		while (*q) {
			if (quote) {
				if (*q == '\'')
					quote = 0;
			} else if (angle) {
				if (*q == '!' && q[1])
					q++;
				else if (*q == '<')
					angle++;
				else if (*q == '>')
					angle--;
			} else if (*q == '\'') {
				quote = 1;
			} else if (*q == '<' && (q == p || q[-1] == ',')) {
				angle = 1;
			} else if (isspace((unsigned char)*q)) {
				break;
			}
			q++;
		}
	g->args = xstrndup(p, q - p);
	if (q == p)
		q = blank;
	/* the note: ;[r68: TEXT] after a blank; TEXT may hold a ']' */
	const char *t = q;
	while (*t == ' ' || *t == '\t')
		t++;
	if (starts(t, ";[r68: ")) {
		const char *s = t + 7, *end = NULL;
		for (const char *c = s; *c; c++)
			if (*c == ']' && (c[1] == '\0' || isspace((unsigned char)c[1])))
				end = c;
		if (end != NULL) {
			g->tag = xstrndup(s, end - s);
			q = end + 1;
		}
	}
	g->rest = xstrdup(q);
}

/* The comment r68togas wrote (" ;text") as the source's (" text"). */
static void put_rest(struct buf *b, const char *rest)
{
	const char *c = rest;
	while (*c == ' ' || *c == '\t')
		c++;
	if (*c == '\0')
		return;
	bput(b, rest, c - rest);
	bputs(b, *c == ';' ? c + 1 : c);
}

static void put_label(struct buf *b, const char *label)
{
	if (label == NULL)
		return;
	size_t n = strlen(label);
	int global = n > 0 && label[n - 1] == ':';
	unname(b, label, n - global);
	if (global)
		bputc(b, ':');
}

/* the epilogue's xdef names: the source's global labels */
static struct names epi_globals;

/* Label NAME (N chars) of r68togas's, written as a macro's argument, as the
 * source's: with ':' where it is global. */
static void put_arg_label(struct buf *b, const char *name, size_t n)
{
	unname(b, name, n);
	if (has(&epi_globals, name, n))
		bputc(b, ':');
}

/* The source's label of gas line LINE (allocated, with ':' if global), or
 * null: in the label field, or an argument of r68togas's prelude macros. */
static char *line_label(const char *line)
{
	struct buf b = { 0 };
	if (line[0] != ' ' && line[0] != '\t' && line[0] != '*' && line[0] != '\0' && line[0] != '#') {
		if (starts(line, ".Lr68") || starts(line, "r68__"))
			return NULL;
		put_label(&b, xstrndup(line, strcspn(line, " \t")));
		return b.s;
	}
	const char *p = line + strspn(line, " \t");
	static const struct { const char *op; int arg; int global; } ms[] = {
		{ "r68xlab ", 0, 1 }, { "r68glab ", 0, 1 }, { "r68gdef ", 0, 1 }, { "r68dc ", 2, 0 },
		{ "r68ds ", 2, 0 }, { "r68dz ", 2, 0 }, { "r68fill ", 1, 0 }, { "r68lab ", 1, 0 },
		{ "r68do ", 0, 0 }, { "r68lo ", 0, 0 }, { "r68align ", 3, 0 }, { "r68com ", 0, 0 },
	};
	for (size_t m = 0; m < sizeof ms / sizeof *ms; m++) {
		if (!starts(p, ms[m].op))
			continue;
		const char *a = p + strlen(ms[m].op);
		const char *pc[8];
		size_t pl[8];
		int k = split_args(a, strcspn(a, " \t"), pc, pl, 8);
		if (k <= ms[m].arg || pl[ms[m].arg] == 0)
			return NULL;
		if (ms[m].global) {
			unname(&b, pc[ms[m].arg], pl[ms[m].arg]);
			bputc(&b, ':');
		} else {
			put_arg_label(&b, pc[ms[m].arg], pl[ms[m].arg]);
		}
		return b.s;
	}
	return NULL;
}

/* The angle-bracketed text S (N chars) without its <> and ! escapes. */
static char *unangle(const char *s, size_t n)
{
	struct buf t = { 0 };
	bputs(&t, "");
	if (n >= 2 && s[0] == '<' && s[n - 1] == '>') {
		s++;
		n -= 2;
	}
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '!' && i + 1 < n)
			i++;
		bputc(&t, s[i]);
	}
	return t.s;
}

static void undo_statement(struct buf *b, const char *label, const char *op, const char *args, const char *rest);

/* A line LINE of r68togas's (one primary line: a statement or a comment)
 * as the source's, into B. */
static void undo_line(struct buf *b, const char *line)
{
	if ((line[0] == '*' && !starts(line, "*\t;[r68: ")) || line[0] == '\0') {
		bputs(b, line);
		return;
	}
	struct gline g;
	gl_split(line[0] == '*' ? line + 1 : line, &g);
	if (g.tag != NULL) {
		/* noted: the operation alone, or the statement */
		/* the operation alone: where r68togas notes only it (ori.w
		 * to ccr, addi, movea, exg.l, swap.l, ifp1, -y's branches) */
		static const char *const ops[] = { "ori", "andi", "eori", "addi", "subi", "movea", "exg", "swap", "ifp1",
			NULL };
		char tb[16];
		size_t tn = strcspn(g.tag, ".");
		snprintf(tb, sizeof tb, "%.*s", (int)(tn < 15 ? tn : 15), g.tag);
		for (char *c = tb; *c; c++)
			*c = (char)tolower((unsigned char)*c);
		int op_only = 0;
		if (strpbrk(g.tag, " \t") == NULL) {
			for (const char *const *o = ops; *o && !op_only; o++)
				op_only = strcmp(tb, *o) == 0;
			size_t on = strlen(g.op);
			if (tb[0] == 'b' && tolower((unsigned char)g.op[0]) == 'b' && on > 2 && g.op[on - 2] == '.' &&
			    tolower((unsigned char)g.op[on - 1]) == 'l')
				op_only = 1;
		}
		int wrapped = strcasecmp(g.op, "r68glab") == 0 || strcasecmp(g.op, "r68xlab") == 0 ||
			strcasecmp(g.op, "r68gdef") == 0 || strcasecmp(g.op, "r68chip") == 0;
		if (op_only && wrapped) {
			/* the statement inside the prelude macro's call, its
			 * operation the noted one */
			const char *pc[2];
			size_t pl[2];
			if (split_args(g.args, strlen(g.args), pc, pl, 2) == 2) {
				char *stmt = unangle(pc[1], g.args + strlen(g.args) - pc[1]);
				struct buf l = { 0 };
				bprintf(&l, " %s", stmt);
				struct gline in;
				gl_split(l.s, &in);
				struct buf lab = { 0 };
				if (strcasecmp(g.op, "r68chip") != 0) {
					bput(&lab, pc[0], pl[0]);
					bputc(&lab, ':');
				}
				undo_statement(b, lab.s ? lab.s : g.label, g.tag, in.args, g.rest);
				gl_free(&in);
				free(lab.s);
				free(l.s);
				free(stmt);
			}
		} else if (op_only) {
			undo_statement(b, g.label, g.tag, strcmp(tb, "ifp1") == 0 ? "" : g.args, g.rest);
		} else {
			/* the label where r68togas wrote it: on the line, or as
			 * a macro's argument (r68com NAME,...) */
			char *lab = g.label == NULL ? line_label(line[0] == '*' ? line + 1 : line) : NULL;
			if (lab != NULL)
				bputs(b, lab);
			else
				put_label(b, g.label);
			free(lab);
			bputc(b, ' ');
			bputs(b, g.tag);
			put_rest(b, g.rest);
		}
		gl_free(&g);
		return;
	}
	undo_statement(b, g.label, g.op, g.args, g.rest);
	gl_free(&g);
}

static void undo_statement(struct buf *b, const char *label, const char *op, const char *args, const char *rest)
{
	char base[32];
	size_t i;
	for (i = 0; op[i] && op[i] != '.' && i + 1 < sizeof base; i++)
		base[i] = (char)tolower((unsigned char)op[i]);
	base[i] = '\0';
	const char *size = op[i] == '.' ? op + i : "";
	const char *pc[64];
	size_t pl[64];
	int k = *args ? split_args(args, strlen(args), pc, pl, 64) : 0;
	/* r68togas's prelude macros */
	static const char *const skipped[] = { "r68nam", "r68ttl", "r68opt", "r68pag", "r68page", "r68spc", "r68end",
		"r68endc", "r68spanreg", NULL };
	for (const char *const *s = skipped; *s; s++)
		if (strcmp(base, *s) == 0) {
			/* a statement gas mustn't see, its text in the comment */
			put_label(b, label);
			bputc(b, ' ');
			bputs(b, op + 3);
			put_rest(b, rest);
			return;
		}
	if ((strcmp(base, "r68dc") == 0 || strcmp(base, "r68ds") == 0 || strcmp(base, "r68dz") == 0) && k == 4) {
		put_arg_label(b, pc[2], pl[2]);
		bprintf(b, " %s%.*s ", op[0] == 'R' ? (base[3] == 'd' && base[4] == 'c' ? "DC" : base[4] == 's' ? "DS" : "DZ") : base + 3, (int)pl[1], pc[1]);
		char *list = unangle(pc[3], pl[3]);
		if (base[3] == 'd' && base[4] == 'c')
			undo_list(b, list, strlen(list));
		else
			undo_expr(b, list, strlen(list));
		free(list);
		put_rest(b, rest);
		return;
	}
	if (strcmp(base, "r68fill") == 0 && k == 3) {
		put_arg_label(b, pc[1], pl[1]);
		bputs(b, op[0] == 'R' ? " DS.B " : " ds.b ");
		undo_expr(b, pc[2], pl[2]);
		put_rest(b, rest);
		return;
	}
	if (strcmp(base, "r68lab") == 0 && k == 2) {
		put_arg_label(b, pc[1], pl[1]);
		put_rest(b, rest);
		return;
	}
	if (strcmp(base, "r68align") == 0 && k >= 3) {
		if (k >= 4) {
			put_arg_label(b, pc[3], pl[3]);
		}
		bputs(b, op[0] == 'R' ? " ALIGN" : " align");
		if (!(pl[2] == 1 && pc[2][0] == '2')) {
			bputc(b, ' ');
			bput(b, pc[2], pl[2]);
		}
		put_rest(b, rest);
		return;
	}
	if ((strcmp(base, "r68do") == 0 || strcmp(base, "r68lo") == 0) && k == 3) {
		put_arg_label(b, pc[0], pl[0]);
		int bytes = atoi(pc[1]);
		bprintf(b, " %.2s.%c ", base + 3, bytes == 1 ? 'b' : bytes == 4 ? 'l' : 'w');
		undo_expr(b, pc[2], pl[2]);
		put_rest(b, rest);
		return;
	}
	if ((strcmp(base, "r68dox") == 0 || strcmp(base, "r68lox") == 0) && k == 2) {
		int bytes = atoi(pc[0]);
		bprintf(b, " %.2s.%c ", base + 3, bytes == 1 ? 'b' : bytes == 4 ? 'l' : 'w');
		undo_expr(b, pc[1], pl[1]);
		put_rest(b, rest);
		return;
	}
	if ((strcmp(base, "r68glab") == 0 || strcmp(base, "r68xlab") == 0 || strcmp(base, "r68gdef") == 0) && k >= 1) {
		struct buf lab = { 0 };
		bput(&lab, pc[0], pl[0]);
		bputc(&lab, ':');
		if (k >= 2) {
			/* the statement after the name, in <> */
			const char *a = pc[1];
			char *stmt = unangle(a, args + strlen(args) - a);
			struct gline g;
			struct buf l = { 0 };
			bprintf(&l, " %s", stmt);
			gl_split(l.s, &g);
			undo_statement(b, lab.s, g.op, g.args, rest);
			gl_free(&g);
			free(l.s);
			free(stmt);
		} else {
			put_label(b, lab.s);
			put_rest(b, rest);
		}
		free(lab.s);
		return;
	}
	if (strcmp(base, "r68chip") == 0 && k == 2) {
		char *stmt = unangle(pc[1], pl[1]);
		struct gline g;
		struct buf l = { 0 };
		bprintf(&l, " %s", stmt);
		gl_split(l.s, &g);
		undo_statement(b, label, g.op, g.args, rest);
		gl_free(&g);
		free(l.s);
		free(stmt);
		return;
	}
	if (strcmp(base, "r68l") == 0 && k >= 1) {
		/* a call through r68L: r68L MAC,args */
		put_label(b, label);
		bputc(b, ' ');
		unname(b, pc[0], pl[0]);
		if (k > 1) {
			bputc(b, ' ');
			for (int j = 1; j < k; j++) {
				if (j > 1)
					bputc(b, ',');
				/* <!<...!>>: an argument angled twice */
				if (pl[j] >= 6 && strncmp(pc[j], "<!<", 3) == 0) {
					char *once = unangle(pc[j], pl[j]);
					undo_operand(b, once, strlen(once));
					free(once);
				} else {
					undo_operand(b, pc[j], pl[j]);
				}
			}
		}
		put_rest(b, rest);
		return;
	}
	/* the source's own statements */
	put_label(b, label);
	bputc(b, ' ');
	if (strcmp(base, "macro") == 0) {
		/* r68's macro takes no operand: its text after the operation
		 * was written ;TEXT, then the comment ;REST */
		bputs(b, op);
		const char *t = *args ? args : rest;
		t += strspn(t, " \t");
		if (*t == ';')
			t++;
		size_t tn = strcspn(t, " \t");
		if (tn > 0)
			bprintf(b, " %.*s", (int)tn, t);
		put_rest(b, *args ? rest : t + tn);
		return;
	}
	/* -b's jb<cc>, jbra, jbsr: r68's b<cc> (blo and bhs as bcs and bcc) */
	if (base[0] == 'j' && base[1] == 'b' && base[2] != '\0')
		op++;
	unname(b, op, strlen(op) - strlen(size));
	bputs(b, size);
	if (strcmp(base, "fill") == 0 && k == 3) {
		/* ds.b as fill N,1,0 */
		b->n -= 4;
		b->s[b->n] = '\0';
		bputs(b, op[0] == 'F' ? "DS.B " : "ds.b ");
		undo_expr(b, pc[0], pl[0]);
		put_rest(b, rest);
		return;
	}
	if (strcmp(base, "dcb") == 0 && k == 2) {
		/* dz as dcb N,0 */
		b->n -= strlen(op);
		b->s[b->n] = '\0';
		bprintf(b, "%s%s ", op[0] == 'D' ? "DZ" : "dz", size);
		undo_expr(b, pc[0], pl[0]);
		put_rest(b, rest);
		return;
	}
	if (*args) {
		bputc(b, ' ');
		if (strcmp(base, "dc") == 0) {
			undo_list(b, args, strlen(args));
		} else if (strcmp(base, "ifc") == 0 || strcmp(base, "ifnc") == 0) {
			bputs(b, args);
		} else {
			for (int j = 0; j < k; j++) {
				if (j)
					bputc(b, ',');
				undo_operand(b, pc[j], pl[j]);
			}
		}
	}
	put_rest(b, rest);
}

/* -- The file -- */

/* Whether LINE is one r68togas adds, which the source doesn't have. */
static int added_line(const char *line)
{
	if (line[0] == '#' && line[1] == ' ' && isdigit((unsigned char)line[2]))
		return 1;	/* a linemarker */
	if (starts(line, ".Lr68") || starts(line, "r68__"))
		return 1;	/* its labels and set symbols (r68__vs set N\t;... aside) */
	if (starts(line, " reloc ") || starts(line, " xdef "))
		return 1;
	if (starts(line, "* r68togas: "))
		return 1;
	if (starts(line, " ifne r68__mo") || starts(line, " ifeq r68__vs") || starts(line, " ifne r68__vs"))
		return 1;
	const char *sp = strpbrk(line, " \t");
	if (sp != NULL && sp > line + 4 && strncmp(sp - 4, "__p1", 4) == 0 && starts(sp + strspn(sp, " \t"), "set "))
		return 1;	/* NAME__p1 set ... */
	return 0;
}

static struct buf undo_out;
static char **hoisted_lines;	/* the moved macro definitions, each a run */
static int *hoisted_start;	/* each definition's first line in hoisted_lines */
static int nhoisted, nhoisted_lines, next_hoisted;

static void undo_region(char **lines, int from, int to, int depth);

/* The output of undo_region: a label alone is held (it may be the first
 * line of a block whose statement is noted on its last), written before
 * anything else. */
static char *held_label;

static void flush_held(void)
{
	if (held_label != NULL)
		bprintf(&undo_out, "%s\n", held_label);
	free(held_label);
	held_label = NULL;
}

static void emit(const char *text)
{
	flush_held();
	bprintf(&undo_out, "%s\n", text ? text : "");
}

/* A noted line ending a block whose first line may be a label alone (held):
 * the label and the noted statement as one line. */
static void emit_noted(const char *line)
{
	struct buf l = { 0 };
	undo_line(&l, line);
	if (held_label != NULL) {
		bprintf(&undo_out, "%s%s\n", held_label, l.s ? l.s : "");
		free(held_label);
		held_label = NULL;
	} else {
		bprintf(&undo_out, "%s\n", l.s ? l.s : "");
	}
	free(l.s);
}

/* Gives back LINES[FROM..TO) (the source's part, or a macro definition). */
static void undo_region(char **lines, int from, int to, int depth)
{
	int after_vs0 = 0, after_ends = 0, endc_after = 0;
	for (int i = from; i < to; i++) {
		const char *line = lines[i];
		lineno = i + 1;
		/* the endc after a statement a block began before */
		if (endc_after && strcmp(line, " endc") == 0) {
			endc_after = 0;
			continue;
		}
		/* an inlined use file: its use statement (the line before, as
		 * a comment), the file skipped */
		if (line[0] == '*' && i + 1 < to && starts(lines[i + 1], "* r68togas: use file ")) {
			emit(line + 1);
			continue;
		}
		if (starts(line, "* r68togas: use file ")) {
			int nest = 1;
			while (++i < to && nest > 0) {
				if (starts(lines[i], "* r68togas: use file "))
					nest++;
				else if (starts(lines[i], "* r68togas: end of use file "))
					nest--;
				else if (strcmp(lines[i], "* r68togas: a macro moved before the source") == 0)
					next_hoisted++;	/* its macro definitions too */
			}
			/* the marker back (or the owed one's "*") */
			if (!(i < to && (lines[i][0] == '#' || strcmp(lines[i], "*") == 0)))
				i--;
			continue;
		}
		if (strcmp(line, "* r68togas: a macro moved before the source") == 0) {
			/* a moved macro definition: back here */
			flush_held();
			if (next_hoisted < nhoisted) {
				int s = hoisted_start[next_hoisted];
				int e = next_hoisted + 1 < nhoisted ? hoisted_start[next_hoisted + 1] : nhoisted_lines;
				next_hoisted++;
				undo_region(hoisted_lines, s, e, depth + 1);
			} else {
				error("a moved macro definition missing");
			}
			continue;
		}
		if (strcmp(line, "* r68togas: (moved)") == 0)
			continue;
		/* blocks r68togas writes for one statement */
		if (strcmp(line, "* r68togas: a split dc line") == 0) {
			/* a split dc line: noted on its last line, its label on
			 * the first that has one */
			char *lab = NULL;
			struct buf xdefs = { 0 };	/* the block's xdef names, "\nNAME\n" */
			bputs(&xdefs, "\n");
			while (++i < to && strstr(lines[i], ";[r68: ") == NULL) {
				if (starts(lines[i], " xdef ")) {
					unname(&xdefs, lines[i] + 6, strcspn(lines[i] + 6, " \t"));
					bputc(&xdefs, '\n');
				} else if (lab == NULL) {
					lab = line_label(lines[i]);
				}
			}
			if (i < to) {
				struct gline g;
				gl_split(lines[i], &g);
				struct buf l = { 0 };
				if (lab == NULL)
					lab = line_label(lines[i]);
				if (lab != NULL) {
					bputs(&l, lab);
					/* global where the block exports it */
					struct buf nd = { 0 };
					bprintf(&nd, "\n%s\n", lab);
					if (lab[strlen(lab) - 1] != ':' && strstr(xdefs.s, nd.s) != NULL)
						bputc(&l, ':');
					free(nd.s);
				}
				free(xdefs.s);
				bprintf(&l, " %s", g.tag ? g.tag : "");
				put_rest(&l, g.rest);
				emit(l.s);
				free(l.s);
				gl_free(&g);
			}
			free(lab);
			continue;
		}
		if (starts(line, "r68__n\\@ set NARG")) {
			i += 4;	/* ifne r68__nset, set, set, endc */
			continue;
		}
		if (starts(line, "* closing ") && isdigit((unsigned char)line[10]) && strstr(line, " conditional") != NULL) {
			/* conditionals the source or a macro body leaves open,
			 * closed by r68togas */
			i += atoi(line + 10);
			continue;
		}
		if (starts(line, ".Lr68d set 1")) {
			/* conditionals before a moveq or move.b: to the endc after
			 * ifne .Lr68d */
			int seen = 0;
			while (++i < to) {
				if (strcmp(lines[i], " ifne .Lr68d") == 0)
					seen = 1;
				else if (seen && strcmp(lines[i], " endc") == 0)
					break;
			}
			continue;
		}
		if (starts(line, " ifndef .Lr68ends")) {
			i += 2;
			after_ends = 1;
			continue;
		}
		if (starts(line, ".Lr68ends")) {
			after_ends = 1;
			continue;
		}
		if (starts(line, " ifne .Lr68k")) {
			/* a branch to a constant parameter: the source's branch is
			 * after the else, an endc after it */
			while (i + 1 < to && strcmp(lines[i + 1], " else") != 0)
				i++;
			i++;
			endc_after = 1;
			continue;
		}
		if (starts(line, " ifeq r68__vs-2")) {
			/* a macro body's dc or ds in its three forms: the code's
			 * (the last) is the source's */
			int d = 0, elses = 0, s = -1, e = -1;
			int j;
			for (j = i; j < to; j++) {
				const char *t = lines[j];
				if (starts(t, " if"))
					d++;
				else if (strcmp(t, " else") == 0 && d <= 2) {
					if (++elses == 2)
						s = j + 1;
				} else if (starts(t, " endc") && (t[5] == '\0' || isspace((unsigned char)t[5]))) {
					if (d == 2 && s >= 0 && e < 0)
						e = j;
					if (--d == 0)
						break;
				}
			}
			if (j < to && strstr(lines[j], ";[r68: ") != NULL && s >= 0 && e >= 0) {
				/* noted on its last line: the note, the label from
				 * the code's form */
				char *lab = NULL;
				for (int k = s; k < e && lab == NULL; k++)
					lab = line_label(lines[k]);
				struct gline g;
				gl_split(lines[j], &g);
				struct buf l = { 0 };
				if (lab != NULL)
					bputs(&l, lab);
				bprintf(&l, " %s", g.tag ? g.tag : "");
				put_rest(&l, g.rest);
				emit(l.s);
				free(l.s);
				free(lab);
				gl_free(&g);
			} else if (s >= 0 && e >= 0) {
				undo_region(lines, s, e, depth);
			}
			i = j;
			continue;
		}
		if (starts(line, ".Lr68pcm set *")) {
			while (i + 1 < to && strcmp(lines[i + 1], " section .text") != 0)
				i++;
			i++;
			continue;
		}
		if (starts(line, ".Lr68dp")) {
			/* a debug line: to its noted last line */
			while (i + 1 < to && strstr(lines[i + 1], ";[r68: ") == NULL)
				i++;
			i++;
			if (i < to)
				emit_noted(lines[i]);
			continue;
		}
		if (starts(line, "r68__vs set 0")) {
			after_vs0 = 1;
			continue;
		}
		if (starts(line, "r68__vs set ") && strstr(line, "\t;") != NULL) {
			/* vsect, its line after the ; */
			emit(strstr(line, "\t;") + 2);
			continue;
		}
		if (starts(line, "r68__ofs set ")) {
			struct buf l = { 0 };
			bputs(&l, " org ");
			const char *a = line + 13;
			size_t an = strcspn(a, " \t");
			undo_expr(&l, a, an);
			put_rest(&l, a + an);
			emit(l.s);
			free(l.s);
			continue;
		}
		if (starts(line, " r68error <") && strstr(line, "which ELF cannot express>") != NULL && strstr(line, ";[r68: ") == NULL)
			continue;
		if (starts(line, " section .text") && i + 1 < to && starts(lines[i + 1], "r68__sid set 1\t;[r68: ")) {
			/* psect: noted on the second line, the comment on the first */
			struct gline g;
			gl_split(lines[i + 1], &g);
			struct buf l = { 0 };
			bprintf(&l, " %s", g.tag ? g.tag : "psect");
			put_rest(&l, line + 14);
			emit(l.s);
			free(l.s);
			gl_free(&g);
			i++;
			continue;
		}
		if (strstr(line, ";[r68: ") != NULL && (starts(line, " reloc ") || starts(line, " section "))) {
			/* the last line of a block, noted */
			emit_noted(line);
			continue;
		}
		if (added_line(line))
			continue;
		if (after_vs0 || after_ends) {
			/* vsect's or psect's ends: "*" before the source's line, or
			 * r68sect .text */
			after_vs0 = after_ends = 0;
			if (line[0] == '*' && strstr(line, ";[r68: ") == NULL) {
				emit(line + 1);
				continue;
			}
			if (strncasecmp(line, " r68sect .text", 14) == 0) {
				struct buf r = { 0 };
				bputs(&r, line[1] == 'R' ? " ENDS" : " ends");
				put_rest(&r, line + 14);
				emit(r.s);
				free(r.s);
				continue;
			}
		}
		if (strncasecmp(line, " r68sect ", 9) == 0)
			continue;	/* a section switch of a block */
		struct buf l = { 0 };
		undo_line(&l, line);
		flush_held();
		/* a label alone: maybe the first line of a block */
		if (line[0] != '*' && line[0] != ' ' && line[0] != '\t' && line[0] != '\0' && strpbrk(line, " \t") == NULL)
			held_label = l.s ? l.s : xstrdup("");
		else {
			emit(l.s);
			free(l.s);
		}
	}
	flush_held();
}

static void undo_r68togas(char **lines, int n)
{
	int src = -1, epi = n;
	for (int i = 0; i < n; i++) {
		if (src < 0 && strcmp(lines[i], "* r68togas: the source") == 0)
			src = i;
		if (strcmp(lines[i], "# 1 \"r68.epilogue\"") == 0)
			epi = i;
	}
	for (int i = 0; i < n; i++)
		if (strcmp(lines[i], "* r68togas: -r, registers with %") == 0)
			regs_percent = 1;
		else if (strcmp(lines[i], "r68__at set 1") == 0)
			at_names = 1;
	if (src < 0)
		die("%s: r68togas's output without its source's start (\"* r68togas: the source\"): an older r68togas's?",
		    input_name);
	/* the epilogue's movem masks: NAME equ ...   ORIGINAL */
	for (int i = epi; i < n; i++) {
		if (starts(lines[i], " xdef "))
			add(&epi_globals, lines[i] + 6, strcspn(lines[i] + 6, " \t"));
		const char *e = strstr(lines[i], " equ ");
		const char *o = strstr(lines[i], "   ");
		if (e != NULL && o != NULL && (starts(lines[i], "r68__mask") || strstr(lines[i], "__rev equ ") != NULL)) {
			add(&mask_names, lines[i], e - lines[i]);
			const char *t = o + strspn(o, " ");
			mask_texts.v = xrealloc(mask_texts.v, (mask_texts.n + 1) * sizeof *mask_texts.v);
			mask_texts.v[mask_texts.n++] = xstrdup(t);
		}
	}
	/* the moved macro definitions: before the source, each behind its
	 * linemarker, after the prelude's own */
	int in_prelude = 1;
	for (int i = 0; i < src; i++) {
		const char *l = lines[i];
		if (l[0] == '#' && l[1] == ' ' && isdigit((unsigned char)l[2])) {
			in_prelude = strstr(l, "\"r68.prelude\"") != NULL;
			if (!in_prelude) {
				hoisted_start = xrealloc(hoisted_start, (nhoisted + 1) * sizeof *hoisted_start);
				hoisted_start[nhoisted++] = nhoisted_lines;
			}
			continue;
		}
		if (in_prelude)
			continue;
		hoisted_lines = xrealloc(hoisted_lines, (nhoisted_lines + 1) * sizeof *hoisted_lines);
		hoisted_lines[nhoisted_lines++] = lines[i];
	}
	undo_region(lines, src + 1, epi, 0);
}

/* ----- main ----- */

static void usage(void)
{
	printf("usage: %s [-mode=c32|ucc] [-n=<psect>] [-o <output>] <source>\n", progname);
	printf("  Translates GCC's gas output (m68k-elfos9-gcc -S) to a Microware r68\n");
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
	if (is_r68togas(lines, nlines)) {
		/* r68togas's output: the source back */
		undo_r68togas(lines, nlines);
		if (errors)
			return 1;
		FILE *o = strcmp(output, "-") == 0 ? stdout : fopen(output, "w");
		if (o == NULL)
			die("cannot write %s", output);
		fputs(undo_out.s ? undo_out.s : "", o);
		if (o != stdout && fclose(o) != 0)
			die("cannot write %s", output);
		return 0;
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
	if (comms.n) {
		switch_to(S_BSS);
		bput(&out, comms.s, comms.n);
	}
	switch_to(S_CODE);
	if (open_vsect != S_NONE) {
		bputs(&other, " ends\n");
		bput(&out, other.s, other.n);
	}
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

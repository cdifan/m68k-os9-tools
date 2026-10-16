/* r68x_note.c: link-time expressions of external symbols; see r68x_note.h.
 *
 * MIT License; see elf2mod.c.
 */

#include "r68x_note.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *alloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (p == NULL) {
		fputs("out of memory\n", stderr);
		exit(1);
	}
	return p;
}

void r68x_add(struct r68x *x, const char *sym, size_t n, long coef)
{
	for (int i = 0; i < x->n; i++)
		if (strlen(x->t[i].sym) == n && strncmp(x->t[i].sym, sym, n) == 0) {
			x->t[i].coef += coef;
			if (x->t[i].coef == 0) {
				free(x->t[i].sym);
				x->t[i] = x->t[--x->n];
			}
			return;
		}
	if (coef == 0)
		return;
	struct r68x_term *t = alloc((x->n + 1) * sizeof *t);
	if (x->n)
		memcpy(t, x->t, x->n * sizeof *t);
	free(x->t);
	x->t = t;
	char *s = alloc(n + 1);
	memcpy(s, sym, n);
	s[n] = '\0';
	x->t[x->n].sym = s;
	x->t[x->n++].coef = coef;
}

void r68x_free(struct r68x *x)
{
	for (int i = 0; i < x->n; i++)
		free(x->t[i].sym);
	free(x->t);
	x->t = NULL;
	x->n = 0;
}

int r68x_needed(const struct r68x *x)
{
	return x->n > 1 || (x->n == 1 && x->t[0].coef != 1);
}

static int by_name(const void *a, const void *b)
{
	return strcmp(((const struct r68x_term *)a)->sym, ((const struct r68x_term *)b)->sym);
}

/* The text of X, terms sorted; LD: quoted names, spaces around *, ABSOLUTE(). */
static char *text(const struct r68x *x, int ld)
{
	struct r68x_term *t = alloc(x->n * sizeof *t);
	if (x->n)
		memcpy(t, x->t, x->n * sizeof *t);
	qsort(t, x->n, sizeof *t, by_name);
	size_t size = 16;
	for (int i = 0; i < x->n; i++)
		size += strlen(t[i].sym) + 48;
	char *s = alloc(size), *p = s;
	*p = '\0';
	for (int i = 0; i < x->n; i++) {
		long c = t[i].coef;
		const char *sign = c < 0 ? "-" : i ? "+" : "";
		long a = c < 0 ? -c : c;
		/* for ld, a term negated or multiplied as ABSOLUTE(): ld keeps
		 * -sym and 2*sym of a section-relative symbol relative to its
		 * section, adding the section's address back (-btext in a ROM
		 * at $400000 came out $400000) */
		const char *q = ld ? (c != 1 ? "ABSOLUTE(\"" : "\"") : "";
		const char *qe = ld ? (c != 1 ? "\")" : "\"") : "";
		if (a == 1)
			p += sprintf(p, "%s%s%s%s", ld && i ? (c < 0 ? " - " : " + ") : sign, q, t[i].sym, qe);
		else
			p += sprintf(p, ld ? "%s%ld * %s%s%s" : "%s%ld*%s%s%s", ld && i ? (c < 0 ? " - " : " + ") : sign, a, q,
				t[i].sym, qe);
	}
	free(t);
	return s;
}

char *r68x_text(const struct r68x *x)
{
	return text(x, 0);
}

char *r68x_ld_expression(const struct r68x *x)
{
	if (x->n == 0) {
		char *s = alloc(2);
		strcpy(s, "0");
		return s;
	}
	return text(x, 1);
}

void r68x_name(const char *text, char *name)
{
	unsigned long h = 2166136261UL;
	for (const unsigned char *p = (const unsigned char *)text; *p; p++)
		h = ((h ^ *p) * 16777619UL) & 0xffffffffUL;
	sprintf(name, R68X_PREFIX "%08lx", h);
}

int r68x_parse(const char *s, struct r68x *x)
{
	x->t = NULL;
	x->n = 0;
	const char *p = s;
	if (*p == '\0')
		return 0;
	while (*p) {
		long sign = 1;
		if (*p == '+' || *p == '-') {
			sign = *p == '-' ? -1 : 1;
			p++;
		} else if (p != s) {
			return 0;
		}
		long coef = 1;
		if (isdigit((unsigned char)*p)) {
			char *end;
			coef = strtol(p, &end, 10);
			if (*end != '*')
				return 0;
			p = end + 1;
		}
		const char *start = p;
		while (*p && *p != '+' && *p != '-')
			p++;
		if (p == start)
			return 0;
		r68x_add(x, start, p - start, sign * coef);
	}
	return 1;
}

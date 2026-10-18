/*
 * MIT License
 *
 * Copyright (c) 2021 Murachue
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define PACKAGE "bfd" // https://qiita.com/yasuo-ozu/items/4d3dbef6f48808ee110c
#include <bfd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

struct modheader {
	uint16_t magic;
	uint16_t sysrev;
	uint32_t size;
	uint32_t owner;
	uint32_t name;
	uint16_t accs;
	uint8_t type;
	uint8_t lang;
	uint8_t attr;
	uint8_t revs;
	uint16_t edit;
	uint32_t usage;
	uint32_t symbol;
	uint8_t reserved[14];
	uint16_t parity;

	uint32_t exec;
	uint32_t excpt;
	uint32_t mem;
	uint32_t stack;
	uint32_t idata;
	uint32_t irefs;
};

#if 1
// le->be
#define BE16(v) ((((v) >> 8) & 0xFF) | (((v) & 0xFF) << 8))
#define BE32(v) ((((v) >> 24) & 0xFF) | (((v) >> 8) & 0xFF00) | (((v) & 0xFF00) << 8) | (((v) & 0xFF) << 24))
#else
// be->be
#define BE16(v) v
#define BE32(v) v
#endif

char *opt_name;
// -1: not given; then the value of __os9_stack etc. (from a mainline ROF
// converted by rof2elf, such as cstart.r), or the default.
int opt_stack = -1;
int opt_revs = -1;
int opt_edit = -1;
int opt_stb = 0; // 1: as Microware C 3.2's l68, 2: as Ultra C's l68
const char *opt_owner = NULL;
// The module's layout (this fork), as a front end such as m68k-os9-l68 asks
// for it: elf2mod has no notion of module types, only of these settings.
int opt_perms = -1; // -p: the header's permissions, default 0x0555
int opt_sticky = 0; // -S: the sticky bit (bit 6 of M$Attr) on top of __os9_attrev
int opt_namelast = 0; // -N: the name after the code, not after the header
int opt_typelang = -1; // --typelang: overrides __os9_typelang
int opt_header = 0x48; // --header: 0x30 (no fields), 0x3C (M$Exec, M$Excpt, M$Mem), 0x48
int opt_xalign = 0; // -x: the code's start in the module aligned to this, zero fill
int opt_raw = 0; // --raw: the module's body only, no header, name or CRC
int opt_noentry = 0; // --entry=none: no entry point, whatever __os9_entry says
int opt_namepad = 0; // --name-pad: zero bytes after the name (l68 -x on a trap library)
const char *opt_ldmap = NULL; // --ld-map: GNU ld's map of the link, for the order of M$IRefs
unsigned char opt_namepadbyte = 0; // --name-pad-byte: the byte that pads an odd-length name
unsigned char *opt_irefspad = NULL; // --irefs-pad: bytes after the M$IRefs table, before the CRC
size_t opt_nirefspad = 0;
#define DEFAULT_STACK 0x00000C00
#define DEFAULT_REVS 1
#define DEFAULT_EDIT 0

typedef struct vec {
	char *buf;
	size_t len;
	size_t cap;
} Vec;

Vec *vec_new(int initsize) {
	Vec *vec = malloc(sizeof(Vec));
	vec->buf = malloc(initsize);
	vec->len = 0;
	vec->cap = initsize;
	return vec;
}

size_t vec_space(Vec *vec, size_t len) {
	size_t prelen = vec->len;
	size_t cap = vec->cap;
	while(cap < vec->len + len) {
		cap = cap * 3 / 2;
	}
	if(cap != vec->cap) {
		vec->buf = realloc(vec->buf, cap);
		vec->cap = cap;
	}

	vec->len += len;
	return prelen;
}

size_t vec_zero(Vec *vec, size_t len) {
	size_t prelen = vec->len;
	vec_space(vec, len);
	memset(vec->buf + prelen, 0, len);
	return prelen;
}

size_t vec_append(Vec *vec, void *src, size_t len) {
	size_t prelen = vec->len;
	vec_space(vec, len);
	memcpy(vec->buf + prelen, src, len);
	return prelen;
}

void vec_free(Vec *vec) {
	free(vec->buf);
	/*
	vec->buf = NULL;
	vec->len = 0;
	vec->cap = 0;
	*/
	free(vec);
}

// A pointer in the initialized data: its offset in the data area, and
// whether it points into .text.
typedef struct idref {
	uint32_t addr;
	int text;
	int obj; // the object it came from, in link order (--ld-map)
	int vs; // its vsect's place in the .r68ord records (INT_MAX: none)
} IdRef;

int cmp_idref(const void *a, const void *b) {
	uint32_t x = ((const IdRef*)a)->addr, y = ((const IdRef*)b)->addr;
	return x < y ? -1 : x > y;
}

// Both Microware linkers list the pointers object by object, the last
// object linked first (measured 2026-10-15), each object's vsect by vsect
// in the order its source has them, each vsect's in address order (a remote
// vsect before a plain one: its pointers first; measured 2026-10-17).
int cmp_idref_obj(const void *a, const void *b) {
	const IdRef *p = a, *q = b;
	if(p->obj != q->obj) return p->obj > q->obj ? -1 : 1;
	if(p->vs != q->vs) return p->vs < q->vs ? -1 : 1;
	return cmp_idref(a, b);
}

// An input section of the initialized data in GNU ld's map: its start, its
// size and its object's place in the link.
typedef struct inputsec {
	uint64_t start, size;
	int obj;
} InputSec;

// The objects' parts of .data and .remote.data in GNU ld's map MAP: their
// input sections (" .data  0x... 0x... file", the name alone on a line
// when long, the rest on the next); the objects numbered in their order of
// appearance. The number of input sections, -1 if MAP can't be read.
long read_ldmap(const char *map, InputSec **secs) {
	FILE *fp = fopen(map, "r");
	if(!fp) return -1;
	static char files[256][1024];
	char line[4096];
	int nfiles = 0, in_data = 0;
	long n = 0;
	*secs = NULL;
	while(fgets(line, sizeof line, fp)) {
		if(line[0] == '.') {
			char name[64];
			in_data = sscanf(line, "%63s", name) == 1 && (!strcmp(name, ".data") || !strcmp(name, ".remote.data"));
			continue;
		}
		if(!in_data || line[0] != ' ' || line[1] != '.') continue;
		char name[256], file[1024];
		unsigned long long start = 0, size = 0;
		int k = sscanf(line, " %255s 0x%llx 0x%llx %1023s", name, &start, &size, file);
		if(k == 1 && fgets(line, sizeof line, fp))
			k = 1 + sscanf(line, " 0x%llx 0x%llx %1023s", &start, &size, file);
		if(k != 4 || size == 0) continue;
		int f;
		for(f = 0; f < nfiles && strcmp(files[f], file); f++);
		if(f == nfiles) {
			if(nfiles == 256) continue;
			strcpy(files[nfiles++], file);
		}
		*secs = realloc(*secs, (n + 1) * sizeof(InputSec));
		(*secs)[n++] = (InputSec){ start, size, f };
	}
	fclose(fp);
	return n;
}

// Symbols Microware's linker defines whose values depend on the module's
// layout, which only elf2mod knows (this fork): btext, the module's start
// (offset 0), bname, its name, and etext, its end (the module's size), and
// the underscored forms Ultra C's l68 uses. Linker scripts PROVIDE them with
// any value; elf2mod puts the module offset into every reference to them,
// which ld -q keeps: a pointer in the initialized data (relocated when OS-9
// loads the module, as any pointer into the code), or a PC-relative
// reference in the code.
enum { LSYM_NONE, LSYM_BTEXT, LSYM_BNAME, LSYM_ETEXT };

// The jump table target standing for etext until the module's size is
// known (no code is there: .text never reaches the top of memory).
#define ETEXT_TARGET ((bfd_vma)0xFFFFFFFE)

int linker_symbol(const asymbol *s) {
	const char *n = bfd_asymbol_name(s);
	if(*n == '_') n++;
	if(!strcmp(n, "btext")) return LSYM_BTEXT;
	if(!strcmp(n, "bname")) return LSYM_BNAME;
	if(!strcmp(n, "etext")) return LSYM_ETEXT;
	return LSYM_NONE;
}

// A reference to such a symbol: where (an offset in .text, or in the data
// area), which symbol, the relocation type, and the addend.
typedef struct lsref {
	uint32_t addr;
	int kind;
	int type;
	int64_t addend;
} LsRef;

// An absolute reference from the code to a code label (dc.w Init in a
// driver's entry table, dc.l, move.w #label): the label's offset in the
// module goes into the field, as both Microware linkers write it (measured
// 2026-10-09; no relocation at load, the code being position independent).
// The link puts the code at its VMA (0x400000), so the field is set here.
typedef struct coderef {
	uint32_t addr; // in .text
	int type;      // R_68K_32, R_68K_16, R_68K_8
	bfd_vma value; // the label's VMA plus the addend
} CodeRef;

size_t make_idrefs(Vec *vec, IdRef *refs, size_t nrefs, int for_text) {
	size_t iblock = vec_space(vec, 4);
	size_t ihi = iblock;
	uint16_t hi16 = -1;
	uint16_t count = 0;
	for(size_t i = 0; i < nrefs; i++) {
		if(refs[i].text != for_text) {
			continue;
		}
		uint32_t addr = refs[i].addr;
		if(hi16 != (addr >> 16)) {
			if(0 < count) {
				uint16_t *p = (uint16_t*)(vec->buf + ihi);
				p[0] = BE16(hi16);
				p[1] = BE16(count);

				ihi = vec_space(vec, 4);
			}
			hi16 = addr >> 16;
			count = 0;
		}
		uint16_t lo16 = BE16(addr & 0xFFFF);
		vec_append(vec, &lo16, 2);
		count++;
	}

	// XXX: copy code
	if(0 < count) {
		uint16_t *p = (uint16_t*)(vec->buf + ihi);
		p[0] = BE16(hi16);
		p[1] = BE16(count);

		ihi = vec_space(vec, 4);
	}

	// end of idrefs for a section
	uint16_t *p = (uint16_t*)(vec->buf + ihi);
	p[0] = BE16(0);
	p[1] = BE16(0);

	return iblock;
}

// The OS-9 module CRC of the first len bytes.
// Hex digits, two to a byte (an optional leading $ or 0x), into a new buffer.
int parse_hex_bytes(const char *s, unsigned char **out, size_t *len) {
	if(*s == '$') s++;
	else if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
	size_t n = strlen(s);
	if(n == 0 || n % 2) return 0;
	unsigned char *b = malloc(n / 2);
	for(size_t i = 0; i < n; i++) {
		int c = s[i];
		int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
		if(v < 0) {
			free(b);
			return 0;
		}
		if(i % 2) b[i / 2] |= v;
		else b[i / 2] = v << 4;
	}
	*out = b;
	*len = n / 2;
	return 1;
}

uint32_t module_crc(const char *buf, size_t len) {
	uint32_t crc = 0x00FFffff;
	for(size_t i = 0; i < len; i++) {
		crc ^= (uint32_t)(unsigned char)buf[i] << 16;
		for(size_t j = 0; j < 8; j++) {
			crc <<= 1;
			if(crc & 0x01000000) {
				crc ^= 0x00800063;
			}
		}
	}
	return ~crc & 0x00FFffff;
}

// Fill in a module header's size, name offset, parity and CRC (this
// fork: shared by the program and symbol modules).
uint32_t finish_module(Vec *vec, size_t iname, size_t icrc) {
	struct modheader *mh = (void*)(vec->buf + 0);
	mh->size = BE32(vec->len);
	mh->name = BE32(iname);
	uint16_t parity = 0xFFFF;
	for(int i = 0; i < 0x2e / 2; i++) {
		parity ^= ((uint16_t*)mh)[i];
	}
	mh->parity = parity; // don't BE16, already swapped
	uint32_t crc = module_crc(vec->buf, vec->len - 3);
	*(uint32_t*)(vec->buf + icrc) = BE32(crc);
	return crc;
}

typedef struct stbsym {
	int32_t value;
	uint16_t type;
	const char *name;
	size_t rank; // linker symbols: their order; others: after them
} StbSym;

// By value; at the same value, as l68 lists them: Ultra C's in the order
// it defines them (linker symbols first, then the others in definition
// order, which in ROFs is by name), Microware C 3.2's in reverse.
int stb_reverse;
int cmp_stbsym(const void *a, const void *b) {
	const StbSym *p = a, *q = b;
	if(p->value != q->value) return p->value < q->value ? -1 : 1;
	int c = p->rank != q->rank ? (p->rank < q->rank ? -1 : 1) : strcmp(p->name, q->name);
	return stb_reverse ? -c : c;
}

int opt(int *argc, char ***argv) {
	const struct option options[] = {
		{ "name",  required_argument, NULL, 'n' },
		{ "stack", required_argument, NULL, 's' },
		{ "revs",  required_argument, NULL, 'r' },
		{ "edit",  required_argument, NULL, 'e' },
		{ "stb",   optional_argument, NULL, 'g' },
		{ "owner", required_argument, NULL, 'u' },
		{ "perms", required_argument, NULL, 'p' },
		{ "sticky", no_argument,      NULL, 'S' },
		{ "name-last", optional_argument, NULL, 'N' },
		{ "code-align", required_argument, NULL, 'x' },
		{ "typelang", required_argument, NULL, 'T' + 0x100 },
		{ "header", required_argument, NULL, 'H' + 0x100 },
		{ "raw",   no_argument,       NULL, 'R' + 0x100 },
		{ "entry", required_argument, NULL, 'E' + 0x100 },
		{ "name-pad", required_argument, NULL, 'P' + 0x100 },
		{ "ld-map", required_argument, NULL, 'M' + 0x100 },
		{ "name-pad-byte", required_argument, NULL, 'B' + 0x100 },
		{ "irefs-pad", required_argument, NULL, 'I' + 0x100 },
		{ "help",  no_argument,       NULL, 'h' },
		{ 0, 0, 0, 0 },
	};

	for(;;) {
		switch(getopt_long(*argc, *argv, "n:s:r:e:gu:p:SN::x:h", options, NULL)) {
		case 'n':
			opt_name = optarg;
			break;
		case 'p':
			opt_perms = strtol(optarg + (*optarg == '$'), NULL, 16);
			break;
		case 'S':
			opt_sticky = 1;
			break;
		case 'N':
			opt_namelast = 1;
			if(optarg) opt_name = optarg;
			break;
		case 'x':
			opt_xalign = strtol(optarg, NULL, 0);
			if(opt_xalign != 2 && opt_xalign != 4 && opt_xalign != 8 && opt_xalign != 16) {
				printf("code alignment %s: 2, 4, 8 or 16\n", optarg);
				return 0;
			}
			break;
		case 'T' + 0x100:
			opt_typelang = strtol(optarg + (*optarg == '$'), NULL, 16);
			break;
		case 'H' + 0x100:
			opt_header = strtol(optarg + (*optarg == '$'), NULL, *optarg == '$' ? 16 : 0);
			if(opt_header != 0x30 && opt_header != 0x3C && opt_header != 0x48) {
				printf("header size %s: 0x30, 0x3C or 0x48\n", optarg);
				return 0;
			}
			break;
		case 'R' + 0x100:
			opt_raw = 1;
			break;
		case 'E' + 0x100:
			if(strcmp(optarg, "none")) {
				printf("--entry=%s: only none\n", optarg);
				return 0;
			}
			opt_noentry = 1;
			break;
		case 'M' + 0x100:
			opt_ldmap = optarg;
			break;
		case 'P' + 0x100:
			opt_namepad = strtol(optarg, NULL, 0);
			if(opt_namepad < 0 || opt_namepad > 64 || opt_namepad % 2) {
				printf("--name-pad=%s: an even number of bytes up to 64\n", optarg);
				return 0;
			}
			break;
		case 'B' + 0x100: {
			unsigned char *b;
			size_t n;
			if(!parse_hex_bytes(optarg, &b, &n) || n != 1) {
				printf("--name-pad-byte=%s: expected one byte in hex (two digits)\n", optarg);
				return 0;
			}
			opt_namepadbyte = b[0];
			free(b);
			break;
		}
		case 'I' + 0x100:
			free(opt_irefspad);
			if(!parse_hex_bytes(optarg, &opt_irefspad, &opt_nirefspad)) {
				printf("--irefs-pad=%s: expected bytes in hex (an even number of digits)\n", optarg);
				return 0;
			}
			break;
		case 's':
			opt_stack = strtol(optarg, NULL, 0);
			break;
		case 'r':
			opt_revs = strtol(optarg, NULL, 0);
			break;
		case 'g':
			if(!optarg || !strcmp(optarg, "c32")) opt_stb = 1;
			else if(!strcmp(optarg, "ucc")) opt_stb = 2;
			else {
				printf("unknown symbol module style %s (c32 or ucc)\n", optarg);
				return 0;
			}
			break;
		case 'u':
			opt_owner = optarg;
			break;
		case 'e':
			opt_edit = strtol(optarg, NULL, 0);
			break;
		case '?':
			printf("unknown option: %c\n", optopt);
			// FALLTHROUGH
		case 'h':
			printf("usage: %s [OPTIONS] ELFFILE OUTFILE\n", *argv[0]);
			printf("  -n, --name=MODNAME     override module name, default lowercase basename of ELFFILE\n");
			printf("  -s, --stack=STACKSIZE  set stack size, default __os9_stack or 0x%X\n", DEFAULT_STACK);
			printf("  -r, --revs=REVISION    set module revision, default from __os9_attrev or %d\n", DEFAULT_REVS);
			printf("  -e, --edit=EDITION     set module edition (not used by OS), default __os9_edition or %d\n", DEFAULT_EDIT);
			printf("  -g, --stb[=STYLE]      also write a symbol module OUTFILE.stb for the debugger,\n");
			printf("                         as Microware C 3.2's l68 (c32, default) or Ultra C's (ucc)\n");
			printf("  -u, --owner=GROUP.USER set module owner, default $GRPUSER or 0.0\n");
			printf("  -p, --perms=HEX        set the header's permissions, default 555\n");
			printf("  -S, --sticky           set the sticky bit (M$Attr bit 6) on top of __os9_attrev\n");
			printf("  -N, --name-last[=NAME] put the module name after the code, not after the header\n");
			printf("  -x, --code-align=N     align the code's start in the module to N (2, 4, 8, 16)\n");
			printf("      --typelang=HEX     set type and language, default __os9_typelang or 0101\n");
			printf("      --header=SIZE      0x48 (program header, default), 0x3C (M$Exec, M$Excpt,\n");
			printf("                         M$Mem; name last) or 0x30 (no fields; name last)\n");
			printf("      --raw              write the module's body only: no header, name or CRC\n");
			printf("      --entry=none       no entry point, whatever __os9_entry says\n");
			printf("      --name-pad=BYTES   zero bytes after the name (as l68 -x= pads a trap library)\n");
			printf("      --ld-map=FILE      GNU ld's map of the link: the initialized data's pointers\n");
			printf("                         (M$IRefs) by object, the last first, as l68 lists them\n");
			printf("      --name-pad-byte=HH the byte after an odd-length name (default 00)\n");
			printf("      --irefs-pad=HEX    bytes after the M$IRefs table, before the CRC\n");
			printf("  A stack, entry point, data area, initialized data or trap entry that is\n");
			printf("  nonzero but has no field in the chosen header is an error.\n");
			printf("  -h, --help             show this help\n");
			return 0;
		case -1:
			*argc -= optind;
			*argv += optind;
			return 1;
		}
	}
}

// The module owner from GROUP.USER (decimal, as l68's -gu and GRPUSER).
int parse_owner(const char *s, uint32_t *owner) {
	char *e;
	unsigned long g = strtoul(s, &e, 10), u;
	if(e == s || *e != '.') return 0;
	s = e + 1;
	u = strtoul(s, &e, 10);
	if(e == s || *e || g > 0xFFFF || u > 0xFFFF) return 0;
	*owner = g << 16 | u;
	return 1;
}

int main(int argc, char *argv[]) {
	if(!opt(&argc, &argv)) {
		return 1;
	}

	if(!argv[0]) {
		puts("ELFFILE not specified");
		return 1;
	}

	if(!argv[1]) {
		puts("OUTFILE not specified");
		return 1;
	}

	// The owner: --owner, else GRPUSER as with l68, else 0.0.
	uint32_t owner = 0;
	if(!opt_owner) opt_owner = getenv("GRPUSER");
	if(opt_owner && *opt_owner && !parse_owner(opt_owner, &owner)) {
		printf("bad owner %s, expected GROUP.USER\n", opt_owner);
		return 1;
	}

	if(!opt_name) {
		opt_name = strdup(argv[0]);
		{
			char *p = strrchr(opt_name, '/');
			if(p) {
				memmove(opt_name, p + 1, strlen(p + 1) + 1);
			}
		}
		{
			char *p = strrchr(opt_name, '.'); // after the directory (this fork)
			if(p) { *p = '\0'; }
		}
		// XXX: isn't there something like strlower()??
		for(char *p = opt_name; *p; p++) {
			*p = tolower(*p);
		}
	}

	// validate
	// open
	bfd *abfd = bfd_openr(argv[0], "elf32-m68k"); // target?
	if(!abfd) {
		bfd_perror("cannot openr (not elf32-m68k?)");
		return 1;
	}
	bfd_check_format(abfd, bfd_object); // ensure it is object, not archive nor corefile

	// get entry and required sections
	bfd_vma entry = bfd_get_start_address(abfd);

	asection *text = bfd_get_section_by_name(abfd, ".text");
	if(!text) {
		bfd_perror(".text not found");
		return 1;
	}
	// .text may be at any VMA: everything is computed relative to it.
	// (With remote data, the data area reaches above 0, so .text must be
	// placed above it.)
	bfd_size_type tsize = bfd_section_size(text);
	bfd_vma tvma = bfd_section_vma(text);
	if(tsize % 2) {
		puts(".text size not aligned to word (2bytes)");
		return 1;
	}

	asection *data = bfd_get_section_by_name(abfd, ".data");
	if(!data) {
		bfd_perror(".data not found");
		return 1;
	}
	bfd_size_type dsize = bfd_section_size(data);
	bfd_vma dvma = bfd_section_vma(data); // the data area's base, see below
	if(dsize % 2) {
		puts(".data size not aligned to word (2bytes)");
		return 1;
	}

	asection *bss = bfd_get_section_by_name(abfd, ".bss");
	if(!bss) {
		bfd_perror(".bss not found");
		return 1;
	}
	bfd_size_type bsize = bfd_section_size(bss);

	// Remote data (optional): after .bss, beyond the 64K a6 window.
	asection *rdata = bfd_get_section_by_name(abfd, ".remote.data");
	asection *rbss = bfd_get_section_by_name(abfd, ".remote.bss");
	bfd_size_type rdsize = rdata ? bfd_section_size(rdata) : 0;
	bfd_size_type rbsize = rbss ? bfd_section_size(rbss) : 0;
	if(rdsize % 2) {
		puts(".remote.data size not aligned to word (2bytes)");
		return 1;
	}

	// The data area starts at the lowest data section (.data, or .bss if
	// the linker script puts it first) and ends with the last one.
	// Addresses are compared as signed 32-bit values and offsets computed
	// modulo 2^32: the area starts at -0x8000, and with more than 32K of
	// data, sections cross from 0xFFFFFFFF to 0 (ld --no-check-sections).
	if((int32_t)bfd_section_vma(bss) < (int32_t)dvma) dvma = bfd_section_vma(bss);
#define DOFF(vma) ((uint32_t)((vma) - dvma))
	uint32_t dend = DOFF(bfd_section_vma(data)) + dsize;
	if(DOFF(bfd_section_vma(bss)) + bsize > dend) dend = DOFF(bfd_section_vma(bss)) + bsize;
	if(rdsize && DOFF(bfd_section_vma(rdata)) + rdsize > dend) dend = DOFF(bfd_section_vma(rdata)) + rdsize;
	if(rbsize && DOFF(bfd_section_vma(rbss)) + rbsize > dend) dend = DOFF(bfd_section_vma(rbss)) + rbsize;
	// The initialized data is one block covering .data and, if present,
	// .remote.data; whatever lies between (usually .bss) is stored as
	// zeros, so a linker script with remote initialized data should put
	// .bss before .data.
	uint32_t idstart = DOFF(bfd_section_vma(data)), idend = idstart + dsize;
	if(rdsize) {
		if(DOFF(bfd_section_vma(rdata)) < idstart) idstart = DOFF(bfd_section_vma(rdata));
		if(DOFF(bfd_section_vma(rdata)) + rdsize > idend) idend = DOFF(bfd_section_vma(rdata)) + rdsize;
	}
	uint32_t idsize = idend - idstart;

	// relocations (with symbols required by bfd)
	// symtab not required directly but required for relocs
	long symsz = bfd_get_symtab_upper_bound(abfd);
	asymbol **syms = malloc(symsz);
	long nsyms = bfd_canonicalize_symtab(abfd, syms);

	// Module header values from a mainline ROF converted by rof2elf.
	asymbol *s_typelang = NULL, *s_attrev = NULL, *s_edition = NULL, *s_stack = NULL, *s_entry = NULL, *s_trapent = NULL;
	// _sysedit and _sysattr, globals of any object (a C source's @_sysedit:
	// equ 19): the edition and attr/rev, over the root psect's and -e, as
	// both Microware linkers (measured 2026-10-15)
	asymbol *s_sysedit = NULL, *s_sysattr = NULL;
	for(long i = 0; i < nsyms; i++) {
		const char *n = bfd_asymbol_name(syms[i]);
		if(!strcmp(n, "__os9_typelang")) s_typelang = syms[i];
		else if(!strcmp(n, "__os9_attrev")) s_attrev = syms[i];
		else if(!strcmp(n, "__os9_edition")) s_edition = syms[i];
		else if(!strcmp(n, "__os9_stack")) s_stack = syms[i];
		else if(!strcmp(n, "__os9_entry")) s_entry = syms[i];
		else if(!strcmp(n, "__os9_trapent")) s_trapent = syms[i];
		else if(!strcmp(n, "_sysedit") && (syms[i]->flags & BSF_GLOBAL)) s_sysedit = syms[i];
		else if(!strcmp(n, "_sysattr") && (syms[i]->flags & BSF_GLOBAL)) s_sysattr = syms[i];
	}
	if(s_entry) {
		entry = bfd_asymbol_value(s_entry);
	}

	// .text relocs: no relocations to .data/.bss allowed, except
	// R_68K_16 (a6-relative), and R_68K_32/R_68K_8 to .data/.bss or
	// absolute symbols: their values are link-time constants (a6-relative
	// data offsets, equates), as in code converted from ROF by rof2elf.
	long trelsz = bfd_get_reloc_upper_bound(abfd, text);
	arelent **trels = malloc(trelsz);
	long ntrels = bfd_canonicalize_reloc(abfd, text, trels, syms);
	char *refd = calloc(nsyms + 1, 1); // symbols that relocations refer to (for -g)
	LsRef *tls = NULL, *dls = NULL; // references to linker symbols in .text, data
	size_t ntls = 0, ndls = 0;
	CodeRef *crefs = NULL; // absolute references from the code to code labels
	size_t ncrefs = 0;
	int64_t etext_entry = -1; // the jump table entry for etext: its offset in the data area
	for(long i = 0; i < ntrels; i++) {
		arelent *rel = trels[i];
		asection *ssec = rel->sym_ptr_ptr[0]->section;
		if(rel->sym_ptr_ptr >= syms && rel->sym_ptr_ptr < syms + nsyms) refd[rel->sym_ptr_ptr - syms] = 1;
		int ls = linker_symbol(rel->sym_ptr_ptr[0]);
		if(ls) {
			// PC-relative, or absolute (dc.w bname in a descriptor: the
			// offset in the module, as both Microware linkers write it,
			// measured 2026-10-15; as a code label's, see CodeRef)
			if(rel->howto->type != 4 /* R_68K_PC32 */ && rel->howto->type != 5 /* R_68K_PC16 */ &&
			   rel->howto->type != 1 /* R_68K_32 */ && rel->howto->type != 2 /* R_68K_16 */) {
				printf("%s at .text+%08lx: only PC-relative and 16- and 32-bit references to %s are supported\n", rel->howto->name, rel->address, rel->sym_ptr_ptr[0]->name);
				return 1;
			}
			tls = realloc(tls, (ntls + 1) * sizeof(LsRef));
			tls[ntls++] = (LsRef){ rel->address, ls, rel->howto->type, rel->addend };
			continue;
		}
		if(bfd_is_und_section(ssec)) {
			printf("undefined symbol %s (.text+%08lx)\n", rel->sym_ptr_ptr[0]->name, rel->address);
			return 1;
		}
		if(!opt_raw && ssec == text && (rel->howto->type == 1 /* R_68K_32 */ || rel->howto->type == 2 /* R_68K_16 */
		                                || rel->howto->type == 3 /* R_68K_8 */)) {
			// a code label's module offset (see CodeRef), checked when set
			crefs = realloc(crefs, (ncrefs + 1) * sizeof(CodeRef));
			crefs[ncrefs++] = (CodeRef){ rel->address, rel->howto->type, bfd_asymbol_value(rel->sym_ptr_ptr[0]) + rel->addend };
			continue;
		}
		int constant =bfd_is_abs_section(ssec) || ssec == data || ssec == bss || (ssec && (ssec == rdata || ssec == rbss));
		if(!(ssec == text || rel->howto->type == 2 /* R_68K_16(%a6) */
		     || ((rel->howto->type == 1 /* R_68K_32 */ || rel->howto->type == 3 /* R_68K_8 */) && constant))) {
			printf(".text inter-section relocation not allowed: %s %08lx %s@%s+%08lx\n", rel->howto->name, rel->address, rel->sym_ptr_ptr[0]->name, rel->sym_ptr_ptr[0]->section->name, rel->addend);
			return 1;
		}
		// The value must fit its field: a6-relative 16 bits (more than 64K
		// of a6 data), 8 bits, and short branches (bra.s).  ld only warns
		// about some of these with --noinhibit-exec, and not at all about
		// R_68K_16.  Far PC16 references are handled by the jump table.
		{
			int64_t v = (int64_t)(int32_t)(bfd_asymbol_value(rel->sym_ptr_ptr[0]) + rel->addend);
			int bad = 0;
			switch(rel->howto->type) {
			case 2: /* R_68K_16 */ bad = v < -0x8000 || v > 0xFFFF || (ssec != text && !bfd_is_abs_section(ssec) && v > 0x7FFF); break;
			case 3: /* R_68K_8 */ bad = v < -0x80 || v > 0xFF; break;
			case 6: /* R_68K_PC8 */ v -= (int64_t)(int32_t)(tvma + rel->address); bad = v < -0x80 || v > 0x7F; break;
			}
			if(bad) {
				printf("%s at .text+%08lx doesn't fit: %s%+ld = %lld\n", rel->howto->name, rel->address, rel->sym_ptr_ptr[0]->name, (long)rel->addend, (long long)v);
				return 1;
			}
		}
	}

	// .data and .remote.data relocs: only R_68K_32 is allowed
	asection *dsecs[2] = { data, rdsize ? rdata : NULL };
	IdRef *drefs = NULL;
	size_t ndrefs = 0;
	for(int s = 0; s < 2; s++) {
		asection *sec = dsecs[s];
		if(!sec) continue;
		long drelsz = bfd_get_reloc_upper_bound(abfd, sec);
		arelent **drels = malloc(drelsz);
		long ndrels = bfd_canonicalize_reloc(abfd, sec, drels, syms);
		drefs = realloc(drefs, (ndrefs + (ndrels > 0 ? ndrels : 0) + 1) * sizeof(IdRef));
		for(long i = 0; i < ndrels; i++) {
			arelent *rel = drels[i];
			if(rel->howto->type != 1 /* R_68K_32 */) {
				printf("%s relocation other than R_68K_32 not allowed: %s %08lx %s@%s+%08lx\n", sec->name, rel->howto->name, rel->address, rel->sym_ptr_ptr[0]->name, rel->sym_ptr_ptr[0]->section->name, rel->addend);
				return 1;
			}
			if(rel->sym_ptr_ptr >= syms && rel->sym_ptr_ptr < syms + nsyms) refd[rel->sym_ptr_ptr - syms] = 1;
			// What the pointer points to: code or data, relocated when OS-9
			// loads the module; an absolute value (an equate) or an undefined
			// weak symbol (0, as ld resolved it) stays as it is.
			asymbol *s = rel->sym_ptr_ptr[0];
			asection *ssec = s->section;
			int ls = linker_symbol(s);
			if(ls) {
				// A pointer into the module, relocated at load time; its
				// value is set once the module's layout is known.
				drefs[ndrefs].addr = DOFF(bfd_section_vma(sec)) + rel->address;
				drefs[ndrefs].text = 1;
				ndrefs++;
				dls = realloc(dls, (ndls + 1) * sizeof(LsRef));
				dls[ndls++] = (LsRef){ DOFF(bfd_section_vma(sec)) + rel->address, ls, rel->howto->type, rel->addend };
				continue;
			}
			if(bfd_is_abs_section(ssec) || (bfd_is_und_section(ssec) && (s->flags & BSF_WEAK))) {
				continue;
			}
			if(bfd_is_und_section(ssec)) {
				printf("undefined symbol %s (%s+%08lx)\n", s->name, sec->name, rel->address);
				return 1;
			}
			if(!(ssec == text || ssec == data || ssec == bss || (ssec && (ssec == rdata || ssec == rbss)))) {
				printf("%s pointer to an unexpected section: %s@%s\n", sec->name, s->name, ssec->name);
				return 1;
			}
			drefs[ndrefs].addr = DOFF(bfd_section_vma(sec)) + rel->address;
			drefs[ndrefs].text = ssec == text;
			ndrefs++;
		}
	}
	qsort(drefs, ndrefs, sizeof(IdRef), cmp_idref);

	// The module's values, and whether the chosen header has a field for
	// each (this fork): a value that is missing or zero needs none; a
	// nonzero one without a field is an error, whatever its source (a
	// symbol from the root psect, an option or a default). --raw has no
	// header, so nothing needs a field.
	int has_fields = opt_raw || opt_header >= 0x3C; // M$Exec, M$Excpt, M$Mem
	int has_prog = opt_raw || opt_header >= 0x48; // M$Stack, M$IData, M$IRefs
	int64_t stack = opt_stack >= 0 ? opt_stack : s_stack ? (int)bfd_asymbol_value(s_stack) : has_prog ? DEFAULT_STACK : 0;
	int64_t entryoff = opt_noentry ? 0 : (int64_t)(int32_t)(entry - tvma);
	{
		const char *missing = NULL;
		if(!has_prog && stack != 0) missing = "a stack (M$Stack)";
		else if(!has_prog && (idsize != 0 || ndrefs != 0)) missing = "initialized data (M$IData, M$IRefs)";
		else if(!has_fields && entryoff != 0) missing = "an entry point (M$Exec)";
		else if(!has_fields && s_trapent) missing = "a trap entry (M$Excpt)";
		else if(!has_fields && dend != 0) missing = "a data area (M$Mem)";
		if(missing) {
			printf("the module has %s, but its header (size 0x%X) has no field for it\n", missing, opt_header);
			return 1;
		}
	}
	// The name follows the code when the header has no program fields, as
	// with Microware's linkers.
	int namelast = opt_namelast || !has_prog;

	// generate
	Vec *vec = vec_new(0x1000);

	if(!opt_raw) {
		vec_zero(vec, opt_header);
	}

	size_t iname = 0;
	if(!opt_raw && !namelast) {
		iname = vec_append(vec, opt_name, strlen(opt_name) + 1);
		// pad to align to word
		if((strlen(opt_name) + 1) % 2) vec_append(vec, &opt_namepadbyte, 1);
		vec_zero(vec, opt_namepad);
	}
	if(!opt_raw && opt_xalign > 1) {
		vec_zero(vec, (opt_xalign - vec->len % opt_xalign) % opt_xalign);
	}

	size_t itext = vec_space(vec, tsize);
	if(!bfd_get_section_contents(abfd, text, vec->buf + itext, 0, tsize)) {
		bfd_perror("could not read .text");
		return 1;
	}
	// Absolute references from the code to code labels: their module offsets.
	for(size_t i = 0; i < ncrefs; i++) {
		CodeRef *c = &crefs[i];
		int64_t off = (int64_t)c->value - (int64_t)tvma + (int64_t)itext;
		int size = c->type == 1 ? 4 : c->type == 2 ? 2 : 1;
		if(size < 4 && (off < 0 || off > (size == 2 ? 0xFFFF : 0xFF))) {
			printf("%s at .text+%08x: the code label's module offset %lld doesn't fit\n",
			       size == 2 ? "R_68K_16" : "R_68K_8", c->addr, (long long)off);
			return 1;
		}
		unsigned char *p = (unsigned char*)vec->buf + itext + c->addr;
		for(int k = 0; k < size; k++)
			p[k] = (unsigned char)((uint64_t)off >> (8 * (size - 1 - k)));
	}
	free(crefs);
	if(opt_raw) {
		iname = itext; // no name: bname is the code's start
	} else if(namelast) {
		iname = vec_append(vec, opt_name, strlen(opt_name) + 1);
		if((strlen(opt_name) + 1) % 2) vec_append(vec, &opt_namepadbyte, 1);
		vec_zero(vec, opt_namepad);
	}

	struct {
		uint32_t off;
		uint32_t len;
	} idatahdr = { BE32(idstart), BE32(idsize) };
	size_t iidatahdr = has_prog ? vec_append(vec, &idatahdr, sizeof(idatahdr)) : 0;
	size_t iidata = vec_zero(vec, idsize);
	if(!bfd_get_section_contents(abfd, data, vec->buf + iidata + DOFF(bfd_section_vma(data)) - idstart, 0, dsize)) {
		bfd_perror("could not read .data");
		return 1;
	}
	if(rdsize && !bfd_get_section_contents(abfd, rdata, vec->buf + iidata + DOFF(bfd_section_vma(rdata)) - idstart, 0, rdsize)) {
		bfd_perror("could not read .remote.data");
		return 1;
	}

	// Jump table (this fork): a PC-relative call or address load whose
	// target is beyond +-32K (ld --noinhibit-exec left the value
	// truncated) goes through an entry of _jmptbl, which the linker script
	// reserves in .data, up to _ejmptbl: "jmp target" ($4EF9 and a 32-bit
	// address, relocated at load time). The instruction keeps its size:
	//   bsr.w f       -> jsr _jmptbl+x(a6)
	//   bra.w f       -> jmp _jmptbl+x(a6)
	//   lea f(pc),An  -> movea.l _jmptbl+x+2(a6),An   (the address of f)
	//   pea f(pc)     -> move.l _jmptbl+x+2(a6),-(sp)
	// a6-relative displacements equal VMAs, since the data area starts at
	// -0x8000 with A6 0x8000 into it.
	{
		bfd_vma jt = 0, ejt = 0;
		int have_jt = 0, have_ejt = 0;
		for(long i = 0; i < nsyms; i++) {
			const char *n = bfd_asymbol_name(syms[i]);
			if(!strcmp(n, "_jmptbl")) { jt = bfd_asymbol_value(syms[i]); have_jt = 1; }
			else if(!strcmp(n, "_ejmptbl")) { ejt = bfd_asymbol_value(syms[i]); have_ejt = 1; }
		}
		bfd_vma *targets = NULL;
		size_t ntargets = 0, nfar = 0;
		// The most the module can grow to: what's laid out so far (header,
		// name, code, initialized data), then the two relocation tables
		// (4 bytes per 64K group and 2 per pointer, a 4-byte end each; at
		// most a group per pointer: the data pointers and the table's
		// entries), then the CRC. A reference to etext is near only if it
		// reaches even that far.
		size_t jtcap = have_jt && have_ejt && (int32_t)(ejt - jt) >= 0 ? (size_t)(int32_t)(ejt - jt) / 6 : 0;
		uint64_t maxend = vec->len + 8 + 6 * (uint64_t)(ndrefs + jtcap) + 4 + 4; // + 4: etext (see below)
		for(long i = 0; i < ntrels; i++) {
			arelent *rel = trels[i];
			if(rel->howto->type != 5 /* R_68K_PC16 */ && rel->howto->type != 14 /* R_68K_PLT16 */) {
				continue;
			}
			// btext and bname (see linker_symbol): their real addresses, in
			// .text's terms, so a far one also goes through the table.
			// etext, the module's end, isn't known yet: near only if even
			// the largest the module can be is in reach (maxend); else an
			// entry, filled in below.
			int ls = linker_symbol(rel->sym_ptr_ptr[0]);
			bfd_vma target = ls == LSYM_ETEXT ? ETEXT_TARGET
				: ls ? tvma - itext + (ls == LSYM_BNAME ? iname : 0) + rel->addend
				: bfd_asymbol_value(rel->sym_ptr_ptr[0]) + rel->addend;
			int64_t edisp = (int64_t)maxend + rel->addend - (int64_t)(itext + rel->address);
			int32_t disp = ls == LSYM_ETEXT ? (edisp > 0x7FFF ? 0x8000 : (int32_t)edisp)
				: (int32_t)(target - (tvma + rel->address));
			if(-0x8000 <= disp && disp <= 0x7FFF) {
				continue;
			}
			if(ls == LSYM_ETEXT && rel->addend != 0) {
				printf("far reference to %s%+ld at .text+%08lx: only etext itself can go through the jump table\n", rel->sym_ptr_ptr[0]->name, (long)rel->addend, rel->address);
				return 1;
			}
			// Done here, not with the near ones below.
			for(size_t k = 0; ls && k < ntls; k++) {
				if(tls[k].addr == rel->address) {
					tls[k].kind = LSYM_NONE;
				}
			}
			nfar++;
			if(rel->address < 2) {
				printf("far PC-relative reference at .text+%08lx can't be patched\n", rel->address);
				return 1;
			}
			unsigned char *ins = (unsigned char*)vec->buf + itext + rel->address - 2;
			uint16_t op = (ins[0] << 8) | ins[1];
			uint16_t newop;
			int extra;
			if(rel->address >= 4) {
				uint16_t prev = (ins[-2] << 8) | ins[-1];
				if((prev & 0xFFBF) == 0x4CBA || (prev & 0xFF3F) == 0x083A) {
					printf("far PC-relative reference at .text+%08lx (instruction %04x %04x) can't be patched: target %s\n", rel->address, prev, op, rel->sym_ptr_ptr[0]->name);
					return 1;
				}
			}
			if(op == 0x6100) { newop = 0x4EAE; extra = 0; }		// bsr.w -> jsr d(a6)
			else if(op == 0x6000) { newop = 0x4EEE; extra = 0; }		// bra.w -> jmp d(a6)
			else if((op & 0xF1FF) == 0x41FA) { newop = 0x206E | (op & 0x0E00); extra = 2; }	// lea d(pc),An -> movea.l d(a6),An
			else if(op == 0x487A) { newop = 0x2F2E; extra = 2; }		// pea d(pc) -> move.l d(a6),-(sp)
			else {
				printf("far PC-relative reference at .text+%08lx (instruction %04x) can't be patched: target %s\n", rel->address, op, rel->sym_ptr_ptr[0]->name);
				return 1;
			}
			size_t k;
			for(k = 0; k < ntargets && targets[k] != target; k++)
				;
			if(k == ntargets) {
				targets = realloc(targets, (ntargets + 1) * sizeof(bfd_vma));
				targets[ntargets++] = target;
			}
			bfd_vma evma = jt + 6 * k;
			int32_t d = (int32_t)(evma + extra);
			if(d < -0x8000 || 0x7FFF < d) {
				printf("_jmptbl entry %zu is out of a6's reach\n", k);
				return 1;
			}
			ins[0] = newop >> 8; ins[1] = newop;
			ins[2] = (uint16_t)d >> 8; ins[3] = (uint16_t)d;
		}
		if(ntargets) {
			size_t cap = have_jt && have_ejt && (int32_t)(ejt - jt) >= 0 ? (size_t)(int32_t)(ejt - jt) / 6 : 0;
			if(!have_jt || !have_ejt || ntargets > cap) {
				printf("%zu far calls need a jump table of %zu entries (%zu bytes): reserve _jmptbl to _ejmptbl in .data (now %zu entries)\n", nfar, ntargets, ntargets * 6, have_jt ? cap : 0);
				return 1;
			}
			// the entries, with their addresses relocated like pointers
			drefs = realloc(drefs, (ndrefs + ntargets) * sizeof(IdRef));
			for(size_t k = 0; k < ntargets; k++) {
				uint32_t eoff = DOFF(jt + 6 * k);
				unsigned char *e = (unsigned char*)vec->buf + iidata + eoff - idstart;
				if(eoff < idstart || eoff + 6 > idend) {
					puts("_jmptbl must be inside .data");
					return 1;
				}
				e[0] = 0x4E; e[1] = 0xF9;
				uint32_t t = targets[k];
				e[2] = t >> 24; e[3] = t >> 16; e[4] = t >> 8; e[5] = t;
				if(targets[k] == ETEXT_TARGET) {
					etext_entry = eoff + 2; // the address, filled in at the end
				}
				drefs[ndrefs].addr = eoff + 2;
				drefs[ndrefs].text = 1;
				ndrefs++;
			}
			qsort(drefs, ndrefs, sizeof(IdRef), cmp_idref);
			printf("_jmptbl: %zu of %zu entries used, for %zu far references\n", ntargets, cap, nfar);
		}
		free(targets);
	}

	// The pointers in l68's order (by object, the last first), from ld's
	// map; in address order without one. Within an object, by vsect, from
	// the .r68ord records r68togas and rof2elf write: each marks where a
	// vsect's initialized data ends (kinds 4 and 5), in the order of the source
	// (of the ROF's references); a pointer belongs to the first vsect
	// ending after it in its input section. Without records, address order.
	if(opt_ldmap) {
		InputSec *isecs;
		long nisecs = read_ldmap(opt_ldmap, &isecs);
		if(nisecs < 0) {
			printf("cannot read the map %s\n", opt_ldmap);
			return 1;
		}
		asection *ord = bfd_get_section_by_name(abfd, ".r68ord");
		bfd_byte *ob = NULL;
		size_t nord = 0;
		if(ord && bfd_section_size(ord) >= 10) {
			if(!bfd_malloc_and_get_section(abfd, ord, &ob)) {
				bfd_perror("could not read .r68ord");
				return 1;
			}
			nord = bfd_section_size(ord) / 10;
		}
		for(size_t i = 0; i < ndrefs; i++) {
			drefs[i].obj = INT_MAX; // in no object's data (the jump table): first
			drefs[i].vs = INT_MAX;
			for(long k = 0; k < nisecs; k++) {
				uint32_t st = DOFF((bfd_vma)isecs[k].start);
				if(drefs[i].addr >= st && drefs[i].addr - st < isecs[k].size) {
					drefs[i].obj = isecs[k].obj;
					for(size_t r = 0; r < nord; r++) {
						uint32_t end = DOFF(bfd_get_32(abfd, ob + r * 10 + 2));
						int kind = bfd_get_16(abfd, ob + r * 10);
						if((kind == 4 || kind == 5) && end > drefs[i].addr
						   && end > st && end - st <= isecs[k].size) {
							drefs[i].vs = (int)r;
							break;
						}
					}
					break;
				}
			}
		}
		free(ob);
		free(isecs);
		qsort(drefs, ndrefs, sizeof(IdRef), cmp_idref_obj);
	}

	// fix pointer in .data to 0-based
	for(size_t i = 0; i < ndrefs; i++) {
		uint32_t *p = (void*)(vec->buf + iidata + drefs[i].addr - idstart);
		uint32_t v = BE32(*p);
		if(drefs[i].text) {
			*p = BE32(v - tvma + itext); // .text is offsetted by module header etc.
		} else {
			// we expect .data, .bss and remote data are contiguous
			*p = BE32(v - dvma);
		}
	}

	// idrefs
	size_t iidrefs = has_prog ? make_idrefs(vec, drefs, ndrefs, 1) : 0;
	if(has_prog) make_idrefs(vec, drefs, ndrefs, 0);
	if(opt_nirefspad > 0) {
		if(!has_prog) {
			printf("--irefs-pad: the module has no M$IRefs table (--header)\n");
			return 1;
		}
		vec_append(vec, opt_irefspad, opt_nirefspad);
	}

	// crc, with pad on head
	size_t icrc = opt_raw ? 0 : vec_zero(vec, 4);

	// The header, as far as it reaches (--header), into a separate buffer:
	// a short header's fields would otherwise land on the code.
	struct modheader mhbuf;
	struct modheader *mh = &mhbuf;
	memset(mh, 0, sizeof(*mh));
	mh->magic = BE16(0x4AFC);
	mh->sysrev = BE16(0x0001);
	mh->size = BE32(vec->len);
	mh->owner = BE32(owner);
	mh->name = BE32(iname);
	mh->accs = BE16(opt_perms >= 0 ? opt_perms : 0x0555); // r-xr-xr-x by default
	uint16_t typelang = opt_typelang >= 0 ? (uint16_t)opt_typelang : s_typelang ? (uint16_t)bfd_asymbol_value(s_typelang) : 0x0101;
	uint16_t attrev = s_sysattr ? bfd_asymbol_value(s_sysattr) : s_attrev ? bfd_asymbol_value(s_attrev) : 0x8000 | DEFAULT_REVS;
	mh->type = typelang >> 8; // prgm by default
	mh->lang = typelang & 0xFF; // objct by default
	mh->attr = (attrev >> 8) | (opt_sticky ? 0x40 : 0); // reentrant by default
	mh->revs = opt_revs >= 0 && !s_sysattr ? opt_revs : (attrev & 0xFF);
	mh->edit = BE16(s_sysedit ? (int)bfd_asymbol_value(s_sysedit) : opt_edit >= 0 ? opt_edit : s_edition ? (int)bfd_asymbol_value(s_edition) : DEFAULT_EDIT); // any
	mh->usage = BE32(0); // reserved
	mh->symbol = BE32(0); // reserved
	uint16_t parity = 0xFFFF;
	for(int i = 0; i < 0x2e / 2; i++) {
		parity ^= ((uint16_t*)mh)[i];
	}
	mh->parity = parity; // don't BE16, already swapped

	mh->exec = BE32(itext + entryoff);
	mh->excpt = BE32(s_trapent ? itext + bfd_asymbol_value(s_trapent) - tvma : 0); // uninitialized trap entry
	mh->mem = BE32(dend);
	mh->stack = BE32((uint32_t)stack);
	mh->idata = BE32(iidatahdr);
	mh->irefs = BE32(iidrefs);
	if(!opt_raw) {
		memcpy(vec->buf, mh, opt_header);
	}

	// etext, as both Microware linkers define it (Ultra C's and C 3.2's l68,
	// checked 2026-10-08): the module's size, plus 4 when exactly one of the
	// two initialized data reference tables (pointers to code, pointers to
	// data) has entries; exact when both or neither have.
	size_t ncoderefs = 0;
	for(size_t i = 0; i < ndrefs; i++) ncoderefs += drefs[i].text != 0;
	size_t etextoff = vec->len + ((ncoderefs > 0) != (ndrefs - ncoderefs > 0) ? 4 : 0);

	// References to btext, bname and etext (see linker_symbol), now that
	// the module's layout and size are known.
	for(size_t i = 0; i < ntls + ndls; i++) {
		LsRef *r = i < ntls ? &tls[i] : &dls[i - ntls];
		if(r->kind == LSYM_NONE) {
			continue; // a far one, through the jump table
		}
		int64_t off = r->kind == LSYM_BTEXT ? 0 : r->kind == LSYM_BNAME ? (int64_t)iname : (int64_t)etextoff;
		if(i < ntls && (r->type == 1 /* R_68K_32 */ || r->type == 2 /* R_68K_16 */)) {
			// absolute: the offset in the module (no relocation at load)
			int64_t v = off + r->addend;
			unsigned char *p = (unsigned char*)vec->buf + itext + r->addr;
			if(r->type == 2) {
				if(v < -0x8000 || v > 0xFFFF) {
					printf("R_68K_16 at .text+%08x: the module symbol's offset %lld doesn't fit\n", r->addr, (long long)v);
					return 1;
				}
				p[0] = v >> 8; p[1] = v;
			} else {
				p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
			}
		} else if(i < ntls) {
			// PC-relative: from the field itself, as GNU ld computes it.
			int64_t v = off + r->addend - (int64_t)(itext + r->addr);
			unsigned char *p = (unsigned char*)vec->buf + itext + r->addr;
			if(r->type == 5 /* R_68K_PC16 */) {
				if(v < -0x8000 || v > 0x7FFF) {
					// Can't happen: far ones went through the jump table,
					// and etext was judged by the largest module size.
					printf("R_68K_PC16 at .text+%08x: %s beyond 32K (internal error)\n", r->addr, r->kind == LSYM_ETEXT ? "etext" : "a module symbol");
					return 1;
				}
				p[0] = v >> 8; p[1] = v;
			} else {
				p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
			}
		} else {
			uint32_t *p = (void*)(vec->buf + iidata + r->addr - idstart);
			*p = BE32((uint32_t)(off + r->addend));
		}
	}
	if(etext_entry >= 0) {
		uint32_t *p = (void*)(vec->buf + iidata + etext_entry - idstart);
		*p = BE32((uint32_t)etextoff);
	}
	free(tls);
	free(dls);

	// update crc
	uint32_t crc = 0;
	if(opt_raw) {
		// What l68 -r reports.
		printf("Code size: %lu\nData area size: %lu\n", (unsigned long)tsize, (unsigned long)dend);
	} else {
		crc = module_crc(vec->buf, vec->len - 3);
		*(uint32_t*)(vec->buf + icrc) = BE32(crc);
	}

	// Symbol module (this fork, -g): an OS-9 data module named after the
	// program module plus ".stb", as Microware's linker writes with -g
	// (format in the OS-9/68000 User-State Debugger manual, appendix A):
	// the STB format number, the program module's CRC, then the global
	// symbols sorted by value (code: offsets in the module; data: a6-
	// relative, i.e. their VMAs), then their names. The header's symbol
	// field points to the STB header. The symbols the linker defines are
	// added, flagged 0x2000: Microware C 3.2's l68 defines btext, bname,
	// etext, end and _jmptbl, and also flags the symbols that other psects
	// refer to (here: that relocations refer to); Ultra C's l68 also
	// defines _btext, _bname, _etext, _bdata, bdata and _enddata.
	if(opt_stb) {
		bfd_vma jmptbl = dvma + dend;
		for(long i = 0; i < nsyms; i++) {
			if(!strcmp(bfd_asymbol_name(syms[i]), "_jmptbl")) jmptbl = bfd_asymbol_value(syms[i]);
		}
		const StbSym ucclinker[] = {
			{ (int32_t)dvma, 0x2001, "_bdata", 0 }, { (int32_t)dvma, 0x2001, "bdata", 1 },
			{ (int32_t)(dvma + dend), 0x2001, "_enddata", 2 }, { (int32_t)(dvma + dend), 0x2001, "end", 3 },
			{ (int32_t)jmptbl, 0x2001, "_jmptbl", 4 },
			{ 0, 0x2004, "_btext", 5 }, { 0, 0x2004, "btext", 6 },
			{ (int32_t)iname, 0x2004, "_bname", 7 }, { (int32_t)iname, 0x2004, "bname", 8 },
			{ (int32_t)etextoff, 0x2004, "_etext", 9 }, { (int32_t)etextoff, 0x2004, "etext", 10 },
		};
		const StbSym c32linker[] = {
			{ (int32_t)(dvma + dend), 0x2001, "end", 0 },
			{ (int32_t)jmptbl, 0x2001, "_jmptbl", 1 },
			{ 0, 0x2004, "btext", 2 },
			{ (int32_t)iname, 0x2004, "bname", 3 },
			{ (int32_t)etextoff, 0x2004, "etext", 4 },
		};
		const StbSym *linker = opt_stb == 2 ? ucclinker : c32linker;
		const size_t nlinker = opt_stb == 2 ? sizeof(ucclinker) / sizeof(ucclinker[0]) : sizeof(c32linker) / sizeof(c32linker[0]);
		const char *const reserved[] = { "_bdata", "bdata", "_enddata", "end", "_jmptbl", "_btext", "btext", "_bname", "bname", "_etext", "etext", "_ejmptbl", "__jmptbl_size" };
		stb_reverse = opt_stb == 1;
		StbSym *ss = malloc((nsyms + nlinker) * sizeof(StbSym));
		memcpy(ss, linker, nlinker * sizeof(StbSym));
		// Ultra C's l68 lists the program's symbols before its own at the same
		// value (a variable at the data area's start before _bdata; checked
		// 2026-10-08): its own rank after them.
		if(opt_stb == 2) {
			for(size_t k = 0; k < nlinker; k++) ss[k].rank += 1;
		}
		size_t nss = nlinker;
		for(long i = 0; i < nsyms; i++) {
			asymbol *s = syms[i];
			asection *sec = s->section;
			uint16_t type;
			if(!(s->flags & BSF_GLOBAL) || !strncmp(bfd_asymbol_name(s), "__os9_", 6)) continue;
			// the link-time expressions and section aliases r68togas and rof2elf make
			if(!strncmp(bfd_asymbol_name(s), "r68x_", 5) || !strncmp(bfd_asymbol_name(s), "r68l_", 5)) continue;
			size_t k, nres = sizeof(reserved) / sizeof(reserved[0]);
			for(k = 0; k < nres && strcmp(bfd_asymbol_name(s), reserved[k]); k++);
			if(k < nres) continue; // the linker's: as above, or not listed
			if(sec == text) type = 4;
			else if(sec == data) type = 1;
			else if(sec == bss) type = 0;
			else if(sec && sec == rdata) type = 3; // initialized remote, as l68
			else if(sec && sec == rbss) type = 2;
			else continue; // equates: both l68s list none (checked 2026-10-12 with -g)
			ss[nss].value = type == 4 ? (int32_t)(itext + bfd_asymbol_value(s) - tvma) : (int32_t)bfd_asymbol_value(s);
			ss[nss].type = type | (opt_stb == 1 && refd[i] ? 0x2000 : 0);
			ss[nss].name = bfd_asymbol_name(s);
			ss[nss].rank = opt_stb == 2 ? 0 : nlinker;
			nss++;
		}
		qsort(ss, nss, sizeof(StbSym), cmp_stbsym);

		Vec *sv = vec_new(0x1000);
		vec_zero(sv, 0x30); // data module header
		char *sname = malloc(strlen(opt_name) + 5);
		sprintf(sname, "%s.stb", opt_name);
		size_t isname = vec_append(sv, sname, strlen(sname) + 1);
		while(sv->len % 2) vec_zero(sv, 1); // word fields must be at even offsets
		size_t istb = vec_zero(sv, 2 + 4 + 4 + 4);
		while(sv->len % 16) vec_zero(sv, 1);
		size_t ient = vec_zero(sv, nss * 10);
		unsigned char *h = (unsigned char*)sv->buf + istb;
		h[0] = 0x01; h[1] = 0x00; // STB format 0x0100
		h[2] = crc >> 24; h[3] = crc >> 16; h[4] = crc >> 8; h[5] = crc;
		h[6] = ient >> 24; h[7] = ient >> 16; h[8] = ient >> 8; h[9] = ient;
		h[10] = nss >> 24; h[11] = nss >> 16; h[12] = nss >> 8; h[13] = nss;
		for(size_t i = 0; i < nss; i++) {
			size_t iname = vec_append(sv, (void*)ss[i].name, strlen(ss[i].name) + 1);
			unsigned char *e = (unsigned char*)sv->buf + ient + i * 10;
			uint32_t v = ss[i].value;
			e[0] = v >> 24; e[1] = v >> 16; e[2] = v >> 8; e[3] = v;
			e[4] = ss[i].type >> 8; e[5] = ss[i].type;
			e[6] = iname >> 24; e[7] = iname >> 16; e[8] = iname >> 8; e[9] = iname;
		}
		while(sv->len % 2) vec_zero(sv, 1);
		size_t iscrc = vec_zero(sv, 4);

		struct modheader *sh = (void*)(sv->buf + 0);
		sh->symbol = BE32(istb);
		sh->magic = BE16(0x4AFC);
		sh->sysrev = BE16(0x0001);
		sh->owner = BE32(owner);
		sh->accs = BE16(0x0555);
		sh->type = 4; // data
		sh->lang = 0;
		sh->attr = 0x80;
		sh->revs = mh->revs;
		sh->edit = mh->edit;
		finish_module(sv, isname, iscrc);

		// OUTFILE.stb, or in the STB directory next to OUTFILE if there's one
		// (as l68).
		char *sfile = malloc(strlen(argv[1]) + 9);
		const char *base = strrchr(argv[1], '/');
		base = base ? base + 1 : argv[1];
		sprintf(sfile, "%.*sSTB", (int)(base - argv[1]), argv[1]);
		struct stat st;
		if(!stat(sfile, &st) && S_ISDIR(st.st_mode)) sprintf(sfile, "%.*sSTB/%s.stb", (int)(base - argv[1]), argv[1], base);
		else sprintf(sfile, "%s.stb", argv[1]);
		FILE *sf = fopen(sfile, "w+b");
		if(!sf || !fwrite(sv->buf, sv->len, 1, sf)) {
			perror("could not write symbol module");
			return 1;
		}
		fclose(sf);

		// The debug information (this fork, -g): where a psect has some
		// (C 3.2's c68 -g; r68togas's and rof2elf's .r68dbg, which the
		// link puts together in link order), OUTFILE.dbg next to the
		// symbol module, as l68 -g writes it (measured 2026-10-17): C 3.2's
		// $BEEFCAFE, a zero word, the program module's CRC as a long, the
		// psects' debug areas; Ultra C's $CAFEBEEF, 3, the CRC, a zero
		// long, the areas. A reference to a code label holds its module
		// offset, to data its a6-relative offset (its VMA, as the link
		// put it).
		asection *dbg = bfd_get_section_by_name(abfd, ".r68dbg");
		if(dbg && bfd_section_size(dbg) > 0) {
			bfd_size_type gsize = bfd_section_size(dbg);
			unsigned char *g = malloc(gsize);
			if(!g || !bfd_get_section_contents(abfd, dbg, g, 0, gsize)) {
				bfd_perror("could not read .r68dbg");
				return 1;
			}
			long grelsz = bfd_get_reloc_upper_bound(abfd, dbg);
			arelent **grels = grelsz > 0 ? malloc(grelsz) : NULL;
			long ngrels = grels ? bfd_canonicalize_reloc(abfd, dbg, grels, syms) : 0;
			for(long i = 0; i < ngrels; i++) {
				arelent *rel = grels[i];
				asection *ssec = rel->sym_ptr_ptr[0]->section;
				int size = rel->howto->type == 1 ? 4 : rel->howto->type == 2 ? 2 : rel->howto->type == 3 ? 1 : 0;
				if(bfd_is_und_section(ssec)) {
					printf("undefined symbol %s (.r68dbg+%08lx)\n", rel->sym_ptr_ptr[0]->name, rel->address);
					return 1;
				}
				if(ssec != text)
					continue; // data, equates: as the link resolved them
				if(!size || rel->address + size > gsize) {
					printf("%s at .r68dbg+%08lx: not supported\n", rel->howto->name, rel->address);
					return 1;
				}
				int64_t off = (int64_t)(bfd_asymbol_value(rel->sym_ptr_ptr[0]) + rel->addend) - (int64_t)tvma + (int64_t)itext;
				for(int k = 0; k < size; k++)
					g[rel->address + k] = (unsigned char)((uint64_t)off >> (8 * (size - 1 - k)));
			}
			free(grels);
			unsigned char gh[14];
			size_t ghn = opt_stb == 2 ? 14 : 10;
			memset(gh, 0, sizeof gh);
			if(opt_stb == 2) {
				gh[0] = 0xCA; gh[1] = 0xFE; gh[2] = 0xBE; gh[3] = 0xEF; gh[5] = 3;
			} else {
				gh[0] = 0xBE; gh[1] = 0xEF; gh[2] = 0xCA; gh[3] = 0xFE;
			}
			gh[6] = crc >> 24; gh[7] = crc >> 16; gh[8] = crc >> 8; gh[9] = crc;
			char *gfile = malloc(strlen(sfile) + 1);
			strcpy(gfile, sfile);
			strcpy(gfile + strlen(gfile) - 4, ".dbg");
			FILE *gf = fopen(gfile, "w+b");
			if(!gf || fwrite(gh, 1, ghn, gf) != ghn || fwrite(g, 1, gsize, gf) != gsize) {
				perror("could not write the debug information");
				return 1;
			}
			fclose(gf);
			free(gfile);
			free(g);
		}
	}

	// write
	FILE *fp = fopen(argv[1], "w+b");
	if(!fp) {
		perror("could not open output file");
		return 1;
	}
	if(!fwrite(vec->buf, vec->len, 1, fp)) {
		perror("could not write output file");
		return 1;
	}
	fclose(fp);

	return 0;
}

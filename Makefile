# MIT License
#
# Copyright (c) 2021 Murachue
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

BFDPATH=/usr/x86_64-pc-linux-gnu/m68k-elfos9
DESTDIR=/usr
EXE=elf2mod
EXE_ROF2ELF=rof2elf
EXE_ELF2ROF=elf2rof
# Source translators between Microware's r68 and gas
TRANSLATORS=r68togas gastor68
# The front ends for Microware's tools (xcc, l68, ...), on code they share;
# libiberty runs the programs
FRONTENDS=m68k-os9-gcc m68k-os9-xcc m68k-os9-l68 m68k-os9-libgen m68k-os9-r68
FE_SRCS=os9conv.c os9link.c hostsys.c r68x_note.c
FE_HDRS=os9conv.h os9link.h hostsys.h r68x_note.h
LIBIBERTY_CFLAGS=-I/usr/include/libiberty
# These binutils of m68k-elfos9 as m68k-os9- too, so that
# build systems find a complete toolchain (./configure --host=m68k-os9)
OS9_TOOLS=ar as ld nm objcopy objdump ranlib readelf size strip

CC=gcc
CFLAGS=-g -I$(BFDPATH)/include -Wall -Wextra
LDFLAGS=
LIBS=$(BFDPATH)/lib/libbfd.a -liberty -lz -ldl
INSTALL=install
RM=rm

.PHONY: all clean

all: $(EXE) $(EXE_ROF2ELF) $(EXE_ELF2ROF) $(TRANSLATORS) $(FRONTENDS)

install: $(EXE) $(EXE_ROF2ELF) $(EXE_ELF2ROF) $(TRANSLATORS) $(FRONTENDS)
	$(INSTALL) -s $(EXE) $(DESTDIR)/bin/$(EXE)
	$(INSTALL) -s $(EXE_ROF2ELF) $(DESTDIR)/bin/$(EXE_ROF2ELF)
	$(INSTALL) -s $(EXE_ELF2ROF) $(DESTDIR)/bin/$(EXE_ELF2ROF)
	for t in $(TRANSLATORS) $(FRONTENDS); do $(INSTALL) -s $$t $(DESTDIR)/bin/$$t; done
	for t in $(OS9_TOOLS); do ln -sf m68k-elfos9-$$t $(DESTDIR)/bin/m68k-os9-$$t; done

clean:
	-$(RM) $(EXE) $(EXE_ROF2ELF) $(EXE_ELF2ROF) $(TRANSLATORS) $(FRONTENDS)

$(EXE): elf2mod.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

# rof2elf and elf2rof need no BFD.
$(EXE_ROF2ELF): rof2elf.c r68x_note.c r68x_note.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< r68x_note.c

$(EXE_ELF2ROF): elf2rof.c r68x_note.c r68x_note.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< r68x_note.c

$(TRANSLATORS): %: %.c r68x_note.c r68x_note.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< r68x_note.c

$(FRONTENDS): %: %.c $(FE_SRCS) $(FE_HDRS)
	$(CC) $(CFLAGS) $(LIBIBERTY_CFLAGS) $(LDFLAGS) -o $@ $< $(FE_SRCS) -liberty

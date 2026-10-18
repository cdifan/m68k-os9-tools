| C names that are r68 register names (a variable d0, an external a1, a function sp): written
| with r68's label designator =, which makes them the symbols in both Microware assemblers
| (tested 2026-10-18), where bare they would be the registers. A label of the name stays as it
| is (an r68 label may be named so).
| fixed: 08be2bc 2026-10-19 r68togas, gastor68: round trip closer to the source; -bt with spanreg; -mspanreg
| expect: lea =d0(a6),a0
| expect: add.l =a1(a6),a0
| expect: lea =sp(pc),a1
| expect: dc.l =a1
	.text
	.globl	f
f:
	lea d0(%a6),%a0
	move.l (%a0),%a0
	add.l a1(%a6),%a0
	lea (sp:w,%pc),%a1
	jsr (%a1)
	rts
	.globl	p
p:
	.long	a1
	.section	.bss
	.globl	d0
d0:
	.zero	4

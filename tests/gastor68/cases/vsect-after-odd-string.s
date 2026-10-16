| A static's initialized data between two strings of odd length in the code (GCC writes a
| function's strings in .text): the vsects go after the code, at the end of the psect, so that
| the code has no section switch after an odd dc.b; there m68k-os9-r68's translation (gas, MRI
| mode) aligned the code, which gas and both r68s don't (the second string at 3, not 4).
| fixed: 16b22ab 2026-10-16 gastor68: vsects after the code
| expect: dc.b "cd",0
	.text
.LC0:
	.string	"ab"
	.data
	.align	2
	.type	n, @object
	.size	n, 4
n:
	.long	5
	.text
.LC1:
	.string	"cd"
	.align	2
	.globl	f
f:
	lea .LC0(%pc),%a0
	lea .LC1(%pc),%a1
	move.l n(%a6),%d0
	rts

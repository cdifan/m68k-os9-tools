| GCC writes a floating-point immediate's hex without leading zeros (1e-300 as
| #0x1a56e1fc2f8f359); r68 takes such digits from the left, so gastor68 writes all of them
| (r68's value was 8.6e-182). An integer size stays as written.
| fixed: fe4cba3 2026-11-01 r68togas, gastor68: floating-point hex immediates as r68 reads them
| cpu: 68020
| expect: fmul.d #$01a56e1fc2f8f359,fp0
| expect: fmove.s #$00800000,fp1
| expect: fmove.l #$1,fp0
	.text
	.globl	f
f:
	fmul.d #0x1a56e1fc2f8f359,%fp0
	fmove.s #0x800000,%fp1
	fmove.l #0x1,%fp0
	rts

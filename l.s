#include "mem.h"
#include "io.h"

#define PUTC(c)\
	ADD $0x14, R8;\
	MOVW (R8), R9;\
	AND $(1<<5), R9;\
	CMP $0, R9;\
	BEQ -3(PC);\
	MOVW $(c), R9;\
	ADD $-0x14, R8;\
	MOVW R9, (R8);\

#define PUTC2(c)\
	MOVW $(c), R0;\
	MOVW R0, (R8);\

#define DUMPDIGIT(Rx, d)\
	MOVW Rx, R9;\
	MOVW R9>>d, R9;\
	AND $0xf, R9;\
	CMP $9, R9;\
	ADD.GT $7, R9;\
	ADD $'0', R9;\
	MOVW R9, (R8);\

#define DUMPREG(Rx)\
	DUMPDIGIT(R(Rx), 28)\
	DUMPDIGIT(R(Rx), 24)\
	DUMPDIGIT(R(Rx), 20)\
	DUMPDIGIT(R(Rx), 16)\
	DUMPDIGIT(R(Rx), 12)\
	DUMPDIGIT(R(Rx), 8)\
	DUMPDIGIT(R(Rx), 4)\
	DUMPDIGIT(R(Rx), 0)\

TEXT _start(SB), $-4
	MOVW $(KTZERO-KZERO+DRAM_BASE), R13
	MOVW $(UART_BASE), R8

	PUTC('P')
	MOVW $0, R0
	MOVW $DRAM_BASE, R1
	MOVW $(CONFADDR-KZERO+DRAM_BASE), R2
_start0:
	MOVW.P R0, 4(R1)
	CMP.S R1, R2
	BNE _start0

	PUTC('l')
	MOVW $(SECSZ), R0
	MOVW $(MACHL1(0)-KZERO+DRAM_BASE), R4
	MOVW $KZERO, R1
	ADD R1>>(SECSH-2), R4, R1
	MOVW $((DRAM_BASE&0xffe00000)|L1SEC|L1NORMAL), R2
	MOVW $(KTOP-KZERO+DRAM_BASE), R3
_start1:
	MOVW.P R2, 4(R1)
	ADD R0, R2
	CMP.S R2, R3
	BGE _start1

	PUTC('a')
	MOVW $L2SZ, R0
	MOVW $VMAP, R1
	ADD R1>>(SECSH-2), R4, R1
	MOVW $((VMAPL2-KZERO+DRAM_BASE)|L1PT), R2
	MOVW $(VMAPL2-KZERO+DRAM_BASE+VMAPL2SZ), R3
_start2:
	MOVW.P R2, 4(R1)
	ADD R0, R2
	CMP.S R2, R3
	BGE _start2

	MOVW $(UART_BASE|L2VALID|L2DEVICE|L2KERRW), R0
	MOVW $(VMAPL2-KZERO+DRAM_BASE), R1
	MOVW R0, (R1)

	PUTC('n')

	MOVW $(MACH(0)-KZERO+DRAM_BASE), R(Rmach)
_start3:
	/* enable MMU permission checking */
	MOVW $0x55555555, R0
	MCR 15, 0, R0, C(3), C(0), 0

	/* invalidate inst/data tlbs */
	MOVW $0, R0
	MCR 15, 0, R0, C(8), C(7), 0
	DSB

	/* set TTBR0 */
	ORR $TTBATTR, R4, R1
	MCR 15, 0, R1, C(2), C(0), 0

	/* set TTBCR */
	MOVW $0, R1
	MCR 15, 0, R1, C(2), C(0), 2

	/* errata 709718 */
	MRC 15, 0, R1, C(10), C(2), 0
	BIC $(1<<17|1<<16), R1
	MCR 15, 0, R1, C(10), C(2), 0

	MRC 15, 0, R1, C(1), C(0), 0
	ORR $(1<<29), R1		/* enable access flags */
	BIC $(1<<28), R1		/* disable TEX remap */
	ORR $1, R1				/* enable mmu */
	MOVW $_virt(SB), R2
	PUTC(' ')
	MCR 15, 0, R1, C(1), C(0), 0
	MOVW R2, R15

TEXT _virt(SB), $-4
	DSB
	ISB

	ADD $(KZERO-DRAM_BASE), R(Rmach)
	MOVW R(Rmach), R13
	ADD $MACHSIZE, R13

	MOVW R(Rmach), R0
	ADD $12, R0
	BL loadsp(SB)

	MOVW $vectors(SB), R1
	MCR 15, 0, R1, C(12), C(0), 0

	/* enable maths coprocessors in CPACR but disable them in FPEXC */
	MRC 15, 0, R0, C(1), C(0), 2
	ORR $(15<<20), R0
	MCR 15, 0, R0, C(1), C(0), 2

	VMRS(0xe, FPEXC, 0)
	BIC $(3<<30), R0
	VMSR(0xe, 0, FPEXC)

	/* clear L1 cache */
	MOVW $0, R0
	MCR 15, 0, R0, C(7), C(5), 0 /* invalidate instruction caches */
	MCR 15, 0, R0, C(7), C(5), 6 /* invalidate brach preditor array */
	BL l1dclear(SB)

	/* disable L1 hw alias support, enable L2 cache, IBE */
	MRC 15, 0, R0, C(1), C(0), 1
	ORR $(1<<0|1<<1|1<<6), R0
	BIC $(1<<1), R0
	MCR 15, 0, R0, C(1), C(0), 1

	/* enable instruction caching, branch prediction, data-caching */
	MRC 15, 0, R0, C(1), C(0), 0
	ORR $(1<<12|1<<11|1<<2), R0
	BIC $(1<<2), R0
	MCR 15, 0, R0, C(1), C(0), 0

	DSB
	ISB

	MOVW $(VMAP), R8
	PUTC('9')

	/* kernel Mach* in TPIDRPRW */
	MCR 15, 0, R(Rmach), C(13), C(0), 4

	MOVW $setR12(SB), R12
	MOVW $0, R(Rup)

	BL main(SB)
	B idlehands(SB)

	BL _div(SB) /* hack to load _div */

TEXT touser(SB), $-4
	CPS(CPSID)

	SUB $12, R13
	MOVW R0, (R13)
	MOVW $0, R1
	MOVW R1, 4(R13)
	MOVW $(UTZERO+0x20), R1
	MOVW R1, 8(R13)

	MOVW CPSR, R1
	BIC $(PsrMask|PsrDirq|PsrDfiq), R1
	ORR $PsrMusr, R1
	MOVW R1, SPSR

	MOVW $(KTZERO-(15*4)), R0
	MOVM.IA (R0), [R0-R12]
	MOVM.IA.S (R13), [R13-R14]
	ADD $8, R13
	MOVM.IA.W.S (R13), [R15]

TEXT forkret(SB), $-4
	MOVW (16*4)(R13), R0
	MOVW R0, SPSR
	MOVM.IA.W (R13), [R0-R12]
	MOVM.IA.S (R13), [R13-R14]
	ADD $16, R13
	DSB
	ISB
	MOVM.IA.W.S (R13), [R15]

TEXT loadsp(SB), $0
	CPS(CPSMODE | PsrMabt)
	MOVW R0, R13
	CPS(CPSMODE | PsrMund)
	MOVW R0, R13
	CPS(CPSMODE | PsrMirq)
	MOVW R0, R13
	CPS(CPSMODE | PsrMfiq)
	MOVW R0, R13
	CPS(CPSMODE | PsrMsvc)
	RET

TEXT tas(SB), $0
TEXT _tas(SB), $0
	MOVW $0xDEADDEAD, R2
_tas1:
	LDREX (R0), R1
	STREX R2, (R0), R3
	CMP.S $0, R3
	BNE _tas1
	MOVW R1, R0
	DMB
	RET

TEXT coherence(SB), $0
	DSB
	RET

TEXT idlehands(SB), $0
	DSB
	WFE
	RET

TEXT ttbget(SB), $0
	MRC 15, 0, R0, C(2), C(0), 0
	BIC $0x7f, R0
	RET

TEXT ttbput(SB), $0
	ORR $TTBATTR, R0
	MCR 15, 0, R0, C(2), C(0), 0
	RET

TEXT flushpg(SB), $0
	MCR 15, 0, R0, C(8), C(7), 0
	DSB
	RET

TEXT flushtlb(SB), $0
	MCR 15, 0, R0, C(8), C(5), 0
	MCR 15, 0, R0, C(8), C(6), 0
	MCR 15, 0, R0, C(8), C(7), 0
	DSB
	RET

TEXT setasid(SB), $0
	DSB	/* errata */
	MCR 15, 0, R0, C(13), C(0), 1
	RET

TEXT spllo(SB), $-4
	MOVW CPSR, R0
	CPS(CPSIE)
	RET

TEXT splhi(SB), $-4
	MOVW R14, 4(R(Rmach))
	MOVW CPSR, R0
	CPS(CPSID)
	RET

TEXT splx(SB), $-4
	MOVW R14, 4(R(Rmach))
	MOVW R0, R1
	MOVW CPSR, R0
	MOVW R1, CPSR
	RET

TEXT islo(SB), $0
	MOVW CPSR, R0
	AND $(PsrDirq), R0
	EOR $(PsrDirq), R0
	RET

TEXT setlabel(SB), $-4
	MOVW R13, 0(R0)
	MOVW R14, 4(R0)
	MOVW $0, R0
	RET

TEXT gotolabel(SB), $-4
	MOVW 0(R0), R13
	MOVW 4(R0), R14
	MOVW $1, R0
	RET

TEXT cas(SB), $0
TEXT cmpswap(SB), $0
	MOVW ov+4(FP), R1
	MOVW nv+8(FP), R2
spincas:
	LDREX (R0), R3
	CMP.S	R3, R1
	BNE	fail
	STREX	R2, (R0), R4
	CMP.S	$0, R4
	BNE	spincas
	MOVW	$1, R0
	DMB
	RET
fail:
	CLREX
	MOVW	$0, R0
	RET

TEXT perfticks(SB), $0
	MRC 15, 0, R0, C(9), C(13), 0
	RET

TEXT cycles(SB), $0
	MRC 15, 0, R1, C(9), C(13), 0
	MOVW R1, (R0)
	MOVW 24(R(Rmach)), R1
	MRC 15, 0, R2, C(9), C(12), 3
	AND.S $(1<<31), R2
	BEQ _cycles0
	MCR 15, 0, R2, C(9), C(12), 3
	ADD $1, R1
	MOVW R1, 24(R(Rmach))
_cycles0:
	MOVW R1, 4(R0)
	RET

TEXT fpinit(SB), $0
	MOVW $(1<<30), R0
	VMSR(0xe, 0, FPEXC)
	MOVW $0, R0
	VMSR(0xe, 0, FPSCR)
	RET

TEXT fprestore(SB), $0
	MOVM.IA.W (R0), [R1-R2]
	VMSR(0xe, 1, FPEXC)
	VMSR(0xe, 2, FPSCR)
	WORD $0xecb00b20
	WORD $0xecf00b20
	RET

TEXT fpsave(SB), $0
	VMRS(0xe, FPEXC, 1)
	VMRS(0xe, FPSCR, 2)
	MOVM.IA.W [R1-R2], (R0)
	WORD $0xeca00b20
	WORD $0xece00b20
	/* wet floor */

TEXT fpoff(SB), $0
TEXT fpclear(SB), $0
	MOVW $0, R1
	VMSR(0xe, 1, FPEXC)
	RET

TEXT getifar(SB), $0
	MRC 15, 0, R0, C(6), C(0), 2
	RET

TEXT getdfar(SB), $0
	MRC 15, 0, R0, C(6), C(0), 0
	RET

TEXT getifsr(SB), $0
	MRC 15, 0, R0, C(5), C(0), 1
	RET

TEXT getdfsr(SB), $0
	MRC 15, 0, R0, C(5), C(0), 0
	RET

TEXT getexcvec(SB), $0
	MRC 15, 0, R0, C(12), C(0), 0
	RET

#define Rnoway R1
#define Rwayinc R2
#define Rmaxway R3
#define Rsetinc R4
#define Rmaxset R5

TEXT l1dclear(SB), $0
	MOVW $0, R0
	MCR 15, 2, R0, C(0), C(0), 0 /* select data cache */
	MRC 15, 1, R9, C(0), C(0), 0 /* get cache size(s) */
	AND $7, R9, R8
	ADD $4, R8
	MOVW $1, Rsetinc
	MOVW Rsetinc<<R8, Rsetinc
	MOVW R9>>13, Rmaxset
	AND $0x7fff, Rmaxset
	MOVW Rmaxset<<R8, Rmaxset

	MOVW R9>>3, R0
	AND $0x3ff, R0
	MOVW $(1<<31), Rwayinc
	MOVW $(1<<31), Rnoway
	MOVW R0, Rmaxway
	ADD $1, R0
_l1dclear0:
	MOVW.S R0>>1, R0
	BEQ _l1dclear1
	MOVW Rwayinc>>1, Rwayinc
	MOVW Rnoway->1, Rnoway
	MOVW Rmaxway@>1, Rmaxway
	B _l1dclear0
_l1dclear1:
	MOVW Rwayinc<<1, Rwayinc
	MVN Rnoway<<1, Rnoway
	BIC Rnoway, Rmaxway

	MOVW $0, R0
_l1dclear2:
	MCR 15, 0, R0, C(7), C(14), 2
	ADD Rwayinc, R0
	CMP.S Rmaxway, R0
	BLT _l1dclear2
	AND Rnoway, R0
	ADD Rsetinc, R0
	CMP.S Rmaxset, R0
	BLT _l1dclear2

	MOVW $2, R0
_l1dclear3:
	MCR 15, 0, R0, C(7), C(14), 2
	ADD Rwayinc, R0
	CMP.S Rmaxway, R0
	BLT _l1dclear3
	AND Rnoway, R0
	ADD Rsetinc, R0
	CMP.S Rmaxset, R0
	BLT _l1dclear3
	RET

TEXT invalise(SB), $0
	MOVW 4(FP), R1
	ADD $(LINSIZ - 1), R1
	BIC $(LINSIZ - 1), R0
	BIC $(LINSIZ - 1), R1
_invalise0:
	MCR 15, 0, R0, C(7), C(5), 1
	ADD $LINSIZ, R0
	CMP.S R1, R0
	BLT _invalise0	
	RET

TEXT cleandse(SB), $0
	DSB
	MOVW 4(FP), R1
	ADD $(LINSIZ - 1), R1
	BIC $(LINSIZ - 1), R0
	BIC $(LINSIZ - 1), R1
_cleandse0:
	MCR 15, 0, R0, C(7), C(10), 1
	ADD $LINSIZ, R0
	CMP.U R1, R0
	BLT _cleandse0
	DSB
	RET
	
TEXT invaldse(SB), $0
	MOVW 4(FP), R1
	ADD $(LINSIZ - 1), R1
	BIC $(LINSIZ - 1), R0
	BIC $(LINSIZ - 1), R1
_invaldse0:
	MCR 15, 0, R0, C(7), C(6), 1
	ADD $LINSIZ, R0
	CMP.S R1, R0
	BLT _invaldse0
	MCR 15, 0, R0, C(7), C(5), 0
	DSB
	RET

TEXT va2pa(SB), $0
	MCR 15, 0, R0, C(7), C(8), 1
	DSB
	MRC 15, 0, R0, C(7), C(4), 0
	RET

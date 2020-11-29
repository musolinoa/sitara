#include "mem.h"
#include "io.h"

#define WDT_BASE 0x44E35000
#define WDT_WWPS (WDT_BASE+0x34)
#define WDT_WSPR (WDT_BASE+0x48)
#define LOAD_ADDR (KTZERO-0x20)

TEXT _setup(SB), $-4
	// disable wdt
	MOVW $WDT_WSPR, R0
	MOVW $0xAAAA, R1
	MOVW R1, (R0)

	MOVW $WDT_WWPS, R0
_wdtpoll1:
	MOVW (R0), R1
	AND $0x10, R1
	CMP $0, R1
	BNE _wdtpoll1

	MOVW $WDT_WSPR, R0
	MOVW $0x5555, R1
	MOVW R1, (R0)

	MOVW $WDT_WWPS, R0
_wdtpoll2:
	MOVW (R0), R1
	AND $0x10, R1
	CMP $0, R1
	BNE _wdtpoll2

	// turn off mmu and caches

	MRC 15, 0, R0, C(1), C(0), 0
	AND $0x8fffeff8, R0
	//ORR $0x20000000, R0
	BIC $(1<<12), R0 // disable icache
	BIC $(1<<2), R0 // disable dcache
	MCR 15, 0, R0, C(1), C(0), 0

	BL l1dclear(SB)

	// relocate initialised data segment
	// R1: s1, R2: s2, R3: e1, R4: e2
	MOVW $(LOAD_ADDR+1*4), R0
	MOVBU.P 1(R0), R6
	MOVW R6<<24, R1
	MOVBU.P 1(R0), R6
	ORR R6<<16, R1
	MOVBU.P 1(R0), R6
	ORR R6<<8, R1
	MOVBU.P 1(R0), R6
	ORR R6, R1
	ADD $(KTZERO), R1
	ADD $0xfff, R1, R2
	AND $~0xfff, R2
	MOVW $(LOAD_ADDR+2*4), R0
	MOVBU.P 1(R0), R6
	MOVW R6<<24, R3
	MOVBU.P 1(R0), R6
	ORR R6<<16, R3
	MOVBU.P 1(R0), R6
	ORR R6<<8, R3
	MOVBU.P 1(R0), R6
	ORR R6, R3
	ADD R2, R3, R4
	MOVW R4, R5
	ADD R1, R3
_movedata:
	MOVW.P -4(R3), R0
	MOVW.P R0, -4(R4)
	CMP.S R1, R3
	BNE _movedata

	// clear bss
	MOVW $(LOAD_ADDR+3*4), R0
	MOVBU.P 1(R0), R6
	MOVW R6<<24, R1
	MOVBU.P 1(R0), R6
	ORR R6<<16, R1
	MOVBU.P 1(R0), R6
	ORR R6<<8, R1
	MOVBU.P 1(R0), R6
	ORR R6, R1
	ADD R1, R5, R2
	ADD $0x1000, R2
	MOVW $0, R0
_clrbss:
	MOVW.P R0, 4(R5)
	CMP.S R5, R2
	BGE _clrbss

	// jump to start of kernel
	MOVW $(LOAD_ADDR+5*4), R0
	MOVBU.P 1(R0), R6
	MOVW R6<<24, R1
	MOVBU.P 1(R0), R6
	ORR R6<<16, R1
	MOVBU.P 1(R0), R6
	ORR R6<<8, R1
	MOVBU.P 1(R0), R6
	ORR R6, R1
	B (R1)

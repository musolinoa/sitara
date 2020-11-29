#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

typedef struct Ctlr Ctlr;
struct Ctlr
{
	Lock;
	volatile ulong *r;
	int irq, iena;
};

static Ctlr bbbctlr[1] = {
	{
		.r = (void*)VMAP,
		.irq = UART0IRQ,
	}
};

#define IT_TYPE(x) (((x)&0x3e)>>1)

enum
{
	RHR = 0x00/4,
	THR = 0x00/4,
	IER = 0x04/4,
		RHRIT = 1<<0,
		THRIT = 1<<1,
		SLEEPMODE = 1<<4,
		RTSIT = 1<<6,
	IIR = 0x08/4,
		IT_PENDING = 1<<0,
		IT_TYPE_MODEM = 0,
		IT_TYPE_THR = 1,
		IT_TYPE_RHR = 2,
		IT_TYPE_RXTIMEOUT = 6,
	EFR = 0x08/4,
		ENHANCEDEN = 1<<4,
	FCR = 0x08/4,
		FIFO_EN = 1<<0,
		RX_FIFO_CLEAR = 1<<1,
		TX_FIFO_CLEAR = 1<<2,
	LCR = 0x0c/4,
		ConfigModeA = 0x80,
		ConfigModeB = 0xbf,
		OperMode = 0x00,
		DIV_EN = 1<<7,
	MCR = 0x10/4,
		DTR = 1<<0,
		RTS = 1<<1,
		TCRTLR = 1<<6,
	LSR = 0x14/4,
		RXFIFOE = 1<<0,
		TXFIFOE = 1<<5,
		TXSRE = 1<<6,
	MSR = 0x18/4,
		
	MDR1 = 0x20/4,
		MODESELECT = 0x3,
	SCR = 0x40/4,
		TXEMPTYCTLIT = 1<<3,
		TXTRIGGRANU1 = 1<<6,
		RXTRIGGRANU1 = 1<<7,
	SSR = 0x44/4,
		TXFIFOFULL = 1<<0,
	SYSC = 0x54/4,
		SOFTRESET = 1<<1,
	SYSS = 0x58/4,
		RESETDONE = 1<<0,
	DLL = 0x00/4,
	DLH = 0x04/4,
};

Uart* uartenable(Uart*);

static Uart *bbbuartpnp(void);
static void bbbuartkick(Uart*);
static void bbbuartintr(Ureg*, void*);
static void bbbuartenable(Uart*, int);
static int bbbuartgetc(Uart*);
static void bbbuartputc(Uart*, int);
static int bbbuartbits(Uart*, int);
static int bbbuartbaud(Uart*, int);
static int bbbuartparity(Uart*, int);
static void bbbuartnop(Uart*, int);
static int bbbuartnope(Uart*, int);

PhysUart bbbphysuart = {
	.baud = bbbuartbaud,
	.bits = bbbuartbits,
	.dobreak = bbbuartnop,
	.dtr = bbbuartnop,
	.enable = bbbuartenable,
	.fifo = bbbuartnop,
	.getc = bbbuartgetc,
	.kick = bbbuartkick,
	.modemctl = bbbuartnop,
	.parity = bbbuartparity,
	.pnp = bbbuartpnp,
	.power = bbbuartnop,
	.putc = bbbuartputc,
	.rts = bbbuartnop,
	.stop = bbbuartnope,
};

static Uart bbbuart[1] = {
	{
		.regs = &bbbctlr[0],
		.name = "UART0",
		.freq = 25000000,
		.phys = &bbbphysuart,
		.console = 1,
		.baud = 115200,
	}
};

static Uart*
bbbuartpnp(void)
{
	return bbbuart;
}

static void
bbbuartkick(Uart *uart)
{
	Ctlr *ct;
	int i;

	if(uart->blocked)
		return;
	ct = uart->regs;
	for(i = 0; i < 128; i++){
		if((ct->r[SSR] & TXFIFOFULL) != 0)
			break;
		if(uart->op >= uart->oe)
		if(uartstageoutput(uart) == 0)
				break;
		ct->r[THR] = *uart->op++;
		ct->r[IER] |= THRIT;
		//coherence();
	}
}

static int
bbbuartbits(Uart*, int)
{
	return -1;
}

static int
bbbuartbaud(Uart*, int)
{
	return 0;
}

static void
bbbuartintr(Ureg *, void *arg)
{
	Uart *uart;
	Ctlr *ct;
	ulong iir, c;

	uart = arg;
	ct = uart->regs;
	for(iir = ct->r[IIR]; (iir & IT_PENDING) == 0; iir = ct->r[IIR]){
		switch(IT_TYPE(iir)){
		case IT_TYPE_RHR:
		case IT_TYPE_RXTIMEOUT:
			while((ct->r[LSR] & RXFIFOE) != 0){
				c = ct->r[RHR];
				uartrecv(uart, c);
			}
			break;
		case IT_TYPE_THR:
			bbbuartkick(uart);
			if(uart->op >= uart->oe)
			if(qlen(uart->oq) == 0)
			if((ct->r[LSR] & TXSRE) != 0){
				ct->r[IER] &= ~THRIT;
				//coherence();
			}
			break;
		}
	}
}

static void
bbbuartenable(Uart *uart, int ie)
{
	Ctlr *ctlr;

	ctlr = uart->regs;
	ilock(ctlr);

	while((ctlr->r[LSR] & TXSRE) == 0);

	ctlr->r[SYSC] |= SOFTRESET;
	while((ctlr->r[SYSS] & RESETDONE) == 0);

	ctlr->r[LCR] = ConfigModeB;
	ctlr->r[EFR] |= ENHANCEDEN;
	ctlr->r[LCR] = ConfigModeA;
	ctlr->r[MCR] |= TCRTLR;
	ctlr->r[FCR] = FIFO_EN;
	ctlr->r[LCR] = ConfigModeB;
	ctlr->r[EFR] &= ~ENHANCEDEN;
	ctlr->r[LCR] = ConfigModeA;
	ctlr->r[MCR] &= ~TCRTLR;

	ctlr->r[MDR1] = 0x7;
	ctlr->r[LCR] = ConfigModeB;
	ctlr->r[EFR] |= ENHANCEDEN;
	ctlr->r[LCR] = OperMode;
	ctlr->r[IER] = 0;
	ctlr->r[LCR] = ConfigModeB;
	ctlr->r[DLL] = 0x1a;
	ctlr->r[DLH] = 0x00; // 115200 baud
	ctlr->r[LCR] = OperMode;
	ctlr->r[SCR] |= TXEMPTYCTLIT;
	ctlr->r[IER] = RHRIT | THRIT; // enable interrupts
	ctlr->r[LCR] = ConfigModeB;
	ctlr->r[EFR] &= ~ENHANCEDEN;
	ctlr->r[LCR] = 0x03; // 8-it, 1 stop, no parity
	ctlr->r[MDR1] = 0;
	delay(25);

	if(ie){
		if(!ctlr->iena){
			intrenable(ctlr->irq, bbbuartintr, uart, uart->name);
			ctlr->iena = 1;
		}
	}
	iunlock(ctlr);
}

static int
bbbuartgetc(Uart *u)
{
	Ctlr *ct;

	ct = u->regs;
	while((ct->r[LSR] & RXFIFOE) == 0)
		;
	return ct->r[RHR];
}

static void
bbbuartputc(Uart *u, int c)
{
	Ctlr *ct;

	ct = u->regs;
	while((ct->r[SSR] & TXFIFOFULL) != 0)
		;
	ct->r[THR] = c;
}

static int
bbbuartparity(Uart*, int)
{
	return -1;
}

static void
bbbuartnop(Uart*, int)
{
}

static int
bbbuartnope(Uart*, int)
{
	return -1;
}

void
uartinit(void)
{
	consuart = bbbuart;
}

int
uartconsole(void)
{
	Uart *uart = bbbuart;

	if(up == nil)
		return -1;
	if(uartenable(uart) != nil){
		serialoq = uart->oq;
		uart->opens++;
		consuart = uart;
	}
	return 0;
}

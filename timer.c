#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

enum
{
	TIOCP_CFG = 0x10/4,
		SOFTRESET = 1<<0,

	IRQSTATUS_RAW = 0x24/4,
	IRQSTATUS = 0x28/4,
		MAT_IT_FLAG = 1<<0,
		OVF_IT_FLAG = 1<<1,
		TCAR_IT_FLAG = 1<<2,

	IRQENABLE_SET = 0x2c/4,
	IRQENABLE_CLR = 0x30/4,
		MAT_EN_FLAG = 1<<0,
		OVF_EN_FLAG = 1<<1,
		TCAR_EN_FLAG = 1<<2,

	TCLR = 0x38/4,
		ST = 1<<0,
		AR = 1<<1,
		PTV_SHIFT = 2,
		PTV_MASK = 0x07<<PTV_SHIFT,
		PRE = 1<<5,
		CE = 1<<6,

	TCRR = 0x3c/4,
	TLDR = 0x40/4,
	TWPS = 0x48/4,
		W_PEND_TCLR = 1<<0,
		W_PEND_TCRR = 1<<1,
		W_PEND_TLDR = 1<<2,
		W_PEND_TMAR = 1<<4,

	TMAR = 0x4c/4,
};

enum
{
	CM_PER = 0,
		CM_PER_L4LS_CLKSTCTRL = 0,
			CLKACTIVITY_TIMER2_GCLK = 1<<14,
			CLKACTIVITY_TIMER3_GCLK = 1<<15,
		CM_PER_TIMER2_CLKCTRL = 0x80/4,
		CM_PER_TIMER3_CLKCTRL = 0x84/4,

	CM_DPLL = 0x500/4,
		CLKSEL_TIMER2_CLK = 0x8/4,
		CLKSEL_TIMER3_CLK = 0xc/4,
			CLK_M_OSC = 0x1,
};

uvlong timerhz;

ulong
µs(void)
{
	return fastticks2us(fastticks(nil));
}

void
microdelay(int n)
{
	ulong now;

	now = µs();
	while(µs() - now < n);
}

void
delay(int n)
{
	while(--n >= 0)
		microdelay(1000);
}

ulong currhi;

uvlong
fastticks(uvlong *hz)
{
	int x;
	uvlong r;

	if(hz)
		*hz = timerhz;
	x = splhi();
	r = (uvlong)currhi<<32 | timer3[TCRR];
	splx(x);
	return r;
}

void
timerset(Tval next)
{
	int x;
	vlong period;
	uvlong ft;

	if(next == 0)
		return;
	x = splhi();
	ft = fastticks(nil);
	period = next - ft;
	if(period > 0xffffffffLL)
		period = 0xffffffffLL;
	else if(period < 1)
		period = 1;
	timer2[TCRR] = -period;
	timer2[TCLR] |= ST;
	splx(x);
}

void
timer2irq(Ureg *u, void*)
{
	timer2[IRQSTATUS] = OVF_IT_FLAG;
	while((timer2[IRQSTATUS] & OVF_IT_FLAG) != 0);
	timerintr(u, 0);
}

void
timer3irq(Ureg*, void*)
{
	currhi++;
	timer3[IRQSTATUS] = OVF_IT_FLAG;
	while((timer3[IRQSTATUS] & OVF_IT_FLAG) != 0);
}

void
timerinit(void)
{
	ulong *dpll, *per;

	m->cpumhz = 1000;
	m->cpuhz = m->cpumhz * 1000000;
	timerhz = 24000000;

	dpll = &cprm[CM_DPLL];
	dpll[CLKSEL_TIMER2_CLK] |= CLK_M_OSC;
	dpll[CLKSEL_TIMER3_CLK] |= CLK_M_OSC;

	per = &cprm[CM_PER];
	per[CM_PER_TIMER2_CLKCTRL] |= 2;
	per[CM_PER_TIMER3_CLKCTRL] |= 2;
	per[CM_PER_L4LS_CLKSTCTRL] |= CLKACTIVITY_TIMER2_GCLK | CLKACTIVITY_TIMER3_GCLK;

	timer2[TIOCP_CFG] = SOFTRESET;
	while((timer2[TIOCP_CFG] & SOFTRESET) != 0);
	timer2[IRQENABLE_SET] = OVF_EN_FLAG;

	timer3[TIOCP_CFG] = SOFTRESET;
	while((timer3[TIOCP_CFG] & SOFTRESET) != 0);
	timer3[IRQENABLE_SET] = OVF_EN_FLAG;
	timer3[TCLR] |= AR | ST;

	intrenable(TIMER2IRQ, timer2irq, nil, "clock");
	intrenable(TIMER3IRQ, timer3irq, nil, "clock");
}

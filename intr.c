#include "u.h"
#include <ureg.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

enum {
	NINTR = 128,
};

static struct irq {
	void (*f)(Ureg*, void*);
	void *arg;
	char *name;
} irqs[NINTR];

enum {
	SYSCFG = 0x10/4,
		AutoIdle = 1<<0,
		SoftReset = 1<<1,
	SYSSTATUS = 0x14/4,
		ResetDone = 1<<0,
	SIRIRQ = 0x40/4,
	CONTROL = 0x48/4,
		NewIRQAgr = 1<<0,
	PROTECTION = 0x4c/4,
		Protection = 1<<0,
	IDLE = 0x50/4,
		Turbo = 1<<1,

	MIRCLRBASE = 0x88/4,
	MIRSETBASE = 0x8c/4,
	MIRREGSTEP = 0x20/4,
};

#define MIRSET(n)	(MIRSETBASE+MIRREGSTEP*(n))
#define MIRCLR(n)	(MIRCLRBASE+MIRREGSTEP*(n))

void
intrinit(void)
{
	int i;

	intcps[SYSCFG] |= SoftReset;
	while((intcps[SYSSTATUS] & ResetDone) == 0);

	for(i = 0; i < 4; i++)
		intcps[MIRSET(i)] = ~0;

	intcps[PROTECTION] |= Protection;
}

void
intrenable(int irq, void (*f)(Ureg *, void *), void *arg, char *name)
{
	struct irq *i;

	if(f == nil)
		panic("intrenable: f == nil");
	if(irq < 0 || irq >= NINTR)
		panic("intrenable: invalid irq %d", irq);
	if(irqs[irq].f != nil && irqs[irq].f != f)
		panic("intrenable: handler already assigned");
	i = &irqs[irq];
	i->f = f;
	i->arg = arg;
	i->name = name;
	intcps[MIRCLR((irq>>5)&3)] = 1<<(irq&0x1f);
}

void
intr(Ureg *ureg)
{
	ulong v;
	int irq;
	struct irq *i;

	v = intcps[SIRIRQ];
	irq = v & 0x7f;

	if(v >> 7){
		print("spurious interrupt\n");
		intcps[CONTROL] = NewIRQAgr;
		return;
	}

	m->intr++;
	m->lastintr = irq;
	i = &irqs[irq];
	if(i->f == nil)
		print("irq without handler %d\n", irq);
	else
		i->f(ureg, i->arg);
	intcps[CONTROL] = NewIRQAgr;

	if(up != nil){
		if(irq == TIMER2IRQ){
			if(up->delaysched){
				splhi();
				sched();
			}
		}else
			preempted();
	}
}

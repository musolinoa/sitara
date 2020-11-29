#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "io.h"
#include "dat.h"
#include "fns.h"
#include "init.h"
#include "pool.h"
#include "../port/error.h"
#include "tos.h"

Conf conf;
uchar *sp;

enum { MAXCONF = 64 };

char *confname[MAXCONF], *confval[MAXCONF];
int nconf;

enum{
	PRM_DEVICE = 0x0f00/4,
		PRM_RSTCTRL = 0,
			RST_GLOBAL_COLD_SW = 1<<1,
			RST_GLOBAL_WARM_SW = 1<<0,
};

void
exit(int)
{
	ulong *r;

	cpushutdown();
	r = &cprm[PRM_DEVICE];
	r[PRM_RSTCTRL] |= RST_GLOBAL_COLD_SW;
	for(;;) idlehands();
}

void
reboot(void*, void*, ulong)
{
}

void
evenaddr(uintptr va)
{
	if((va & 3) != 0){
		dumpstack();
		postnote(up, 1, "sys: odd address", NDebug);
		error(Ebadarg);
	}
}

void
procfork(Proc *p)
{
	ulong s;

	p->kentry = up->kentry;
	p->pcycles = -p->kentry;
	
	s = splhi();
	switch(up->fpstate & ~FPillegal){
	case FPactive:
		fpsave(up->fpsave);
		up->fpstate = FPinactive;
	case FPinactive:
		memmove(p->fpsave, up->fpsave, sizeof(FPsave));
		p->fpstate = FPinactive;
	}
	splx(s);
}

void
procsetup(Proc *p)
{
	p->fpstate = FPinit;
	fpoff();
	
	cycles(&p->kentry);
	p->pcycles = -p->kentry;
}

void
kexit(Ureg *)
{
	Tos *tos;
	uvlong t;

	tos = (Tos*)(USTKTOP-sizeof(Tos));
	cycles(&t);
	tos->kcycles += t - up->kentry;
	tos->pcycles = t + up->pcycles;
	tos->pid = up->pid;
}

char*
getconf(char *n)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], n) == 0)
			return confval[i];
	return nil;
}

static void
options(void)
{
	long i, n;
	char *cp, *line[MAXCONF], *p, *q;

	cp = (char *) CONFADDR;

	p = cp;
	for(q = cp; *q; q++){
		if(*q == '\r')
			continue;
		if(*q == '\t')
			*q = ' ';
		*p++ = *q;
	}
	*p = 0;

	n = getfields(cp, line, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++){
		if(*line[i] == '#')
			continue;
		cp = strchr(line[i], '=');
		if(cp == nil)
			continue;
		*cp++ = '\0';
		confname[nconf] = line[i];
		confval[nconf] = cp;
		nconf++;
	}
}

void
confinit(void)
{
	ulong kmem;
	int i;

	conf.nmach = 1;
	conf.nproc = 2000;
	conf.ialloc = 16*1024*1024;
	conf.nimage = 200;
	conf.mem[0].base = PGROUND((ulong)end - KZERO + DRAM_BASE);
	conf.mem[0].limit = DRAM_TOP;
	conf.npage = 0;
	for(i = 0; i < nelem(conf.mem); i++)
		conf.npage += conf.mem[i].npage = (conf.mem[i].limit - conf.mem[i].base) >> PGSHIFT;
	kmem = 200*1024*1024;
	conf.upages = conf.npage - kmem/BY2PG;
	kmem -= conf.upages*sizeof(Page)
		+ conf.nproc*sizeof(Proc)
		+ conf.nimage*sizeof(Image);
	mainmem->maxsize = kmem;
	imagmem->maxsize = kmem - (kmem/10);
}

void
cpuidprint(void)
{
	print("cpu%d: %dMHz ARM Cortex-A8\n", m->machno, m->cpumhz);
}

static uchar *
pusharg(char *p)
{
	int n;
	
	n = strlen(p) + 1;
	sp -= n;
	memmove(sp, p, n);
	return sp;
}

static void
bootargs(void *base)
{
	int i, ac;
	uchar *av[32];
	uchar **lsp;
	
	sp = (uchar *) base + BY2PG - sizeof(Tos);
	
	ac = 0;
	av[ac++] = pusharg("boot");
	sp = (uchar *) ((ulong) sp & ~3);
	sp -= (ac + 1) * sizeof(sp);
	lsp = (uchar **) sp;
	for(i = 0; i < ac; i++)
		lsp[i] = av[i] + ((USTKTOP - BY2PG) - (ulong) base);
	lsp[i] = 0;
	sp += (USTKTOP - BY2PG) - (ulong) base;
	sp -= BY2WD;
}

static void
init0(void)
{
	char buf[ERRMAX];
	int i;

	up->nerrlab = 0;
	spllo();
	up->slash = namec("#/", Atodir, 0, 0);
	pathclose(up->slash->path);
	up->slash->path = newpath("/");
	up->dot = cclone(up->slash);
	chandevinit();
	uartconsole();

	if(!waserror()){
		ksetenv("cputype", "arm", 0);
		if(cpuserver)
			ksetenv("service", "cpu", 0);
		else
			ksetenv("service", "terminal", 0);
		ksetenv("console", "0", 0);
		snprint(buf, sizeof(buf), "am335x %s", conffile);
		ksetenv("terminal", buf, 0);
		for(i = 0; i < nconf; i++){
			if(*confname[i] != '*')
				ksetenv(confname[i], confval[i], 0);
			ksetenv(confname[i], confval[i], 1);
		}
		poperror();
	}
	kproc("alarm", alarmkproc, 0);
	touser(sp);
}

void
userinit(void)
{
	Proc *p;
	Segment *s;
	void *v;
	Page *pg;
	
	p = newproc();
	p->pgrp = newpgrp();
	p->egrp = smalloc(sizeof(Egrp));
	p->egrp->ref = 1;
	p->fgrp = dupfgrp(nil);
	p->rgrp = newrgrp();
	p->procmode = 0640;
	
	kstrdup(&eve, "");
	kstrdup(&p->text, "*init*");
	kstrdup(&p->user, eve);
	
	procsetup(p);
	
	p->sched.pc = (ulong)init0;
	p->sched.sp = (ulong)p->kstack + KSTACK - (sizeof(Sargs) + BY2WD);
	
	s = newseg(SG_STACK, USTKTOP - USTKSIZE, USTKSIZE / BY2PG);
	p->seg[SSEG] = s;
	pg = newpage(0, 0, USTKTOP - BY2PG);
	v = tmpmap(pg->pa);
	memset(v, 0, BY2PG);
	segpage(s, pg);
	bootargs(v);
	tmpunmap(v);

	s = newseg(SG_TEXT, UTZERO, 1);
	s->flushme++;
	p->seg[TSEG] = s;
	pg = newpage(0, 0, UTZERO);
	pg->txtflush = ~0;

	segpage(s, pg);
	v = tmpmap(pg->pa);
	memset(v, 0, BY2PG);
	memmove(v, initcode, sizeof(initcode));
	tmpunmap(v);

	ready(p);
}

void
main(void)
{
	active.machs[m->machno] = 1;
	uartinit();
	mmuinit();
	intrinit();
	options();
	confinit();
	timerinit();
	uartputs(" from Bell Labs\n", 16);
	printinit();
	xinit();
	quotefmtinstall();
	cpuidprint();
	todinit();
	timersinit();
	procinit0();
	initseg();
	links();
	chandevreset();
	pageinit();
	userinit();
	schedinit();
}

void
setupwatchpts(Proc*, Watchpt*, int n)
{
	if(n > 0)
		error("no watchpoints");
}

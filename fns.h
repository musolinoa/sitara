#include "../port/portfns.h"

int tas(void*);
int cmpswap(long*, long, long);
void evenaddr(uintptr);
void* kaddr(uintptr);
uintptr paddr(void*);
uintptr cankaddr(uintptr);
void procsave(Proc*);
void procrestore(Proc*);
void idlehands(void);
void coherence(void);
void procfork(Proc*);
void procsetup(Proc*);
KMap* kmap(Page*);
void kunmap(KMap*);
void* ucalloc(ulong);

#define	waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))
#define getpgcolor(a) 0
#define kmapinval()
#define KADDR(a) kaddr(a)
#define PADDR(a) paddr((void*)(a))
#define userureg(ur) (((ur)->psr & PsrMask) == PsrMusr)
#define VA(k) ((void*)(k))
#define PTR2UINT(p) ((uintptr)(p))

void uartinit(void);
void mmuinit(void);
uintptr ttbget(void);
void ttbput(uintptr);
void cycles(uvlong*);
void kexit(Ureg*);
ulong getifsr(void);
ulong getdfsr(void);
uintptr getifar(void);
uintptr getdfar(void);
void links(void);
void* vmap(uintptr, ulong);
void timerinit(void);
void* tmpmap(uintptr);
void tmpunmap(void*);
void flushpg(void*);
void setasid(ulong);
void flushtlb(void);
void noted(Ureg*, ulong);
void l1switch(L1*, int);
void intrenable(int, void (*)(Ureg*, void*), void*, char*);
void intrinit(void);
void intr(Ureg*);
void fpoff(void);
void fpsave(FPsave*);
void fprestore(FPsave*);
void fpinit(void);
void fpclear(void);
char* getconf(char*);
void invalise(void*, void*);
void cleandse(void*, void*);
void invaldse(void*, void*);
void touser(void*);
int uartconsole(void);

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/netif.h"
#include "etherif.h"

enum{
	CPSW_SS,
		SS_SOFT_RESET = 0x08/4,

	CPSW_CPDMA = 0x0800/4,
		TX_CONTROL = CPSW_CPDMA+0x04/4,
			TX_EN = 1<<0,
		RX_CONTROL = CPSW_CPDMA+0x14/4,
			RX_EN = 1<<0,
		CPDMA_SOFT_RESET = CPSW_CPDMA+0x1c/4,
		DMASTATUS = CPSW_CPDMA+0x24/4,
		RX_BUFFER_OFFSET = CPSW_CPDMA+0x28/4,
		TX_INTMASK_SET = CPSW_CPDMA+0x88/4,
		TX_INTMASK_CLEAR = CPSW_CPDMA+0x8c/4,
			TX_MASK_ALL = 0xff,
		CPDMA_EOI_VECTOR = CPSW_CPDMA+0x94/4,
		RX_INTMASK_SET = CPSW_CPDMA+0xa8/4,
			RX_MASK_ALL = 0xff,
		DMA_INTMASK_SET = CPSW_CPDMA+0xb8/4,
			STAT_INT_MASK = 1<<0,
			HOST_ERR_INT_MASK = 1<<1,

	CPSW_STATERAM = 0x0a00/4,
		TX0_HDP = CPSW_STATERAM+0x00/4,
		RX0_HDP = CPSW_STATERAM+0x20/4,
		TX0_CP	= CPSW_STATERAM+0x40/4,
		RX0_CP	= CPSW_STATERAM+0x60/4,

	CPSW_ALE = 0x0d00/4,
		ALE_CONTROL = CPSW_ALE+0x08/4,
			BYPASS = 1<<4,
			CLEAR_TABLE = 1<<30,
			ENABLE_ALE = 1<<31,
		PORTCTL0 = CPSW_ALE+0x40/4,
		PORTCTL1 = CPSW_ALE+0x44/4,
			PORT_STATE = 3,

	CPSW_SL1 = 0x0d80/4,
		MACCONTROL = CPSW_SL1+0x04/4,
			IFCTL_A = 1<<15,
			GIG = 1<<7,
			GMII_EN = 1<<5,
			FULLDUPLEX = 1<<0,
		MACSTATUS = CPSW_SL1+0x08/4,
		SL1_SOFT_RESET = CPSW_SL1+0x0c/4,

	CPSW_WR = 0x1200/4,
		WR_SOFT_RESET = CPSW_WR+0x04/4,
		C0_RX_THRESH_EN = CPSW_WR+0x10/4,
		C0_RX_EN = CPSW_WR+0x14/4,
		C0_TX_EN = CPSW_WR+0x18/4,
		C0_MISC_EN = CPSW_WR+0x1c/4,
			MDIO_USERINT = 1<<0,
			MDIO_LINKINT = 1<<1,
			HOST_PEND = 1<<2,
			STAT_PEND = 1<<3,
			EVNT_PEND = 1<<4,
		C0_RX_THRESH_STAT = CPSW_WR+0x40/4,
		C0_RX_STAT = CPSW_WR+0x44/4,
		C0_TX_STAT = CPSW_WR+0x48/4,
		C0_MISC_STAT = CPSW_WR+0x4c/4,
		RGMII_CTL = CPSW_WR+0x88/4,
			RGMII1_LINK = 1<<0,
			RGMII2_LINK = 1<<4,

	MDIO = 0x1000/4,
		MDIOCONTROL = MDIO+04/4,
		MDIOALIVE = MDIO+0x08/4,
		MDIOLINK = MDIO+0x0c/4,
		MDIOLINKINTRAW = MDIO+0x10/4,
		MDIOLINKINTMASKED = MDIO+0x14/4,
		MDIOUSERPHYSEL0 = MDIO+0x84/4,
		MDIOUSERPHYSEL1 = MDIO+0x88/4,
			PHYADDRMON = 0x1f,
			LINKINTENB = 1<<6,
			LINKSEL = 1<<7,
};

#define TX_HDP(i)	(TX0_HDP+i)
#define RX_HDP(i)	(RX0_HDP+i)
#define TX_CP(i)	(TX0_CP+i)
#define RX_CP(i)	(RX0_CP+i)

enum{
	EOQ = 1<<12,
	OWNER = 1<<13,
	EOP = 1<<14,
	SOP = 1<<15,
};

typedef struct Desc Desc;
struct Desc
{
	uintptr next;
	uintptr bufptr;
	ushort buflen;
	ushort bufoff;
	ushort pktlen;
	ushort flags;
};

#define Rbsz		ROUNDUP(sizeof(Etherpkt)+16, 64)

enum{
	RXRING = 0x200,
	TXRING = 0x200,
};

typedef struct Ctlr Ctlr;
struct Ctlr
{
	ulong *r;
	int attach;
	int rxconsi;
	int rxprodi;
	Desc *rxd;
	Block **rxb;
	int txconsi;
	int txprodi;
	Desc *txd;
	Block **txb;
	Lock txlock;
};

static int ethinit(Ether*);

static int
replenish(Ctlr *c)
{
	int n;
	Block *b;
	Desc *d;

	for(;;){
		n = (c->rxprodi+1) & (RXRING-1);
		if(n == c->rxconsi){
			c->rxd[c->rxprodi].next = 0;
			break;
		}
		b = iallocb(Rbsz);
		if(b == nil){
			iprint("cpsw: out of memory for rx buffers\n");
			return -1;
		}
		c->rxb[n] = b;
		d = &c->rxd[n];
		d->bufptr = PADDR(b->rp);
		d->bufoff = 0;
		d->buflen = BALLOC(b);
		if((d->buflen & 0x7ff) != d->buflen)
			d->buflen = 0x7ff;
		d->flags = OWNER;
		assert(d->buflen > 0);
		//cleandse(d, d+1);
		cleandse(b->base, b->lim);
		//coherence();
		assert(c->rxd[c->rxprodi].next == 0);
		d = &c->rxd[c->rxprodi];
		d->next = PADDR(&c->rxd[n]);
		//cleandse(d, d+1);
		c->rxprodi = n;
	}
	return 0;
}

static void
ethrxirq(Ureg*, void *arg)
{
	static int x;

	int n;
	Ether *edev;
	Ctlr *c;
	Desc *d, *d0;
	Block *b;

	edev = arg;
	c = edev->ctlr;

 	//iprint("rx enter (%d)!\n", x);
	n = 0;
	d0 = nil;
	for(;;){
		d = &c->rxd[c->rxconsi];
		//invaldse(d, d+1);
		if((d->flags & OWNER) != 0)
			break;
		if((d->flags & SOP) == 0 || (d->flags & EOP) == 0)
			iprint("cpsw: partial frame received -- shouldn't happen\n");
		if((d->flags & OWNER) == 0 && (d->flags & EOQ) != 0 && d->next != 0){
			//iprint("cpsw: rx misqueue detected\n");
			d0 = d;
		}
		b = c->rxb[c->rxconsi];
		b->wp = b->rp + (d->pktlen & 0x7ff);
		invaldse(b->rp, b->wp);
		//coherence();
		etheriq(edev, b, 1);
		c->r[RX0_CP] = PADDR(d);
		c->rxconsi = (c->rxconsi+1) & (RXRING-1);
		replenish(c);
		n++;
	}
	if(n == 0)
		iprint("cpsw: rx buffer full\n");
	if(d0 != nil){
		assert(d0->next == PADDR(d));
		c->r[RX0_HDP] = PADDR(d);
	}
	c->r[CPDMA_EOI_VECTOR] = 1;
	//iprint("rx leave (%d)!\n", x++);
}

static void
ethtx(Ether *edev)
{
	int i, n;
	Block *b;
	Ctlr *c;
	Desc *d0, *dn, *dp;

	c = edev->ctlr;
	ilock(&c->txlock);

	for(;;){
		dp = &c->txd[c->txconsi];
		if(c->txconsi == c->txprodi){
			dp->flags |= EOQ;
			break;
		}
		if((dp->flags & OWNER) != 0)
			break;
		c->txconsi = (c->txconsi+1) & (TXRING-1);
	}
	c->r[TX0_CP] = PADDR(&c->txd[c->txconsi]);
	n = 0;
	d0 = nil;
	for(;;){
		i = (c->txprodi+1) & (TXRING-1);
		dn = &c->txd[i];
		if((dn->flags & OWNER) != 0){
			iprint("cpsw: tx buffer full\n");
			break;
		}
		b = qget(edev->oq);
		if(b == nil)
			break;
		if(c->txb[c->txprodi] != nil)
			freeb(c->txb[c->txprodi]);
		c->txb[c->txprodi] = b;
		dp->next = PADDR(dn);
		dn->next = 0;
		dn->bufptr = PADDR(b->rp);
		dn->buflen = BLEN(b);
		dn->flags = SOP | EOP | OWNER;
		dn->pktlen = dn->buflen;
		if(d0 == nil)
			d0 = dp;
		dp = dn;
		c->txprodi = i;
		n++;
	}
	//iprint("tx: queued %d packets\n", n);
	if(d0 != nil){ // queued at least 1 packet
		if((d0->flags & OWNER) == 0 && (d0->flags & EOQ) != 0){
			//iprint("tx: misqueue detected!\n");
			c->r[TX0_HDP] = d0->next;
		}
	}
	iunlock(&c->txlock);
}

static void
ethtxirq(Ureg*, void *arg)
{
	Ether *edev;
	Ctlr *c;

	edev = arg;
	c = edev->ctlr;
	//assert(c->r[TX0_HDP] == 0);
	ethtx(edev);
	c->r[CPDMA_EOI_VECTOR] = 2;
}

static void
debugctlr(Ctlr *c)
{
	int i;
	uintptr hdp;
	Desc *d;

	hdp = c->r[RX0_HDP];
	if(hdp != 0){
		d = (Desc*)kaddr(hdp);
		iprint("pa(RX0_HDP)=%#p\n", hdp);
		iprint("va(RX0_HDP)=%#p\n", d);
		iprint("ii(RX0_HDP)=%ld\n",  d - &c->rxd[0]);	
		iprint("next=%#p\n", d->next);
	}
	for(i = 0; i < RXRING; i++){
		d = &c->rxd[i];
		if(d->buflen <= d->bufoff)
			iprint("i=%d -> {buflen=%d, bufoff=%d, pktlen=%d }\n", i, d->buflen, d->bufoff, d->pktlen);
	}
}

static void
ethmiscirq(Ureg*, void *arg)
{
	Ether *edev;
	Ctlr *c;
	ulong r;

	edev = arg;
	c = edev->ctlr;
	r = c->r[C0_MISC_STAT];

	if((r & MDIO_LINKINT) != 0){
		if((c->r[MDIOLINK] & 1) == 0){
			edev->link = 0;
			iprint("cpsw: no link\n");
		}else{
			edev->link = 1;
			edev->mbps = c->r[MACCONTROL] & IFCTL_A ? 100 : 10;
			iprint("cpsw: %dMbps %s duplex link\n", edev->mbps, c->r[MACCONTROL] & FULLDUPLEX ? "full" : "half");
		}
		c->r[MDIOLINKINTRAW] |= 1;
	}
	if((r & HOST_PEND) != 0){
		debugctlr(c);
		r = c->r[DMASTATUS];
		iprint("cpsw: dma host error (tx=0x%ulx, rx=0x%ulx)\n", (r>>20)&0xf, (r>>12)&0xf);
		ethinit(edev);
	}

	c->r[CPDMA_EOI_VECTOR] = 3;
}

static int
ethinit(Ether *edev)
{
	int i;
	Ctlr *c;

	c = edev->ctlr;

	c->r[SS_SOFT_RESET] |= 1;
	c->r[SL1_SOFT_RESET] |= 1;
	c->r[CPDMA_SOFT_RESET] |= 1;

	while((c->r[SS_SOFT_RESET] & 1) != 0);
	while((c->r[SL1_SOFT_RESET] & 1) != 0);
	while((c->r[CPDMA_SOFT_RESET] & 1) != 0);

	for(i = 0; i < 8; i++){
		c->r[TX_HDP(i)] = 0;
		c->r[RX_HDP(i)] = 0;
		c->r[TX_CP(i)] = 0;
		c->r[RX_CP(i)] = 0;
	}

	c->r[ALE_CONTROL] |= ENABLE_ALE | CLEAR_TABLE | BYPASS;
	c->r[PORTCTL0] |= PORT_STATE;
	c->r[PORTCTL1] |= PORT_STATE;

	//intrenable(ETHIRQRXTHR, ethrxthrirq, edev, edev->name);
	intrenable(ETHIRQRX, ethrxirq, edev, edev->name);
	intrenable(ETHIRQTX, ethtxirq, edev, edev->name);
	intrenable(ETHIRQMISC, ethmiscirq, edev, edev->name);

	c->r[TX_INTMASK_SET] |= TX_MASK_ALL;
	c->r[RX_INTMASK_SET] |= RX_MASK_ALL;
	c->r[DMA_INTMASK_SET] |= STAT_INT_MASK | HOST_ERR_INT_MASK;

	c->r[MDIOUSERPHYSEL0] |= LINKINTENB;

	c->r[C0_RX_THRESH_EN] |= 1;
	c->r[C0_RX_EN] |= 1;
	c->r[C0_TX_EN] |= 1;
	c->r[C0_MISC_EN] |= HOST_PEND | MDIO_LINKINT;

	c->r[RX_CONTROL] |= RX_EN;
	c->r[TX_CONTROL] |= TX_EN;

	c->r[MACCONTROL] &= ~GIG;
	c->r[MACCONTROL] |= GMII_EN | FULLDUPLEX | IFCTL_A;

	if(c->rxd == nil)
		c->rxd = ucalloc(RXRING*sizeof(Desc));
	memset(c->rxd, 0, RXRING*sizeof(Desc));
	if(c->rxb == nil)
		c->rxb = ucalloc(RXRING*sizeof(Block*));
	memset(c->rxb, 0, RXRING*sizeof(Block*));

	if(c->txd == nil)
		c->txd = ucalloc(TXRING*sizeof(Desc));
	memset(c->txd, 0, TXRING*sizeof(Desc));
	if(c->txb == nil)
		c->txb = ucalloc(TXRING*sizeof(Block*));
	memset(c->txb, 0, TXRING*sizeof(Block*));

#ifdef nope
	ulong x;
	x = va2pa(c->txd);
	if((x&1) != 0)
		iprint("ethinit: va2pa failed\n");
	else
		iprint("ethinit: %#p, SH=%ulx, OUTER=%ulx, INNER=%ulx\n", x, (x>>7)&1, (x>>2)&3, (x>>4)&7);

	x = va2pa(c->txb);
	if((x&1) != 0)
		iprint("ethinit: va2pa failed\n");
	else
		iprint("ethinit: %#p, SH=%ulx, OUTER=%ulx, INNER=%ulx\n", x, (x>>7)&1, (x>>2)&3, (x>>4)&7);
#endif

	c->rxprodi = RXRING-1;
	c->rxconsi = RXRING-1;
	replenish(c);
	c->rxconsi = 0;
	replenish(c);

	c->r[RX0_HDP] = PADDR(&c->rxd[c->rxconsi]);

	return 0;
}

static void
ethprom(void*, int)
{
}

static void
ethmcast(void*, uchar*, int)
{
}

static void
ethattach(Ether *edev)
{
	Ctlr *c;

	c = edev->ctlr;
	if(c->attach)
		return;
	c->attach = 1;
}

static int
etherpnp(Ether *edev)
{
	static Ctlr ct;
	static uchar mac[] = {0x6c, 0xec, 0xeb, 0xaf, 0x1c, 0xed};

	ulong x;

	if(ct.r != nil)
		return -1;
	memmove(edev->ea, mac, 6);
	edev->ctlr = &ct;
	edev->port = ETH_BASE;
	ct.r = vmap(edev->port, 2*BY2PG);

#ifdef nope
	x = va2pa(ct.r);
	if((x&1) != 0)
		iprint("etherpnp: va2pa failed\n");
	else
		iprint("etherpnp: %#p, SH=%ulx, OUTER=%ulx, INNER=%ulx\n", x, (x>>7)&1, (x>>2)&3, (x>>4)&7);
#endif

	edev->irq0 = ETHIRQRXTHR;
	edev->irqn = ETHIRQMISC;
	edev->transmit = ethtx;
	edev->attach = ethattach;
	edev->promiscuous = ethprom;
	edev->multicast = ethmcast;
	edev->mbps = 100;
	edev->arg = edev;
	if(ethinit(edev) < 0){
		edev->ctlr = nil;
		return -1;
	}
	return 0;
}

void
etherbbblink(void)
{
	addethercard("cpsw", etherpnp);
}

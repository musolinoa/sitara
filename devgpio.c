#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

enum{
	GPIO_SYSCONFIG = 0x10/4,
		SOFTRESET = 1<<1,
	GPIO_SYSSTATUS = 0x114/4,
		RESETDONE = 1<<0,
	GPIO_OE = 0x134/4,
	GPIO_DATAOUT = 0x13c/4,
	GPIO_CLEARDATAOUT = 0x190/4,
	GPIO_SETDATAOUT = 0x194/4,
};

enum{
	Qroot,
	Qgpio,
	Qdir,
		Qdata,
		Qctl,
		Qevent,
};

static Dirtab rootdir = { "#G",   { Qroot, 0, QTDIR }, 0, 0555 };
static Dirtab gpiodir = { "gpio", { Qgpio, 0, QTDIR }, 0, 0555 };

static Dirtab pinfiles[] = {
	"data",  { Qdata,  0, QTFILE }, 0, 0666,
	"ctl",   { Qctl,   0, QTFILE }, 0, 0666,
	"event", { Qevent, 0, QTFILE }, 0, 0444,
};

static void
gpioinit(void)
{
	int i;

	for(i = 0; i < 4; i++){
		gpio[i][GPIO_SYSCONFIG] = SOFTRESET;
		while((gpio[i][GPIO_SYSSTATUS] & RESETDONE) != 0);
		gpio[i][GPIO_OE] = 0;
	}
}

static void
gpioshutdown(void)
{
}

static Chan*
gpioattach(char *spec)
{
	return devattach('G', spec);
}

static int
gpiogen(Chan *c, char*, Dirtab*, int, int s, Dir *db)
{
	Qid qid;

	if(s == DEVDOTDOT){
		switch((ulong)c->qid.path&0xff){
		case Qroot:
		case Qgpio:
			qid.type = QTDIR;
			qid.path = Qroot;
			qid.vers = 0;
			devdir(c, qid, 0, 0, eve, 0555, db);
			return 1;
		case Qdir:
			qid.type = QTDIR;
			qid.path = Qgpio;
			qid.vers = 0;
			devdir(c, qid, 0, 0, eve, 0555, db);
			return 1;
		case Qdata:
		case Qctl:
		case Qevent:
			qid.type = QTDIR;
			qid.path = (c->qid.path&0xff00)|Qdir;
			qid.vers = 0;
			devdir(c, qid, 0, 0, eve, 0555, db);
			return 1;
		}
		return -1;
	}

	if(c->qid.path == Qroot){
		if(s == 0){
			qid.type = QTDIR;
			qid.path = Qgpio;
			qid.vers = 0;
			devdir(c, qid, "gpio", 0, eve, 0555, db);
			return 1;
		}
		return -1;
	}

	if(c->qid.path == Qgpio){
		if(s < 128){
			qid.type = QTDIR;
			qid.path = s<<8|Qdir;
			qid.vers = 0;
			snprint(up->genbuf, sizeof(up->genbuf), "%d", s);
			devdir(c, qid, up->genbuf, 0, eve, 0555, db);
			return 1;
		}
		return -1;
	}

	if((c->qid.path & 0xff) == Qdir){
		qid.type = QTFILE;
		qid.path = (c->qid.path&0xff00);
		qid.vers = 0;
		switch(s){
		case 0:
			qid.path |= Qdata;
			devdir(c, qid, "data", 0, eve, 0666, db);
			return 1;
		case 1:
			qid.path |= Qctl;
			devdir(c, qid, "ctl", 0, eve, 0666, db);
			return 1;
		case 2:
			qid.path |= Qevent;
			devdir(c, qid, "event", 0, eve, 0444, db);
			return 1;
		}
		return -1;
	}

	qid.type = QTFILE;
	qid.path = c->qid.path+s;
	qid.vers = 0;

	switch((ulong)c->qid.path & 0xff){
	case Qdata:
		devdir(c, qid, "data", 0, eve, 0666, db);
		return 1;
	case Qctl:
		devdir(c, qid, "ctl", 0, eve, 0666, db);
		return 1;
	case Qevent:
		devdir(c, qid, "event", 0, eve, 0666, db);
		return 1;
	}

	return -1;
}

static Walkqid*
gpiowalk(Chan *c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, gpiogen);
}

static int
gpiostat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, 0, 0, gpiogen);
}

static Chan*
gpioopen(Chan *c, int omode)
{
	c = devopen(c, omode, 0, 0, gpiogen);
	return c;
}

static void
gpioclose(Chan*)
{
}

static long
gpioread(Chan *c, void *a, long n, vlong off)
{
	ulong pin, reg, i;

	pin = (ulong)c->qid.path>>8;
	reg = pin>>5;
	i = pin&0x1f;
	switch((ulong)c->qid.path&0xff){
	case Qroot:
	case Qgpio:
	case Qdir:
		return devdirread(c, a, n, 0, 0, gpiogen);
	case Qdata:
		if(n > 2)
			n = 2;
		if(off > 1)
			return 0;
		if((gpio[reg][GPIO_DATAOUT] & 1<<i) != 0)
			memmove(a, "1\n", n);
		else
			memmove(a, "0\n", n);
		return 2;
	case Qctl:
		if(n > 8)
			n = 8;
		if(off > 8)
			return 0;
		memmove(a, "dig out\n" + off, n - off);
		return n - off;
	case Qevent:
		return 0;
	}
	error(Egreg);
	return -1;
}

static char Ebaddata[] = "bad data message";

static long
gpiowrite(Chan *c, void *va, long, vlong)
{
	char *a;
	ulong pin, reg, i;

	if(c->qid.type & QTDIR)
		error(Eisdir);

	a = va;
	pin = (ulong)c->qid.path>>8;
	reg = pin>>5;
	i = pin&0x1f;
	switch((ulong)c->qid.path&0xff){
	case Qdata:
		if(a[0] == '1'){
			gpio[reg][GPIO_SETDATAOUT] = 1<<i;
			return 1;
		}
		if(a[0] == '0'){
			gpio[reg][GPIO_CLEARDATAOUT] = 1<<i;
			return 1;
		}
		error(Ebaddata);
		return -1;
	}
	error(Egreg);
	return -1;
}

Dev gpiodevtab = {
	'G',
	"gpio",

	devreset,
	gpioinit,
	gpioshutdown,
	gpioattach,
	gpiowalk,
	gpiostat,
	gpioopen,
	devcreate,
	gpioclose,
	gpioread,
	devbread,
	gpiowrite,
	devbwrite,
	devremove,
	devwstat,
};

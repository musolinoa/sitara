CONF=bbb
CONFLIST=bbb

#must match mem.h
KTZERO=0x80080000

objtype=arm
</$objtype/mkfile
p=9

DEVS=`{rc ../port/mkdevlist $CONF}

PORT=\
	alarm.$O\
	alloc.$O\
	allocb.$O\
	auth.$O\
	cache.$O\
	chan.$O\
	dev.$O\
	edf.$O\
	fault.$O\
	mul64fract.$O\
	page.$O\
	parse.$O\
	pgrp.$O\
	portclock.$O\
	print.$O\
	proc.$O\
	qio.$O\
	qlock.$O\
	random.$O\
	rdb.$O\
	rebootcmd.$O\
	segment.$O\
	syscallfmt.$O\
	sysfile.$O\
	sysproc.$O\
	taslock.$O\
	tod.$O\
	xalloc.$O\

OBJ=\
	ltrap.$O\
	l.$O\
	intr.$O\
	main.$O\
	mmu.$O\
	timer.$O\
	trap.$O\
	reloc.$O\
	$CONF.root.$O\
	$CONF.rootc.$O\
	$DEVS\
	$PORT\

LIB=\
	/$objtype/lib/libip.a\
	/$objtype/lib/libsec.a\
	/$objtype/lib/libc.a\

$p$CONF: $CONF.c $OBJ $LIB mkfile
	$CC $CFLAGS '-DKERNDATE='`{date -n} $CONF.c
	$LD -o $target -E _start -T$KTZERO -l $OBJ $CONF.$O $LIB

<../boot/bootmkfile
<../port/portmkfile
<|../port/mkbootrules $CONF

init.h:D: ../port/initcode.c init9.s
	$CC ../port/initcode.c
	$AS init9.s
	$LD -l -R1 -s -o init.out init9.$O initcode.$O /arm/lib/libc.a
	{echo 'uchar initcode[]={'
	 xd -1x <init.out |
		sed -e 's/^[0-9a-f]+ //' -e 's/ ([0-9a-f][0-9a-f])/0x\1,/g'
	 echo '};'} > init.h

install:V:	$p$CONF
	cp $p$CONF /$objtype/
	for(i in $EXTRACOPIES)
		import $i / /n/$i && cp $p$CONF $p$CONF.gz /n/$i/$objtype/

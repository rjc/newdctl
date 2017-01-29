#	$OpenBSD$

PROG=	netcfgctl
SRCS=	netcfgctl.c parser.c

MAN=	netcfgctl.8

CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../newd
LDADD=	-lutil
DPADD=	${LIBUTIL}

.include <bsd.prog.mk>

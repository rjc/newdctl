#	$OpenBSD$

.PATH:		${.CURDIR}/../ospfd

PROG=	newdctl
SRCS=	newdctl.c parser.c
CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../newd
LDADD=	-lutil
DPADD=	${LIBUTIL}
MAN=	newdctl.8

.include <bsd.prog.mk>

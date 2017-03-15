/*	$OpenBSD$	*/

/*
 * Copyright (c) 2017 Kenneth R Westerback <krw@openbsd.org>
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003 Henning Brauer <henning@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/route.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "netcfgd.h"
#include "frontend.h"
#include "parser.h"

__dead void	 usage(void);
int		 show_main_msg(struct imsg *);
int		 show_proposals_msg(struct imsg *);
int		 show_frontend_msg(struct imsg *);

struct imsgbuf	*ibuf;

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-s socket] command [argument ...]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct imsg		 imsg;
	struct sockaddr_un	 sun;
	struct ctl_policy_id	 cpid;
	struct parse_result	*res;
	char			*sockname;
	int			 ctl_sock;
	int			 done = 0;
	int			 n, verbose = 0;
	int			 ch;

	sockname = NETCFGD_SOCKET;
	while ((ch = getopt(argc, argv, "s:")) != -1) {
		switch (ch) {
		case 's':
			sockname = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* Parse command line. */
	if ((res = parse(argc, argv)) == NULL)
		exit(1);

	/* Connect to control socket. */
	if ((ctl_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;

	strlcpy(sun.sun_path, sockname, sizeof(sun.sun_path));
	if (connect(ctl_sock, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "connect: %s", sockname);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if ((ibuf = malloc(sizeof(struct imsgbuf))) == NULL)
		err(1, NULL);
	imsg_init(ibuf, ctl_sock);
	done = 0;

	/* Process user request. */
	switch (res->action) {
	case KILL_XID:
		imsg_compose(ibuf, IMSG_CTL_KILL_PROPOSAL, 0, 0, -1,
		    &res->payload, sizeof(res->payload));
		printf("kill proposal '%0x' request send.\n", res->payload);
		done = 1;
		break;
	case DISCARD_XID:
		imsg_compose(ibuf, IMSG_CTL_DISCARD_PROPOSAL, 0, 0, -1,
		    &res->payload, sizeof(res->payload));
		printf("discard proposal '%0x' request send.\n", res->payload);
		done = 1;
		break;
	case LOG_VERBOSE:
		verbose = 1;
		/* FALLTHROUGH */
	case LOG_BRIEF:
		imsg_compose(ibuf, IMSG_CTL_LOG_LEVEL, 0, 0, -1,
		    &verbose, sizeof(verbose));
		printf("logging request sent.\n");
		done = 1;
		break;
	case SHOW_MAIN:
		imsg_compose(ibuf, IMSG_CTL_SHOW_MAIN_INFO, 0, 0, -1, NULL, 0);
		break;
	case SHOW_STATIC:
		cpid.ifindex = res->ifindex;
		cpid.source = RTP_PROPOSAL_STATIC;
		imsg_compose(ibuf, IMSG_CTL_SHOW_PROPOSALS, 0, 0, -1,
		    &cpid, sizeof(cpid));
		break;
	case SHOW_DHCLIENT:
		cpid.ifindex = res->ifindex;
		cpid.source = RTP_PROPOSAL_DHCLIENT;
		imsg_compose(ibuf, IMSG_CTL_SHOW_PROPOSALS, 0, 0, -1,
		    &cpid, sizeof(cpid));
		break;
	case SHOW_SLAAC:
		cpid.ifindex = res->ifindex;
		cpid.source = RTP_PROPOSAL_SLAAC;
		imsg_compose(ibuf, IMSG_CTL_SHOW_PROPOSALS, 0, 0, -1,
		    &cpid, sizeof(cpid));
		break;
	case SHOW_PROPOSALS:
		cpid.ifindex = res->ifindex;
		cpid.source = 0;
		imsg_compose(ibuf, IMSG_CTL_SHOW_PROPOSALS, 0, 0, -1,
		    &cpid, sizeof(cpid));
		break;
	case SHOW_FRONTEND:
		imsg_compose(ibuf, IMSG_CTL_SHOW_FRONTEND_INFO, 0, 0, -1,
		    NULL, 0);
		break;
	case RELOAD:
		imsg_compose(ibuf, IMSG_CTL_RELOAD, 0, 0, -1, NULL, 0);
		printf("reload request sent.\n");
		done = 1;
		break;
	case ENABLE_SOURCE:
		cpid.ifindex = res->ifindex;
		cpid.source = res->payload;
		imsg_compose(ibuf, IMSG_CTL_SET_SOURCE_STATE, 0, 0, -1,
		    &cpid, sizeof(cpid));
		done = 1;
		break;
	case DISABLE_SOURCE:
		cpid.ifindex = res->ifindex;
		cpid.source = -res->payload;
		imsg_compose(ibuf, IMSG_CTL_SET_SOURCE_STATE, 0, 0, -1,
		    &cpid, sizeof(cpid));
		done = 1;
		break;
	default:
		usage();
	}

	while (ibuf->w.queued)
		if (msgbuf_write(&ibuf->w) <= 0 && errno != EAGAIN)
			err(1, "write error");

	while (!done) {
		if ((n = imsg_read(ibuf)) == -1 && errno != EAGAIN)
			errx(1, "imsg_read error");
		if (n == 0)
			errx(1, "pipe closed");

		while (!done) {
			if ((n = imsg_get(ibuf, &imsg)) == -1)
				errx(1, "imsg_get error");
			if (n == 0)
				break;

			switch (res->action) {
			case SHOW_MAIN:
				done = show_main_msg(&imsg);
				break;
			case SHOW_DHCLIENT:
			case SHOW_SLAAC:
			case SHOW_STATIC:
			case SHOW_PROPOSALS:
				done = show_proposals_msg(&imsg);
				break;
			case SHOW_FRONTEND:
				done = show_frontend_msg(&imsg);
				break;
			default:
				break;
			}
			imsg_free(&imsg);
		}
	}
	close(ctl_sock);
	free(ibuf);

	return (0);
}

int
show_main_msg(struct imsg *imsg)
{
	struct ctl_main_info *cmi;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_MAIN_INFO:
		cmi = imsg->data;
		printf("main says: '%s'\n", cmi->text);
		break;
	case IMSG_CTL_END:
		return (1);
	default:
		break;
	}

	return (0);
}

int
show_proposals_msg(struct imsg *imsg)
{
	char			 ifname[IF_NAMESIZE];
	char			 buf[INET6_ADDRSTRLEN];
	const char		*pbuf;
	struct imsg_v4proposal	*p4;
	struct imsg_v6proposal	*p6;

	switch (imsg->hdr.type) {
	case IMSG_CTL_REPLY_V4PROPOSAL:
		p4 = imsg->data;
		pbuf = if_indextoname(p4->index, ifname);
		printf("xid: %0x index: %d (%s) source: %d ", p4->xid,
		    p4->index, pbuf ? pbuf : "???", p4->source);
		switch (p4->source) {
		case RTP_PROPOSAL_DHCLIENT:
			printf("(dhclient)");
			break;
		case RTP_PROPOSAL_STATIC:
			printf("(static)");
			break;
		case RTP_PROPOSAL_SLAAC:
			printf("(slaac)");
			break;
		default:
			printf("(\?\?\?)");
			break;
		}
		if (p4->inits & RTV_MTU)
			printf("mtu: %d\n", p4->mtu);
		printf("\n");
		if (p4->addrs & RTA_IFA) {
			pbuf = inet_ntop(AF_INET, &p4->ifa, buf,
			    INET_ADDRSTRLEN);
			printf("                 ifa: %s\n",
			    pbuf ? buf : strerror(errno));
		}
		if (p4->addrs & RTA_NETMASK) {
			pbuf = inet_ntop(AF_INET, &p4->netmask, buf,
			    INET_ADDRSTRLEN);
			printf("             netmask: %s\n",
			    pbuf ? buf : strerror(errno));
		}
		if (p4->addrs & RTA_DNS) {
			unsigned int cnt, i;
			struct in_addr addr;
			cnt = p4->rtdns_len / sizeof(struct in_addr);
			printf("                 dns: ");
			for (i = 0; i < cnt; i++) {
				memcpy(&addr.s_addr, &p4->rtdns[i],
				    sizeof(addr.s_addr));
				printf("%s ", inet_ntoa(addr));
			}
			printf("\n");
		}
		if (p4->addrs & RTA_SEARCH) {
			printf("              search: %*s\n",
			    p4->rtsearch_len, p4->rtsearch);
		}
		break;
	case IMSG_CTL_REPLY_V6PROPOSAL:
		p6 = imsg->data;
		printf("xid: %d index: %d source: %d ", p6->xid, p6->index,
		    p6->source);
		if (p6->inits & RTV_MTU)
			printf("mtu: %d\n", p6->mtu);
		printf("\n");
		if (p6->addrs & RTA_IFA) {
			pbuf = inet_ntop(AF_INET6, &p6->ifa, buf,
			    INET6_ADDRSTRLEN);
			printf("                 ifa: %s\n",
			    pbuf ? buf : strerror(errno));
		}
		if (p6->addrs & RTA_NETMASK) {
			pbuf = inet_ntop(AF_INET6, &p6->netmask, buf,
			    INET6_ADDRSTRLEN);
			printf("             netmask: %s\n",
			    pbuf ? buf : strerror(errno));
		}
		if (p6->addrs & RTA_DNS) {
			struct in6_addr addr;
			unsigned int cnt, i;
			cnt = p6->rtdns_len / sizeof(struct in6_addr);
			printf("                dns: ");
			for (i = 0; i < cnt; i++) {
				memcpy(&addr, &p6->rtdns[i], sizeof(addr));
				pbuf = inet_ntop(AF_INET6, &addr, buf,
				    INET6_ADDRSTRLEN);
				printf("%s ", pbuf ? buf : strerror(errno));
			}
			printf("\n");
		}
		if (p6->addrs & RTA_SEARCH) {
			printf("             search: %*s\n",
			    p6->rtsearch_len, p6->rtsearch);
		}
		printf("\n");
		break;
	case IMSG_CTL_END:
		return (1);
	default:
		break;
	}

	return (0);
}

int
show_frontend_msg(struct imsg *imsg)
{
	struct ctl_frontend_info *cfi;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_FRONTEND_INFO:
		cfi = imsg->data;
		printf("frontend says: %d %d '%s'",
		    cfi->yesno, cfi->integer, cfi->global_text);
		printf("\n");
		break;
	case IMSG_CTL_END:
		return (1);
	default:
		break;
	}

	return (0);
}

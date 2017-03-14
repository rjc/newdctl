/*	$OpenBSD$	*/

/*
 * Copyright (c) 2017 Kenneth R Westerback <krw@openbsd.org>
 * Copyright (c) 2004 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "netcfgd.h"
#include "parser.h"

enum token_type {
	NOTOKEN,
	ENDTOKEN,
	IFNAME,
	XID,
	SOURCE,
	KEYWORD
};

struct token {
	enum token_type		 type;
	const char		*keyword;
	int			 value;
	const struct token	*next;
};

static const struct token t_main[];
static const struct token t_loglevel[];
static const struct token t_show[];
static const struct token t_ifname[];
static const struct token t_xid[];
static const struct token t_source[];

static const struct token t_main[] = {
	{KEYWORD,	"reload",	RELOAD,		NULL},
	{KEYWORD,	"show",		NONE,		t_show},
	{KEYWORD,	"log",		NONE,		t_loglevel},
	{KEYWORD,	"kill",		KILL_XID,	t_xid},
	{KEYWORD,	"enable",	ENABLE_SOURCE,	t_source},
	{KEYWORD,	"disable",	DISABLE_SOURCE,	t_source},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_loglevel[] = {
	{KEYWORD,	"verbose",	LOG_VERBOSE,	NULL},
	{KEYWORD,	"brief",	LOG_BRIEF,	NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show[] = {
	{KEYWORD,	"proposals",	SHOW_PROPOSALS,	t_ifname},
	{KEYWORD,	"main",		SHOW_MAIN,	NULL},
	{KEYWORD,	"frontend",	SHOW_FRONTEND,	NULL},
	{KEYWORD,	"static",	SHOW_STATIC,	t_ifname},
	{KEYWORD,	"dhclient",	SHOW_DHCLIENT,	t_ifname},
	{KEYWORD,	"slaac",	SHOW_SLAAC,	t_ifname},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_source[] = {
	{SOURCE,	"static",	NONE,	t_ifname},
	{SOURCE,	"dhclient",	NONE,	t_ifname},
	{SOURCE,	"slaac",	NONE,	t_ifname},
	{ENDTOKEN,	"",		NONE,	NULL}
};

static const struct token t_ifname[] = {
	{NOTOKEN,	"",		NONE,		NULL},
	{IFNAME,	"",		NONE,		NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_xid[] = {
	{XID,		"",		NONE,	NULL},
	{ENDTOKEN,	"",		NONE,	NULL}
};

static const struct token *match_token(const char *, const struct token *,
    struct parse_result *);
static void show_valid_args(const struct token *);

struct parse_result *
parse(int argc, char *argv[])
{
	static struct parse_result	res;
	const struct token	*table = t_main;
	const struct token	*match;

	memset(&res, 0, sizeof(res));

	while (argc >= 0) {
		if ((match = match_token(argv[0], table, &res)) == NULL) {
			fprintf(stderr, "valid commands/args:\n");
			show_valid_args(table);
			return (NULL);
		}

		argc--;
		argv++;

		if (match->type == NOTOKEN || match->next == NULL)
			break;

		table = match->next;
	}

	if (argc > 0) {
		fprintf(stderr, "superfluous argument: %s\n", argv[0]);
		return (NULL);
	}

	return (&res);
}

static const struct token *
match_token(const char *word, const struct token *table,
    struct parse_result *res)
{
	char			 ifname[IF_NAMESIZE];
	const struct token	*t = NULL;
	size_t			 n;
	u_int			 i, match, index;

	match = 0;

	for (i = 0; table[i].type != ENDTOKEN; i++) {
		switch (table[i].type) {
		case NOTOKEN:
			if (word == NULL || strlen(word) == 0) {
				match++;
				t = &table[i];
			}
			break;
		case IFNAME:
			if (!match && word != NULL && strlen(word) > 0) {
				n = strlcpy(ifname, word,
				    sizeof(ifname));
				if (n >= sizeof(ifname))
					err(1, "ifname too long");
				index = if_nametoindex(ifname);
				if (index == 0)
					err(1, "'%s'", ifname);
				res->ifindex = index;
				match++;
				t = &table[i];
				if (t->value)
					res->action = t->value;
			}
			break;
		case XID:
			if (!match && word != NULL && strlen(word) > 0) {
				char *ep;
				long lval;
				unsigned int val;
				errno = 0;
				lval = strtol(word, &ep, 16);
				if (word[0] == '\0' || *ep != '\0')
					errx(1, "xid is not a number");
				if ((errno == ERANGE && (lval == LONG_MAX ||
				    lval == LONG_MIN)) || (lval > UINT_MAX ||
				    lval < 0))
					errx(1, "xid is out of range %ld",
					    lval);
				val = lval;
				memcpy(&res->payload, &val,
				    sizeof(res->payload));
				match++;
				t = &table[i];
				if (t->value)
					res->action = t->value;
			}
			break;
		case SOURCE:
			if (!match && word != NULL && strlen(word) > 0) {
				if (strcmp(word, "dhclient") == 0)
					res->payload = RTP_PROPOSAL_DHCLIENT;
				else if (strcmp(word, "slaac") == 0)
					res->payload = RTP_PROPOSAL_SLAAC;
				else if (strcmp(word, "static") == 0)
					res->payload = RTP_PROPOSAL_STATIC;
				else {
					fprintf(stderr, "not a good source");
					break;
				}
				match++;
				t = &table[i];
				if (t->value)
					res->action = t->value;
			}
			break;
		case KEYWORD:
			if (word != NULL && strncmp(word, table[i].keyword,
			    strlen(word)) == 0) {
				match++;
				t = &table[i];
				if (t->value)
					res->action = t->value;
			}
			break;
		case ENDTOKEN:
			break;
		}
	}

	if (match != 1) {
		if (word == NULL)
			fprintf(stderr, "missing argument:\n");
		else if (match > 1)
			fprintf(stderr, "ambiguous argument: %s\n", word);
		else if (match < 1)
			fprintf(stderr, "unknown argument: %s\n", word);
		return (NULL);
	}

	return (t);
}

static void
show_valid_args(const struct token *table)
{
	int	i;

	for (i = 0; table[i].type != ENDTOKEN; i++) {
		switch (table[i].type) {
		case NOTOKEN:
			fprintf(stderr, "  <cr>\n");
			break;
		case IFNAME:
			fprintf(stderr, " <ifname>\n");
			break;
		case KEYWORD:
			fprintf(stderr, "  %s\n", table[i].keyword);
			break;
		case XID:
			fprintf(stderr, " <xid>\n");
			break;
		case SOURCE:
			fprintf(stderr, " dhclient | slaac | static");
			break;
		case ENDTOKEN:
			break;
		}
	}
}

/*	$OpenBSD$	*/

/*
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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
	KEYWORD
};

struct token {
	enum token_type		 type;
	const char		*keyword;
	int			 value;
	const struct token	*next;
};

static const struct token t_main[];
static const struct token t_log[];
static const struct token t_show[];
static const struct token t_show_proposals[];
static const struct token t_kill[];

static const struct token t_main[] = {
	{KEYWORD,	"reload",	RELOAD,		NULL},
	{KEYWORD,	"show",		SHOW,		t_show},
	{KEYWORD,	"log",		NONE,		t_log},
	{KEYWORD,	"kill",		NONE,		t_kill},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_log[] = {
	{KEYWORD,	"verbose",	LOG_VERBOSE,	NULL},
	{KEYWORD,	"brief",	LOG_BRIEF,	NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show[] = {
	{KEYWORD,	"proposals",	SHOW_PROPOSALS,	t_show_proposals},
	{KEYWORD,	"main",		SHOW_MAIN,	NULL},
	{KEYWORD,	"frontend",	SHOW_FRONTEND,	NULL},
	{KEYWORD,	"dhclient",	SHOW_DHCLIENT,	NULL},
	{KEYWORD,	"slaac",	SHOW_SLAAC,	NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_show_proposals[] = {
	{NOTOKEN,	"",		NONE,		NULL},
	{IFNAME,	"",		SHOW_PROPOSALS,	NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
};

static const struct token t_kill[] = {
	{XID,		"",		KILL_XID,	NULL},
	{ENDTOKEN,	"",		NONE,		NULL}
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
	size_t			 n;
	u_int			 i, match;
	const struct token	*t = NULL;

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
				memset(res->ifname, 0,
				    sizeof(res->ifname));
				n = strlcpy(res->ifname, word,
				    sizeof(res->ifname));
				if (n >= sizeof(res->ifname))
					err(1, "ifname too long");
				match++;
				t = &table[i];
				if (t->value)
					res->action = t->value;
			}
			break;
		case XID:
			if (!match && word != NULL && strlen(word) > 0) {
				const char *errstr;
				res->xid = strtonum(word, INT_MIN, INT_MAX,
				    &errstr);
				if (errstr != NULL)
					errx(1, "xid is %s:%s", errstr, word);
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
		case ENDTOKEN:
			break;
		}
	}
}

/*
 * Copyright (c) 2013 Matt Dainty <matt@bodgit-n-scarper.com>
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

#ifndef _GRAPHITE_H
#define _GRAPHITE_H

#include <sys/time.h>

#include <event2/event.h>

#define	GRAPHITE_DEFAULT_PORT	(2003)

#define	GRAPHITE_PART_METRIC	0
#define	GRAPHITE_PART_VALUE	1
#define	GRAPHITE_PART_TIMESTAMP	2

struct graphite_connection {
	struct evdns_base	 *dns;
	struct bufferevent	 *bev;

	char			 *host;
	unsigned short		  port;

	/* (Re)connect */
	struct event		 *connect_ev;
	struct timeval		  connect_tv[2];
	int			  connect_index;

	/* Bytes sent */
	unsigned long long	  bytes_tx;

	/* Metrics sent */
	unsigned long long	  metrics_tx;

	/* Callbacks */
	void			(*connectcb)(struct graphite_connection *,
				    void *);
	void			(*disconnectcb)(struct graphite_connection *,
				    void *);
	void			 *arg;
};

int				 graphite_init(struct event_base *);
int				 graphite_parse(unsigned char *, unsigned char *,
				    unsigned char *[]);
struct graphite_connection	*graphite_connection_new(char *,
				    unsigned short, struct timeval);
void				 graphite_connection_setcb(struct graphite_connection *,
				    void (*connectcb)(struct graphite_connection *, void *),
				    void (*disconnectcb)(struct graphite_connection *, void *),
				    void *);
void				 graphite_connect(struct graphite_connection *);
void				 graphite_send(struct graphite_connection *,
				    char *, char *, char *);
void				 graphite_disconnect(struct graphite_connection *);
void				 graphite_connection_free(struct graphite_connection *);

#endif /* _GRAPHITE_H */

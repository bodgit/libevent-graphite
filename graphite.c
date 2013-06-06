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

#include <stdlib.h>
#include <string.h>

#include <event2/dns.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "graphite.h"

/* For use with strspn(3) */
#define	DIGITS		"0123456789"
#define	LOWER		"abcdefghijklmnopqrstuvwxyz"
#define	UPPER		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define	SPACE		" "
#define	UNDERSCORE	"_"
#define	MINUS		"-"
#define	PERIOD		"."

void	 graphite_read(struct bufferevent *, void *);
void	 graphite_event(struct bufferevent *, short, void *);
void	 graphite_count_tx(struct evbuffer *, const struct evbuffer_cb_info *,
	    void *arg);
void	 graphite_reconnect(int, short, void *);

struct event_base	*base;

int
graphite_init(struct event_base *b)
{
#ifdef __APPLE__
	if (!strcmp(event_base_get_method(b), "kqueue"))
		return (-1);
#endif

	base = b;

	return (0);
}

int
graphite_parse(unsigned char *key, unsigned char *line, unsigned char *part[])
{
	char	*ptr = line;
	size_t	 len;

	if (key != NULL) {
		if (strspn(key, LOWER UPPER DIGITS UNDERSCORE MINUS PERIOD) !=
		    strlen(key))
			goto bad;
		if (part)
			part[GRAPHITE_PART_METRIC] = key;
	} else {
		/* Scan the metric key, make a note of how long it is */
		if ((len = strspn(ptr,
		    LOWER UPPER DIGITS UNDERSCORE MINUS PERIOD)) == 0)
			goto bad;
		if (part)
			part[GRAPHITE_PART_METRIC] = ptr;
		ptr += len;

		/* Scan the spaces after the metric key */
		if ((len = strspn(ptr, SPACE)) == 0)
			goto bad;
		if (part)
			*ptr = '\0';
		ptr += len;
	}

	/* At this point, we're just concerned with the value and timestamp */

	/* Scan the metric value */
	if ((len = strspn(ptr, DIGITS MINUS PERIOD)) == 0)
		goto bad;
	if (part)
		part[GRAPHITE_PART_VALUE] = ptr;
	ptr += len;

	/* Scan the spaces after the metric value */
	if ((len = strspn(ptr, SPACE)) == 0)
		goto bad;
	if (part)
		*ptr = '\0';
	ptr += len;

	/* Scan the metric timestamp */
	if ((len = strspn(ptr, DIGITS)) == 0)
		goto bad;
	if (part)
		part[GRAPHITE_PART_TIMESTAMP] = ptr;
	ptr += len;

	/* If we reached the end and yet don't have a null, there's extra
	 * junk on the line so get rid of it
	 */
	if (*ptr != '\0')
		*ptr = '\0';

	return (0);
bad:
	return (-1);
}

void
graphite_read(struct bufferevent *bev, void *arg)
{
	//struct graphite_connection	*c = (struct graphite_connection *)arg;
	struct evbuffer			*input = bufferevent_get_input(bev);

	/* Just drain any input away */
	evbuffer_drain(input, evbuffer_get_length(input));
}

void
graphite_event(struct bufferevent *bev, short events, void *arg)
{
	struct graphite_connection	*c = (struct graphite_connection *)arg;

	if (events & BEV_EVENT_CONNECTED) {
		/* Reset backoff to immediate */
		c->connect_index = 0;

		if (c->connectcb)
			c->connectcb(c, c->arg);
	} else if (events & (BEV_EVENT_ERROR|BEV_EVENT_EOF)) {
		if (events & BEV_EVENT_ERROR) {
			int err = bufferevent_socket_get_dns_error(bev);
			if (err)
				fprintf(stderr, "DNS error: %s\n",
				    evutil_gai_strerror(err));
		}

		/* Tear down connection */
		bufferevent_free(c->bev);
		c->bev = NULL;

		if (c->disconnectcb)
			c->disconnectcb(c, c->arg);

		/* Schedule a reconnect attempt */
		evtimer_add(c->connect_ev,
		    &c->connect_tv[c->connect_index]);

		/* If this attempt is after no delay, set the next attempt (and
		 * all subsequent ones) to be after a delay
		 */
		if (c->connect_index == 0)
			c->connect_index++;
	}

	if (c->dns) {
		evdns_base_free(c->dns, 1);
		c->dns = NULL;
	}
}

void
graphite_count_tx(struct evbuffer *buffer, const struct evbuffer_cb_info *info,
    void *arg)
{
	struct graphite_connection *c = (struct graphite_connection *)arg;

	c->bytes_tx += info->n_deleted;
}

void
graphite_reconnect(int fd, short event, void *arg)
{
	struct graphite_connection	*c = (struct graphite_connection *)arg;

	c->dns = evdns_base_new(base, 1);

	c->bev = bufferevent_socket_new(base, -1,
	    BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);

	if (bufferevent_socket_connect_hostname(c->bev, c->dns, AF_UNSPEC,
            c->host, c->port) < 0) {
		/* Error starting connection */
		bufferevent_free(c->bev);
		return;
	}

	bufferevent_setcb(c->bev, graphite_read, NULL, graphite_event,
	    (void *)c);
	bufferevent_enable(c->bev, EV_READ|EV_WRITE);

	/* Add callbacks to the output buffers to track how much data we're
	 * transmitting
	 */
	evbuffer_add_cb(bufferevent_get_output(c->bev), graphite_count_tx,
	    (void *)c);
}

struct graphite_connection *
graphite_connection_new(char *host, unsigned short port, struct timeval tv)
{
	struct graphite_connection	*c;

	if ((c = calloc(1, sizeof(struct graphite_connection))) != NULL) {
		/* Set up timer to (re)connect */
		c->connect_ev = evtimer_new(base, graphite_reconnect,
		    (void *)c);
		c->connect_tv[1] = tv;

		/* Hostname */
		c->host = strdup(host);

		/* TCP port */
		c->port = port;
	}

	return (c);
}

void
graphite_connection_setcb(struct graphite_connection *c,
    void (*connectcb)(struct graphite_connection *, void *),
    void (*disconnectcb)(struct graphite_connection *, void *),
    void *arg)
{
	c->connectcb = connectcb;
	c->disconnectcb = disconnectcb;
	c->arg = arg;
}

void
graphite_connect(struct graphite_connection *c)
{
	evtimer_add(c->connect_ev, &c->connect_tv[0]);
	c->connect_index = 1;
}

void
graphite_send(struct graphite_connection *c, char *metric,
    char *value, char *timestamp)
{
	/* Yes, this is basically it */
	evbuffer_add_printf(bufferevent_get_output(c->bev), "%s %s %s\n",
	    metric, value, timestamp);

	c->metrics_tx++;
}

void
graphite_disconnect(struct graphite_connection *c)
{
	bufferevent_free(c->bev);
	c->bev = NULL;
	if (c->disconnectcb)
		c->disconnectcb(c, c->arg);
}

void
graphite_connection_free(struct graphite_connection *c)
{
	free(c->host);
	free(c);
}

libevent-graphite - asynchronous non-blocking Graphite client
=============================================================

Based around libevent bufferevents and designed to be used as part of a bigger
libevent-based application.

Example usage:

    #include <stdio.h>
    
    #include "graphite.h"
    
    void
    connect_cb(struct graphite_connection *c, void *arg)
    {
            struct event *ev = (struct event *)arg;
            struct timeval tv = { 60, 0 };
    
            printf("Connected\n");
    
            evtimer_add(ev, &tv);
    }
    
    void
    disconnect_cb(struct graphite_connection *c, void *arg)
    {
            struct event *ev = (struct event *)arg;
    
            printf("Disconnected\n");
    
            evtimer_del(ev);
    }
    
    void
    timer(int fd, short event, void *arg)
    {
            struct graphite_connection *c = (struct graphite_connection *)arg;
            struct timeval tv;
    
            gettimeofday(&tv, NULL);
    
            graphite_send(c, "universe.answer", "42", tv.tv_sec);
    }
    
    int
    main(int argc, char *argv[])
    {
            struct event_base *base;
            struct timeval tv = { 10, 0 };
            struct graphite_connection *c;
            struct event *ev;
    
            base = event_base_new();
    
            if (graphite_init(base) < 0)
                    return (-1);
    
            if ((c = graphite_connection_new("192.0.2.1",
                GRAPHITE_DEFAULT_PORT, tv)) == NULL)
                    return (-1);
    
            ev = event_new(base, -1, EV_PERSIST, timer, (void *)c);
    
            graphite_connection_setcb(c, connect_cb, disconnect_cb,
                (void *)ev);
    
            graphite_connect(c);
    
            event_base_dispatch(base);
    
            return (0);
    }

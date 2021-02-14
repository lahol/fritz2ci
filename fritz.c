#define _GNU_SOURCE
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <sys/types.h>
#include <unistd.h>
#include "netutils.h"
#include "fritz.h"
#include "logging.h"
#include <errno.h>
#include <time.h>

#define FRITZ_BUFFER_SIZE      1024

enum CIFritzServerState {
    CIFritzServerStateUninitialized = 0,
    CIFritzServerStateInitialized   = 1,
    CIFritzServerStateConnected     = 2,
    CIFritzServerStateListening     = 4
};

typedef struct _CIFritzServer {
    void (*fritz_listen_cb)(CIFritzCallMsg *);
    gchar *host;
    gushort port;
    gushort state;
    int sock;
    int netlink;
    int assoc_ifindex;
    int fdpipe[2];
    GThread *thread;
} CIFritzServer;

GMainContext *_main_context = NULL;

static gint _fritz_parse_notification(const gchar *notify, CIFritzCallMsg *cmsg);
static void *_fritz_listen_thread_proc(void *pdata);

/* callbacks for netlink messages */
static void _fritz_handle_net_up(int ifindex, char *ifname, CIFritzServer *srv);
static void _fritz_handle_net_down(int ifindex, char *ifname, CIFritzServer *srv);

static gint _fritz_connect(gboolean *connected);
static void _fritz_try_connect(void);
static gboolean _fritz_try_reconnect(CIFritzServer *srv);

CIFritzServer _cifritz_server;

gint fritz_init(gchar *host, gushort port)
{
    memset(&_cifritz_server, 0, sizeof(CIFritzServer));
    _main_context = g_main_context_default();

    if (!host) {
        return 1;
    }

    _cifritz_server.host = g_strdup(host);
    _cifritz_server.port = port;

    /* init netlink socket */
    if ((_cifritz_server.netlink = netutil_init_netlink()) == -1) {
        log_log("Error connecting netlink socket.\n");
        return 2;
    }

    if (pipe(_cifritz_server.fdpipe) != 0) {
        return 3;
    }

    _cifritz_server.state |= CIFritzServerStateInitialized;
    return 0;
}

static
gint _fritz_connect(gboolean *connected)
{
    char ifname[IF_NAMESIZE];

    if (connected) *connected = FALSE;
    if (!(_cifritz_server.state & CIFritzServerStateInitialized)) {
        return 4;
    }
    if (_cifritz_server.state & CIFritzServerStateConnected) {
        if (connected) *connected = TRUE;
        return 0;
    }

    struct sockaddr_in srv;
    _cifritz_server.sock = socket(AF_INET, SOCK_STREAM, 0);
    if (_cifritz_server.sock == -1) {
        return 2;
    }

    srv.sin_addr.s_addr = netutil_get_ip_address(_cifritz_server.host);
    srv.sin_port = htons((gushort)_cifritz_server.port);
    srv.sin_family = AF_INET;

    if (connect(_cifritz_server.sock, (const struct sockaddr *)&srv, sizeof(srv)) == 0) {
        if (netutil_get_interface_from_sock(_cifritz_server.sock, &_cifritz_server.assoc_ifindex, ifname) != 0) {
            log_log("fritz: could not determine associated interface\n");
        }
        log_log("fritz: connected via %s (%d)\n", ifname, _cifritz_server.assoc_ifindex);
        _cifritz_server.state |= CIFritzServerStateConnected;
        if (connected) *connected = TRUE;
    }
    else {
        log_log("fritz: failed to connect: %d (%s)\n", errno, strerror(errno));
        netutil_close_fd(&_cifritz_server.sock);
    }
    return 0;
}

gint fritz_startup(void (*fritz_listen_cb)(CIFritzCallMsg *))
{
    _cifritz_server.fritz_listen_cb = fritz_listen_cb;

    _fritz_try_connect();

    if ((_cifritz_server.thread = g_thread_new("Fritz", (GThreadFunc)_fritz_listen_thread_proc, NULL)) == NULL) {
        return 4;
    }

    return 0;
}

gint fritz_shutdown(void)
{
    size_t bytes;
    log_log("fritz: shutdown\n");
    if (_cifritz_server.state & CIFritzServerStateListening) {
        log_log("write to pipe\n");
        bytes = write(_cifritz_server.fdpipe[1], "disconnect", 10);
        if (bytes != 10)
            log_log("Error writing to pipe.\n");
        log_log("join: %p\n", _cifritz_server.thread);
        g_thread_join(_cifritz_server.thread);
        _cifritz_server.thread = NULL;
        log_log("joined\n");
        _cifritz_server.state &= ~CIFritzServerStateListening;
    }
    if (_cifritz_server.state & CIFritzServerStateConnected) {
        netutil_close_fd(&_cifritz_server.sock);
        _cifritz_server.state &= ~CIFritzServerStateConnected;
    }
    return 0;
}

gint fritz_cleanup(void)
{
    if (_cifritz_server.state >= CIFritzServerStateConnected) {
        fritz_shutdown();
    }
    netutil_close_fd(&_cifritz_server.fdpipe[0]);
    netutil_close_fd(&_cifritz_server.fdpipe[1]);
    netutil_cleanup(_cifritz_server.netlink);
    g_free(_cifritz_server.host);
    _cifritz_server.host = NULL;
    return 0;
}

static
gboolean _fritz_try_reconnect(CIFritzServer *srv)
{
    gboolean connected;
    _fritz_connect(&connected);

    if (srv && connected) {
        write(srv->fdpipe[1], "add sock", 8);
    }

    return !connected;
}

static
void _fritz_try_connect(void)
{
    if (_cifritz_server.state & CIFritzServerStateConnected)
        return;
    gboolean connected;
    _fritz_connect(&connected);

    if (!connected) {
        g_timeout_add_seconds(10, (GSourceFunc)_fritz_try_reconnect, (gpointer)&_cifritz_server);
    }
}

static
void _fritz_handle_net_up(int ifindex, char *ifname, CIFritzServer *srv)
{
    log_log("fritz: handle net up\n");
    _fritz_try_connect();
}

static
void _fritz_handle_net_down(int ifindex, char *ifname, CIFritzServer *srv)
{
    log_log("fritz: handle net down: %s (%d)\n", ifname, ifindex);
    if (ifindex == _cifritz_server.assoc_ifindex) {
        netutil_close_fd(&srv->sock);
        srv->state &= ~CIFritzServerStateConnected;
    }
    else {
        log_log("fritz: still connected via %d, ignoring\n", _cifritz_server.assoc_ifindex);
    }
}

static
void *_fritz_listen_thread_proc(void *pdata)
{
    log_log("fritz: start listening: %sconnected\n", _cifritz_server.state & CIFritzServerStateConnected ? "" : "not ");

    _cifritz_server.state |= CIFritzServerStateListening;
    gchar buffer[FRITZ_BUFFER_SIZE];
    int bytes;

    fd_set rfds;
    int max;

    CIFritzCallMsg cmsg;

    NetutilCallbacks nu_cb = {
        .net_up   = (NetutilCallback)_fritz_handle_net_up,
        .net_down = (NetutilCallback)_fritz_handle_net_down
    };

    while (1) {
        max = netutil_init_fd_set(&rfds, 3,
                                  _cifritz_server.sock,
                                  _cifritz_server.fdpipe[0],
                                  _cifritz_server.netlink);

        if (select(max, &rfds, NULL, NULL, NULL) < 0) {
            log_log("select returned negative value, terminating thread\n");
            return NULL;
        }

        if (FD_ISSET(_cifritz_server.sock, &rfds)) {
            if ((bytes = recv(_cifritz_server.sock, buffer, FRITZ_BUFFER_SIZE-1, 0)) >= 1) {
                buffer[bytes] = '\0';
                log_log("received message\n");
                if (_fritz_parse_notification(buffer, &cmsg) == 0) {
                    log_log("parsed message (cb: %p)\n", _cifritz_server.fritz_listen_cb);
                    if (_cifritz_server.fritz_listen_cb) {
                        (*_cifritz_server.fritz_listen_cb)(&cmsg);
                    }
                }
            }
            else {
                log_log("fritz: lost connection, trying to reconnect\n");
                _cifritz_server.state &= ~CIFritzServerStateConnected;
                netutil_close_fd(&_cifritz_server.sock);
            }
        }

        if (FD_ISSET(_cifritz_server.fdpipe[0], &rfds)) {
            bytes = read(_cifritz_server.fdpipe[0], buffer, FRITZ_BUFFER_SIZE-1);
            if (bytes) {
                if (!strncmp(buffer, "disconnect", bytes > 10 ? 10 : bytes)) {
                    log_log("received terminating signal\n");
                    _cifritz_server.state &= ~CIFritzServerStateListening;
                    return NULL;
                }
            }
        }

        if (FD_ISSET(_cifritz_server.netlink, &rfds)) {
            netutil_handle_netlink_message(_cifritz_server.netlink, &nu_cb, (void *)&_cifritz_server);
        }
    }
    _cifritz_server.state &= ~CIFritzServerStateListening;
    log_log("No file descriptor set -- returning\n");
    return NULL;
}

/* parse notifications:
 * date;CALL;connectionid;nst;msn;number;protocol;
 * date;RING;connectionid;number;msn;protocol;
 * date;CONNECT;connectionid;nst;number;
 * date;DISCONNECT;connectionid;duration;
 *
 * date: %d.%m.%y %H:%M:%S
 * duration: seconds
 */
static
gint _fritz_parse_notification(const gchar *notify, CIFritzCallMsg *cmsg)
{
    if (notify == NULL)
        return 1;
    if (cmsg == NULL)
        return 2;
    log_log("_fritz_parse_notification: %s\n", notify);

    memset(cmsg, 0, sizeof(CIFritzCallMsg));

    gchar **fields = g_strsplit(notify, ";", 0);

    /* all messages must have at least 4 fields */
    if (fields[0] == NULL || fields[1] == NULL ||
            fields[2] == NULL || fields[3] == NULL)
        goto out;

    strptime(fields[0], "%d.%m.%y %H:%M:%S", &cmsg->datetime);

    if (g_strcmp0(fields[1], "RING") == 0)
        cmsg->msgtype = CALLMSGTYPE_RING;
    else if (g_strcmp0(fields[1], "CALL") == 0)
        cmsg->msgtype = CALLMSGTYPE_CALL;
    else if (g_strcmp0(fields[1], "CONNECT") == 0)
        cmsg->msgtype = CALLMSGTYPE_CONNECT;
    else if (g_strcmp0(fields[1], "DISCONNECT") == 0)
        cmsg->msgtype = CALLMSGTYPE_DISCONNECT;
    else
        goto out;

    cmsg->connectionid = (gushort)atoi(fields[2]);

    switch (cmsg->msgtype) {
        case CALLMSGTYPE_RING:
            if (fields[4] == NULL)
                goto out;
            g_strlcpy(cmsg->calling_number, fields[3], 32);
            g_strlcpy(cmsg->called_number, fields[4], 32);
            break;
        case CALLMSGTYPE_CALL:
            if (fields[4] == NULL || fields[5] == NULL)
                goto out;
            cmsg->nst = (gushort)atoi(fields[3]);
            g_strlcpy(cmsg->calling_number, fields[4], 32);
            g_strlcpy(cmsg->called_number, fields[5], 32);
            break;
        case CALLMSGTYPE_CONNECT:
            if (fields[4] == NULL)
                goto out;
            cmsg->nst = (gushort)atoi(fields[3]);
            g_strlcpy(cmsg->calling_number, fields[4], 32);
            break;
        case CALLMSGTYPE_DISCONNECT:
            cmsg->duration = (gulong)strtoul(fields[3], NULL, 10);
            break;
    }

    g_strfreev(fields);
    return 0;

out:
    g_strfreev(fields);
    return 3;
}

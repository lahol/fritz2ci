#include <glib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
/*#include <pthread.h>*/
#include "ci2server.h"
#include "logging.h"
#include "netutils.h"
#include <errno.h>
#include <cinet.h>
#include "dbhandler.h"

typedef enum {
    CISrvStateUninitialized = 0,
    CISrvStateInitialized = 1,
    CISrvStateConnected,
    CISrvStateRunning,
} CIServerState;

#define CISRV_CLIENT_REMOVE              1 << 0

#define CI_MAKE_VERSION(maj, min, pat) (((maj) << 16 | (min) << 8 | (pat)) & 0x00ffffff)
#define CI_VERSION_MAJ(ver) (((ver) >> 16) & 0x000000ff)
#define CI_VERSION_MIN(ver) (((ver) >> 8) & 0x000000ff)
#define CI_VERSION_PAT(ver) ((ver) & 0x000000ff)
#define CI_CHECK_VERSION(ver, min) (CI_VERSION_MAJ(ver) >= CI_VERSION_MAJ(min) &&\
            CI_VERSION_MIN(ver) >= CI_VERSION_MIN(min) &&\
            CI_VERSION_PAT(ver) >= CI_VERSION_PAT(min))

typedef struct _CIClient {
    int sock;
    gulong flags;
    guint32 version;
} CIClient;

typedef struct _CIServer {
    int sock;
    gushort port;
    /*  pthread_t serverthread;*/
    GThread *serverthread;
    /*  pthread_mutex_t clist_lock;*/
    GMutex clist_lock;
    int fdpipe[2];
    gushort state;
    GSList *clientlist;
} CIServer;

typedef struct _CINetMessage {
    char msgCode;
    CIDataSet msgData;
} CINetMessage;

void _cisrv_add_client(int sock);
void _cisrv_remove_client(int sock);
void _cisrv_remove_marked_clients(void);
void _cisrv_close_all_clients(void);

void _cisrv_handle_client_message(CIClient *client);

void *_cisrv_listen_thread_proc(void *pdata);

CIServer _cisrv_server;

gint cisrv_init(void)
{
    memset(&_cisrv_server, 0, sizeof(CIServer));
    _cisrv_server.serverthread = NULL/*-1*/;
    _cisrv_server.sock = socket(AF_INET, SOCK_STREAM, 0);
    /*  pthread_mutex_init(&_cisrv_server.clist_lock, NULL);*/
    g_mutex_init(&_cisrv_server.clist_lock);
    if (_cisrv_server.sock == -1) {
        return 1;
    }
    _cisrv_server.state = CISrvStateInitialized;
    return 0;
}

gint cisrv_run(gushort port)
{
    /*  int rc;*/
    if (_cisrv_server.state == CISrvStateUninitialized) {
        return 1;
    }
    if (_cisrv_server.state == CISrvStateRunning) {
        return 2;
    }

    if (pipe(_cisrv_server.fdpipe) != 0) {
        return 3;
    }

    _cisrv_server.port = port;

    /*  if ((rc = pthread_create(&_cisrv_server.serverthread, NULL, _cisrv_listen_thread_proc, NULL))) {
        return 4;
      }*/
    if ((_cisrv_server.serverthread = g_thread_new("CIServer", (GThreadFunc)_cisrv_listen_thread_proc, NULL)) == NULL) {
        return 4;
    }
    _cisrv_server.state = CISrvStateRunning;
    return 0;
}

void *_cisrv_listen_thread_proc(void *pdata)
{
    fd_set fdSet;
    struct sockaddr_in addr;
    int rc;
    int max;
    int newsock;
    GSList *tmp;

    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(_cisrv_server.port);
    rc = wait_for_bind(_cisrv_server.sock, (const struct sockaddr *)&addr, sizeof(addr), _cisrv_server.fdpipe[0]);
    if (rc == -1) {
        return NULL;
    }
    if (rc == 1) {
        /* fdpipe has input, quit */
        return NULL;
    }
    rc = listen(_cisrv_server.sock, 10);
    if (rc == -1) {
        log_log("ci2server: Could not listen: %d (%s)\n", errno, strerror(errno));
        return NULL;
    }
    while (1) {
        FD_ZERO(&fdSet);
        FD_SET(_cisrv_server.sock, &fdSet);
        FD_SET(_cisrv_server.fdpipe[0], &fdSet);
        max = _cisrv_server.sock > _cisrv_server.fdpipe[0] ? _cisrv_server.sock : _cisrv_server.fdpipe[0];
        /*    pthread_mutex_lock(&_cisrv_server.clist_lock);*/
        g_mutex_lock(&_cisrv_server.clist_lock);
        tmp = _cisrv_server.clientlist;
        while (tmp) {
            FD_SET(((CIClient *)(tmp->data))->sock, &fdSet);
            if (((CIClient *)(tmp->data))->sock > max) {
                max = ((CIClient *)(tmp->data))->sock;
            }
            tmp = g_slist_next(tmp);
        }
        /*    pthread_mutex_unlock(&_cisrv_server.clist_lock);*/
        g_mutex_unlock(&_cisrv_server.clist_lock);
        rc = select(max+1, &fdSet, NULL, NULL, NULL);
        if (rc == -1) {
            return NULL;
        }
        if (FD_ISSET(_cisrv_server.sock, &fdSet)) {
            newsock = accept(_cisrv_server.sock, NULL, NULL);
            _cisrv_add_client(newsock);
            if (newsock > max) {
                max = newsock;
            }
        }
        /*    pthread_mutex_lock(&_cisrv_server.clist_lock);*/
        g_mutex_lock(&_cisrv_server.clist_lock);
        tmp = _cisrv_server.clientlist;
        while (tmp) {
            if (FD_ISSET(((CIClient *)tmp->data)->sock, &fdSet)) {
                _cisrv_handle_client_message((CIClient*)tmp->data);
            }
            tmp = g_slist_next(tmp);
        }
        /*    pthread_mutex_unlock(&_cisrv_server.clist_lock);*/
        g_mutex_unlock(&_cisrv_server.clist_lock);
        _cisrv_remove_marked_clients();

        if (FD_ISSET(_cisrv_server.fdpipe[0], &fdSet)) {
            return NULL;
        }
    }
}

gint cisrv_send_message(CIClient *client, gchar *buffer, gsize len)
{
    if (!client)
        return -1;

    send(client->sock, buffer, len, 0);
    return 0;
}

void _cisrv_handle_client_message_version(CIClient *client, CINetMsgVersion *msg)
{
    CINetMsg *reply = NULL;
    log_log("Client reports itself as %d.%d.%d (%s)\n",
            ((CINetMsgVersion*)msg)->major,
            ((CINetMsgVersion*)msg)->minor,
            ((CINetMsgVersion*)msg)->patch,
            ((CINetMsgVersion*)msg)->human_readable);
    client->version = CI_MAKE_VERSION(
            ((CINetMsgVersion*)msg)->major,
            ((CINetMsgVersion*)msg)->minor,
            ((CINetMsgVersion*)msg)->patch);

    reply = cinet_message_new(CI_NET_MSG_VERSION,
            "major", 3, "minor", 0, "patch", 0, "guid", ((CINetMsg*)msg)->guid, NULL, NULL);

    gchar *msgdata = NULL;
    gsize msglen = 0;

    cinet_msg_write_msg(&msgdata, &msglen, reply);

    cisrv_send_message(client, msgdata, msglen);
}

void _cisrv_handle_client_message_leave(CIClient *client, CINetMsgLeave *msg)
{
    log_log("Client wants to leave\n");
    client->flags |= CISRV_CLIENT_REMOVE;
    CINetMsg *reply = NULL;
    reply = cinet_message_new(CI_NET_MSG_LEAVE, "guid", ((CINetMsg*)msg)->guid, NULL, NULL);
    gchar *msgdata = NULL;
    gsize msglen = 0;

    cinet_msg_write_msg(&msgdata, &msglen, reply);

    cisrv_send_message(client, msgdata, msglen);

    cinet_msg_free(reply);
    g_free(msgdata);
}

void _cisrv_handle_client_message_db_num_calls(CIClient *client, CINetMsgDbNumCalls *msg)
{
    log_log("db_num_calls\n");

    gchar *msgdata = NULL;
    gsize msglen = 0;

    cinet_message_new_for_data(&msgdata, &msglen, CI_NET_MSG_DB_NUM_CALLS,
            "count", dbhandler_get_num_calls(), "guid", ((CINetMsg*)msg)->guid, NULL, NULL);

    cisrv_send_message(client, msgdata, msglen);

    g_free(msgdata);
}

void _cisrv_handle_client_message_db_call_list(CIClient *client, CINetMsgDbCallList *msg)
{
    log_log("db_call_list\n");

    gchar *msgdata = NULL;
    gsize msglen = 0;

    CINetMsgDbCallList *reply = NULL;

    GList *result = dbhandler_get_calls(msg->user, msg->min_id, msg->count);
    GList *tmp;
    CICallInfo *info;

    reply = (CINetMsgDbCallList*)cinet_message_new(CI_NET_MSG_DB_CALL_LIST,
            "user", msg->user,
            "min-id", msg->min_id,
            "count", msg->count,
            "guid", ((CINetMsg*)msg)->guid,
            NULL, NULL);

    for (tmp = result; tmp != NULL; tmp = g_list_next(tmp)) {
        info = g_malloc0(sizeof(CICallInfo));
        cinet_call_info_set_value(info, "id", GINT_TO_POINTER(((CIDbCall*)tmp->data)->id));
        cinet_call_info_set_value(info, "completenumber", ((CIDbCall*)tmp->data)->data.cidsNumberComplete);
        cinet_call_info_set_value(info, "areacode", ((CIDbCall*)tmp->data)->data.cidsAreaCode);
        cinet_call_info_set_value(info, "number", ((CIDbCall*)tmp->data)->data.cidsNumber);
        cinet_call_info_set_value(info, "date", ((CIDbCall*)tmp->data)->data.cidsDate);
        cinet_call_info_set_value(info, "time", ((CIDbCall*)tmp->data)->data.cidsTime);
        cinet_call_info_set_value(info, "msn", ((CIDbCall*)tmp->data)->data.cidsMSN);
        cinet_call_info_set_value(info, "alias", ((CIDbCall*)tmp->data)->data.cidsAlias);
        cinet_call_info_set_value(info, "area", ((CIDbCall*)tmp->data)->data.cidsArea);
        cinet_call_info_set_value(info, "name", ((CIDbCall*)tmp->data)->data.cidsName);

        reply->calls = g_list_prepend(reply->calls, (gpointer)info);
    }

    reply->calls = g_list_reverse(reply->calls);

    cinet_msg_write_msg(&msgdata, &msglen, (CINetMsg*)reply);

    cisrv_send_message(client, msgdata, msglen);

    g_list_free_full(result, g_free);
    cinet_msg_free((CINetMsg*)reply);
    g_free(msgdata);
}

void _cisrv_handle_client_message(CIClient *client)
{
    log_log("handle client message\n");
    char buffer[32];
    CINetMsgHeader header;
    ssize_t rc = recv(client->sock, buffer, CINET_HEADER_LENGTH, 0);
    if (rc == 0 || rc == -1) {
        client->flags |= CISRV_CLIENT_REMOVE;
        log_log("error: rc=%d\n", rc);
        return;
    }

    if (cinet_msg_read_header(&header, buffer, rc) < CINET_HEADER_LENGTH) {
        /* read everything left */
        log_log("header not complete\n");
        while (recv(client->sock, buffer, 32, MSG_DONTWAIT) > 0);
        return;
    }

    char *msgdata = malloc(CINET_HEADER_LENGTH + header.msglen);
    if (!msgdata) {
        log_log("no msg data\n");
        return;
    }

    memcpy(msgdata, buffer, CINET_HEADER_LENGTH);

    ssize_t bytes_read = 0;
    while (bytes_read < header.msglen) {
        rc = recv(client->sock, &msgdata[CINET_HEADER_LENGTH + bytes_read], header.msglen-bytes_read, MSG_DONTWAIT);
        if (rc <= 0) {
            client->flags |= CISRV_CLIENT_REMOVE;
            log_log("could not read rest of message\n");
            return;
        }

        bytes_read += rc;
    }

    CINetMsg *msg = NULL;
    if (cinet_msg_read_msg(&msg, msgdata, bytes_read + CINET_HEADER_LENGTH) == 0) {
        switch (msg->msgtype) {
            case CI_NET_MSG_VERSION:
                _cisrv_handle_client_message_version(client, (CINetMsgVersion*)msg);
                break;
            case CI_NET_MSG_LEAVE:
                _cisrv_handle_client_message_leave(client, (CINetMsgLeave*)msg);
                break;
            case CI_NET_MSG_DB_NUM_CALLS:
                _cisrv_handle_client_message_db_num_calls(client, (CINetMsgDbNumCalls*)msg);
                break;
            case CI_NET_MSG_DB_CALL_LIST:
                _cisrv_handle_client_message_db_call_list(client, (CINetMsgDbCallList*)msg);
                break;
            default:
                log_log("unhandled message from client: %d\n", msg->msgtype);
                break;
        }
    }
    else {
        log_log("error converting message\n");
    }
    cinet_msg_free(msg);
}

gint cisrv_broadcast_message(CI2ServerMsg msgtype, CIDataSet *data)
{
    GSList *tmp;
    CINetMessage cmsg;
    memset(&cmsg, 0, sizeof(CINetMessage));
    if (data) {
        memcpy(&cmsg.msgData, data, sizeof(CIDataSet));
    }

    CINetMsg *msg = NULL;
    if (data && (msgtype == CI2ServerMsgMessage || msgtype == CI2ServerMsgUpdate ||
                msgtype == CI2ServerMsgComplete)) {
        msg = cinet_message_new(CI_NET_MSG_EVENT_RING, NULL, NULL);
        cinet_message_set_value(msg, "completenumber", data->cidsNumberComplete[0] ?
                data->cidsNumberComplete : NULL);
        cinet_message_set_value(msg, "number", data->cidsNumber[0] ?
                data->cidsNumber : NULL);
        cinet_message_set_value(msg, "name", data->cidsName[0] ?
                data->cidsName : NULL);
        cinet_message_set_value(msg, "date", data->cidsDate[0] ?
                data->cidsDate : NULL);
        cinet_message_set_value(msg, "time", data->cidsTime[0] ?
                data->cidsTime : NULL);
        cinet_message_set_value(msg, "msn", data->cidsMSN[0] ?
                data->cidsMSN : NULL);
        cinet_message_set_value(msg, "alias", data->cidsAlias[0] ?
                data->cidsAlias : NULL);
        cinet_message_set_value(msg, "area", data->cidsArea[0] ?
                data->cidsArea : NULL);
        cinet_message_set_value(msg, "areacode", data->cidsAreaCode[0] ?
                data->cidsAreaCode : NULL);
    }
    else if (msgtype == CI2ServerMsgDisconnect) {
        msg = cinet_message_new(CI_NET_MSG_SHUTDOWN, NULL, NULL);
    }

    switch (msgtype) {
        case CI2ServerMsgMessage:
            cmsg.msgCode = 'm';
            cinet_message_set_value(msg, "stage", GINT_TO_POINTER(MultipartStageInit));
            break;
        case CI2ServerMsgUpdate:
            cmsg.msgCode = 'u';
            cinet_message_set_value(msg, "stage", GINT_TO_POINTER(MultipartStageUpdate));
            break;
        case CI2ServerMsgDisconnect:
            cmsg.msgCode = 'd';
            break;
        case CI2ServerMsgComplete:
            cmsg.msgCode = 'c';
            cinet_message_set_value(msg, "stage", GINT_TO_POINTER(MultipartStageComplete));
            break;
        default:
            return 1;
    }

    gchar *msgdata = NULL;
    gsize len = 0;
    gssize bytes_written , rc;

    cinet_msg_write_msg(&msgdata, &len, msg);
    cinet_msg_free(msg);

    g_mutex_lock(&_cisrv_server.clist_lock);
    tmp = _cisrv_server.clientlist;
    while (tmp) {
        log_log("broadcast to %p\n", tmp);
        bytes_written = 0;
        if (CI_CHECK_VERSION(((CIClient*)(tmp->data))->version, CI_MAKE_VERSION(3,0,0))) {
            while (bytes_written < len) {
                rc = send(((CIClient*)(tmp->data))->sock, &msgdata[bytes_written],
                        len - bytes_written, 0);
                if (rc <= 0) {
                    ((CIClient*)tmp->data)->flags |= CISRV_CLIENT_REMOVE;
                    break;
                }
                bytes_written += rc;
            }
        }
        else {
            if (send(((CIClient *)(tmp->data))->sock, &cmsg, sizeof(CINetMessage), 0) < sizeof(CINetMessage)) {
                ((CIClient *)(tmp->data))->flags |= CISRV_CLIENT_REMOVE;
            }
        }
        tmp = g_slist_next(tmp);
    }
    g_mutex_unlock(&_cisrv_server.clist_lock);

    g_free(msgdata);

    _cisrv_remove_marked_clients();
    return 0;
}

gint cisrv_disconnect(void)
{
    log_log("cisrv_disconnect\n");
    int bval;
    size_t bytes;
    switch (_cisrv_server.state) {
        case CISrvStateRunning:
            log_log("write to pipe\n");
            bytes = write(_cisrv_server.fdpipe[1], "disconnect", 10);
            if (bytes != 10) {
                log_log("Error writing to pipe.\n");
            }
            if (_cisrv_server.serverthread) {
                log_log("cisrv: join: %p\n", _cisrv_server.serverthread);
                g_thread_join(_cisrv_server.serverthread);
                _cisrv_server.serverthread = NULL;
                log_log("joined\n");
            }
            close(_cisrv_server.fdpipe[0]);
            close(_cisrv_server.fdpipe[1]);
            _cisrv_server.state = CISrvStateConnected;
        case CISrvStateConnected:
            log_log("broadcast message\n");
            cisrv_broadcast_message(CI2ServerMsgDisconnect, NULL);
            _cisrv_server.state = CISrvStateInitialized;
        case CISrvStateInitialized:
            break;
    }
    if (_cisrv_server.state != CISrvStateUninitialized) {
        _cisrv_server.state = CISrvStateInitialized;
        log_log("close all clients\n");
        _cisrv_close_all_clients();
        bval = 1;
        setsockopt(_cisrv_server.sock, SOL_SOCKET, SO_REUSEADDR, &bval, sizeof(int));
        close(_cisrv_server.sock);
        _cisrv_server.sock = -1;
    }
    log_log("cisrv_disconnect done\n");
    return 0;
}

void _cisrv_close_all_clients(void)
{
    g_mutex_lock(&_cisrv_server.clist_lock);
    while (_cisrv_server.clientlist) {
        close(((CIClient *)_cisrv_server.clientlist->data)->sock);
        g_free((CIClient *)_cisrv_server.clientlist->data);
        _cisrv_server.clientlist = g_slist_remove(_cisrv_server.clientlist, _cisrv_server.clientlist->data);
    }
    g_mutex_unlock(&_cisrv_server.clist_lock);
}

void _cisrv_remove_marked_clients(void)
{
    GSList *tmp, *next;
    g_mutex_lock(&_cisrv_server.clist_lock);
    /*  pthread_mutex_lock(&_cisrv_server.clist_lock);*/
    tmp = _cisrv_server.clientlist;
    while (tmp) {
        next = g_slist_next(tmp);
        if (((CIClient *)tmp->data)->flags & CISRV_CLIENT_REMOVE) {
            log_log("removing client %d\n", ((CIClient*)tmp->data)->sock);
            close(((CIClient *)tmp->data)->sock);
            g_free((CIClient *)tmp->data);
            _cisrv_server.clientlist = g_slist_remove(_cisrv_server.clientlist, tmp->data);
        }
        tmp = next;
    }
    g_mutex_unlock(&_cisrv_server.clist_lock);
    /*  pthread_mutex_unlock(&_cisrv_server.clist_lock);*/
}

void _cisrv_add_client(int sock)
{
    CIClient *cl = g_malloc0(sizeof(CIClient));
    log_log("cisrv_add_client\n");
    cl->sock = sock;
    /* assume that client version is at least 2.0.0 until we receive a version message */
    cl->version = CI_MAKE_VERSION(2,0,0);
    /*  pthread_mutex_lock(&_cisrv_server.clist_lock);*/
    g_mutex_lock(&_cisrv_server.clist_lock);
    _cisrv_server.clientlist = g_slist_append(_cisrv_server.clientlist, (gpointer)cl);
    g_mutex_unlock(&_cisrv_server.clist_lock);
    /*  pthread_mutex_unlock(&_cisrv_server.clist_lock);*/
}

void _cisrv_remove_client(int sock)
{
    GSList *tmp;
    log_log("cisrv_remove_client\n");
    g_mutex_lock(&_cisrv_server.clist_lock);
    /*  pthread_mutex_lock(&_cisrv_server.clist_lock);*/
    tmp = _cisrv_server.clientlist;
    while (tmp) {
        if (((CIClient *)tmp->data)->sock == sock) {
            close(sock);
            g_free((CIClient *)tmp->data);
            _cisrv_server.clientlist = g_slist_remove(_cisrv_server.clientlist, tmp->data);
            break;
        }
        tmp = g_slist_next(tmp);
    }
    g_mutex_unlock(&_cisrv_server.clist_lock);
    /*  pthread_mutex_unlock(&_cisrv_server.clist_lock);*/
}

gint cisrv_cleanup(void)
{
    g_mutex_clear(&_cisrv_server.clist_lock);
    return 0;
}

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

typedef enum {
  CISrvStateUninitialized = 0,
  CISrvStateInitialized = 1,
  CISrvStateConnected,
  CISrvStateRunning,
} CIServerState;

#define CISRV_CLIENT_REMOVE              1 << 0

typedef struct _CIClient {
  int sock;
  gulong flags;
} CIClient;

typedef struct _CIServer {
  int sock;
  gushort port;
/*  pthread_t serverthread;*/
  GThread * serverthread;
/*  pthread_mutex_t clist_lock;*/
  GMutex * clist_lock;
  int fdpipe[2];
  gushort state;
  GSList * clientlist;
} CIServer;

typedef struct _CINetMessage {
  char msgCode;
  CIDataSet msgData;
} CINetMessage;

void _cisrv_add_client(int sock);
void _cisrv_remove_client(int sock);
void _cisrv_remove_marked_clients(void);
void _cisrv_close_all_clients(void);

void* _cisrv_listen_thread_proc(void * pdata);

CIServer _cisrv_server;

gint cisrv_init(void) {
#if GLIB_MAJOR_VERSION >= 2 && GLIB_MINOR_VERSION >= 20
  if (!g_thread_get_initialized()) {
#else
  if (!g_thread_supported()) {
#endif
    g_thread_init(NULL);
  }
  memset(&_cisrv_server, 0, sizeof(CIServer));
  _cisrv_server.serverthread = NULL/*-1*/;
  _cisrv_server.sock = socket(AF_INET, SOCK_STREAM, 0);
/*  pthread_mutex_init(&_cisrv_server.clist_lock, NULL);*/
  _cisrv_server.clist_lock = g_mutex_new();
  if (_cisrv_server.sock == -1) {
    return 1;
  }
  _cisrv_server.state = CISrvStateInitialized;
  return 0;
}

gint cisrv_run(gushort port) {
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
  if ((_cisrv_server.serverthread = g_thread_create((GThreadFunc)_cisrv_listen_thread_proc, NULL, TRUE, NULL)) == NULL) {
    return 4;
  }
  _cisrv_server.state = CISrvStateRunning;
  return 0;
}

void* _cisrv_listen_thread_proc(void * pdata) {
  fd_set fdSet;
  struct sockaddr_in addr;
  int rc;
  int max;
  int newsock;
  GSList * tmp;
  char buffer[256];
  
  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(_cisrv_server.port);
  rc = bind(_cisrv_server.sock, (const struct sockaddr*)&addr, sizeof(addr));
  if (rc == -1) {
    return NULL;
  }
  rc = listen(_cisrv_server.sock, 10);
  if (rc == -1) {
    return NULL;
  }
  while (1) {
    FD_ZERO(&fdSet);
    FD_SET(_cisrv_server.sock, &fdSet);
    FD_SET(_cisrv_server.fdpipe[0], &fdSet);
    max = _cisrv_server.sock > _cisrv_server.fdpipe[0] ? _cisrv_server.sock : _cisrv_server.fdpipe[0];
/*    pthread_mutex_lock(&_cisrv_server.clist_lock);*/
    g_mutex_lock(_cisrv_server.clist_lock);
    tmp = _cisrv_server.clientlist;
    while (tmp) {
      FD_SET(((CIClient*)(tmp->data))->sock, &fdSet);
      if (((CIClient*)(tmp->data))->sock > max) {
        max = ((CIClient*)(tmp->data))->sock;
      }
      tmp = g_slist_next(tmp);
    }
/*    pthread_mutex_unlock(&_cisrv_server.clist_lock);*/
    g_mutex_unlock(_cisrv_server.clist_lock);
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
    g_mutex_lock(_cisrv_server.clist_lock);
    tmp = _cisrv_server.clientlist;
    while (tmp) {
      if (FD_ISSET(((CIClient*)tmp->data)->sock, &fdSet)) {
        rc = recv(((CIClient*)tmp->data)->sock, buffer, 255, 0);
        if (rc == 0 || rc == -1) {
          ((CIClient*)tmp->data)->flags |= CISRV_CLIENT_REMOVE;
        }
      }
      tmp = g_slist_next(tmp);
    }
/*    pthread_mutex_unlock(&_cisrv_server.clist_lock);*/
    g_mutex_unlock(_cisrv_server.clist_lock);
    _cisrv_remove_marked_clients();
    
    if (FD_ISSET(_cisrv_server.fdpipe[0], &fdSet)) {
      return NULL;
    }
  }
}

gint cisrv_broadcast_message(CI2ServerMsg msgtype, CIDataSet * data) {
  GSList * tmp;
  CINetMessage cmsg;
  memset(&cmsg, 0, sizeof(CINetMessage));
  if (data) {
    memcpy(&cmsg.msgData, data, sizeof(CIDataSet));
  }
  switch (msgtype) {
    case CI2ServerMsgMessage: cmsg.msgCode = 'm'; break;
    case CI2ServerMsgUpdate: cmsg.msgCode = 'u'; break;
    case CI2ServerMsgDisconnect: cmsg.msgCode = 'd'; break;
    case CI2ServerMsgComplete: cmsg.msgCode = 'c'; break;
    default: return 1;
  }
/*  pthread_mutex_lock(&_cisrv_server.clist_lock);*/
  g_mutex_lock(_cisrv_server.clist_lock);
  tmp = _cisrv_server.clientlist;
  while (tmp) {
    send(((CIClient*)(tmp->data))->sock, &cmsg, sizeof(CINetMessage), 0);
    tmp = g_slist_next(tmp);
  }
  g_mutex_unlock(_cisrv_server.clist_lock);
/*  pthread_mutex_unlock(&_cisrv_server.clist_lock);*/
  return 0;
}

gint cisrv_disconnect(void) {
  log_log("cisrv_disconnect\n");
  int bval;
  size_t bytes;
  switch (_cisrv_server.state) {
    case CISrvStateRunning:
      bytes = write(_cisrv_server.fdpipe[1], "disconnect", 10);
      /*pthread_join(_cisrv_server.serverthread, NULL);*/
      g_thread_join(_cisrv_server.serverthread);
      close(_cisrv_server.fdpipe[0]);
      close(_cisrv_server.fdpipe[1]);
      _cisrv_server.serverthread = NULL/*-1*/;
      _cisrv_server.state = CISrvStateConnected;
    case CISrvStateConnected:
      cisrv_broadcast_message(CI2ServerMsgDisconnect, NULL);
      _cisrv_server.state = CISrvStateInitialized;
    case CISrvStateInitialized:
      break;
  }
  if (_cisrv_server.state != CISrvStateUninitialized) {
    _cisrv_server.state = CISrvStateInitialized;
    _cisrv_close_all_clients();
    bval = 1;
    setsockopt(_cisrv_server.sock, SOL_SOCKET, SO_REUSEADDR, &bval, sizeof(int));
    close(_cisrv_server.sock);
    _cisrv_server.sock = -1;
  }
  return 0;
}

void _cisrv_close_all_clients(void) {
/*  pthread_mutex_lock(&_cisrv_server.clist_lock);*/
  g_mutex_lock(_cisrv_server.clist_lock);
  while (_cisrv_server.clientlist) {
    close(((CIClient*)_cisrv_server.clientlist->data)->sock);
    g_free((CIClient*)_cisrv_server.clientlist->data);
    _cisrv_server.clientlist = g_slist_remove(_cisrv_server.clientlist, _cisrv_server.clientlist->data);
  }
  g_mutex_unlock(_cisrv_server.clist_lock);
/*  pthread_mutex_unlock(&_cisrv_server.clist_lock);*/
}

void _cisrv_remove_marked_clients(void) {
  GSList * tmp, *next;
  g_mutex_lock(_cisrv_server.clist_lock);
/*  pthread_mutex_lock(&_cisrv_server.clist_lock);*/
  tmp = _cisrv_server.clientlist;
  while (tmp) {
    next = g_slist_next(tmp);
    if (((CIClient*)tmp->data)->flags & CISRV_CLIENT_REMOVE) {
      close(((CIClient*)tmp->data)->sock);
      g_free((CIClient*)tmp->data);
      _cisrv_server.clientlist = g_slist_remove(_cisrv_server.clientlist, tmp->data);
    }
    tmp = next;
  }
  g_mutex_unlock(_cisrv_server.clist_lock);
/*  pthread_mutex_unlock(&_cisrv_server.clist_lock);*/
}

void _cisrv_add_client(int sock) {
  CIClient * cl = g_malloc0(sizeof(CIClient));
  cl->sock = sock;
/*  pthread_mutex_lock(&_cisrv_server.clist_lock);*/
  g_mutex_lock(_cisrv_server.clist_lock);
  _cisrv_server.clientlist = g_slist_append(_cisrv_server.clientlist, (gpointer)cl);
  g_mutex_unlock(_cisrv_server.clist_lock);
/*  pthread_mutex_unlock(&_cisrv_server.clist_lock);*/
}

void _cisrv_remove_client(int sock) {
  GSList * tmp;
  g_mutex_lock(_cisrv_server.clist_lock);
/*  pthread_mutex_lock(&_cisrv_server.clist_lock);*/
  tmp = _cisrv_server.clientlist;
  while (tmp) {
    if (((CIClient*)tmp->data)->sock == sock) {
      close(sock);
      g_free((CIClient*)tmp->data);
      _cisrv_server.clientlist = g_slist_remove(_cisrv_server.clientlist, tmp->data);
      break;
    }
    tmp = g_slist_next(tmp);
  }
  g_mutex_unlock(_cisrv_server.clist_lock);
/*  pthread_mutex_unlock(&_cisrv_server.clist_lock);*/
}

gint cisrv_cleanup(void) {
  g_mutex_free(_cisrv_server.clist_lock);
  return 0;
}

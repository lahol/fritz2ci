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

#define FRITZ_BUFFER_SIZE      1024

enum CIFritzServerState {
  CIFritzServerStateUninitialized = 0,
  CIFritzServerStateInitialized   = 1,
  CIFritzServerStateConnected     = 2,
  CIFritzServerStateListening     = 4
};

typedef struct _CIFritzServer {
  void (*fritz_listen_cb)(CIFritzCallMsg*);
  gchar * host;
  gushort port;
  gushort state;
  int sock;
  int netlink;
  int assoc_ifindex;
  int fdpipe[2];
  GThread * thread;
} CIFritzServer;

GMainContext * _main_context = NULL;

void _fritz_parse_date(gchar * buffer, struct tm * dt);
gint _fritz_parse_notification(const gchar * notify, CIFritzCallMsg * cmsg);
void * _fritz_listen_thread_proc(void * pdata);

/* callbacks for netlink messages */
void _fritz_handle_net_up(int ifindex, char *ifname, CIFritzServer *srv);
void _fritz_handle_net_down(int ifindex, char *ifname, CIFritzServer *srv);

gboolean _fritz_try_reconnect(CIFritzServer *srv);

CIFritzServer _cifritz_server;

gint fritz_init(gchar *host, gushort port) {
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

gint fritz_connect(gboolean *connected)
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
      
    if (connect(_cifritz_server.sock, (const struct sockaddr*)&srv, sizeof(srv)) == 0) {
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

gint fritz_listen(void (*fritz_listen_cb)(CIFritzCallMsg *)) {
  _cifritz_server.fritz_listen_cb = fritz_listen_cb;

  if ((_cifritz_server.thread = g_thread_new("Fritz", (GThreadFunc)_fritz_listen_thread_proc, NULL)) == NULL) {
    return 4;
  }

  return 0;
}

gint fritz_disconnect(void) {
  size_t bytes;
  log_log("fritz: disconnect\n");
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

gint fritz_cleanup(void) {
  if (_cifritz_server.state >= CIFritzServerStateConnected) {
    fritz_disconnect();
  }
  netutil_close_fd(&_cifritz_server.fdpipe[0]);
  netutil_close_fd(&_cifritz_server.fdpipe[1]);
  netutil_cleanup(_cifritz_server.netlink);
  g_free(_cifritz_server.host);
  _cifritz_server.host = NULL;
  return 0;
}

gboolean _fritz_try_reconnect(CIFritzServer *srv)
{
    gboolean connected;
    fritz_connect(&connected);

    if (srv && connected) {
        write(srv->fdpipe[1], "add sock", 8);
    }

    return !connected;
}

void _fritz_handle_net_up(int ifindex, char *ifname, CIFritzServer *srv)
{
    log_log("fritz: handle net up\n");
    if (srv->state & CIFritzServerStateConnected)
        return;
    gboolean connected;
    fritz_connect(&connected);

    if (!connected) {
        g_timeout_add_seconds(10, (GSourceFunc)_fritz_try_reconnect, (gpointer)srv);
    }
}

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

void * _fritz_listen_thread_proc(void * pdata) {
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
        netutil_handle_netlink_message(_cifritz_server.netlink, &nu_cb, (void*)&_cifritz_server);
    }
  }
  _cifritz_server.state &= ~CIFritzServerStateListening;
  log_log("No file descriptor set -- returning\n");
  return NULL;
}

void _fritz_parse_date(gchar * buffer, struct tm * dt) {
  char buf[4];
  strncpy(buf, &buffer[0], 2);
  dt->tm_mday = (unsigned short)atoi(buf);
  strncpy(buf, &buffer[3], 2);
  dt->tm_mon = (unsigned short)atoi(buf)-1;
  strncpy(buf, &buffer[6], 2);
  dt->tm_year = 100+(unsigned short)atoi(buf);
  strncpy(buf, &buffer[9], 2);
  dt->tm_hour = (unsigned short)atoi(buf);
  strncpy(buf, &buffer[12], 2);
  dt->tm_min = (unsigned short)atoi(buf);
  strncpy(buf, &buffer[15], 2);
  dt->tm_sec = (unsigned short)atoi(buf);
}

gint _fritz_parse_notification(const gchar * notify, CIFritzCallMsg * cmsg) {
  if (!notify) {
    return 1;
  }
  if (!cmsg) {
    return 2;
  }
  int i, j;
  char buffer[64];
  i = 0;
  j = 0;
  while (notify[i] != ';' && notify[i] != '\0' && notify[i] != 0xa && notify[i] != 0xd) {
    buffer[j] = notify[i];
    i++; j++;
  }
  buffer[j] = '\0';
  _fritz_parse_date(buffer, &cmsg->datetime);
  if (notify[i] == '\0') {
    return 3;
  }
  i++;
  j = 0;
  while (notify[i] != ';' && notify[i] != '\0' && notify[i] != 0xa && notify[i] != 0xd) {
    buffer[j] = notify[i];
    i++; j++;
  }
  buffer[j] = '\0';
  if (strcmp(buffer, "CALL") == 0) {
    cmsg->msgtype = CALLMSGTYPE_CALL;
  }
  else if (strcmp(buffer, "RING") == 0) {
    cmsg->msgtype = CALLMSGTYPE_RING;
  }
  else if (strcmp(buffer, "CONNECT") == 0) {
    cmsg->msgtype = CALLMSGTYPE_CONNECT;
  }
  else if (strcmp(buffer, "DISCONNECT") == 0) {
    cmsg->msgtype = CALLMSGTYPE_DISCONNECT;
  }
  else {
    return 4;
  }
  if (notify[i] == '\0') {
    return 3;
  }
  j = 0;
  i++;
  while (notify[i] != ';' && notify[i] != '\0' && notify[i] != 0xa && notify[i] != 0xd) {
    buffer[j] = notify[i];
    i++; j++;
  }
  buffer[j] = '\0';
  cmsg->connectionid = (unsigned short)atoi(buffer);
	
  if (notify[i] == '\0') {
    return 3;
  }
  i++; j = 0;
	
  switch (cmsg->msgtype) {
    case CALLMSGTYPE_CALL:
      while (notify[i] != ';' && notify[i] != '\0' && notify[i] != 0xa && notify[i] != 0xd) {
        buffer[j] = notify[i];
        i++; j++;
      }
      buffer[j] = '\0';
      if (notify[i] == '\0') {
        return 3;
      }
      i++; j = 0;
      while (notify[i] != ';' && notify[i] != '\0' && notify[i] != 0xa && notify[i] != 0xd) {
        cmsg->calling_number[j] = notify[i];
        i++; j++;
      }
      cmsg->calling_number[j] = '\0';
      if (notify[i] == '\0') {
        return 3;
      }
      i++; j = 0;
      while (notify[i] != ';' && notify[i] != '\0' && notify[i] != 0xa && notify[i] != 0xd) {
        cmsg->called_number[j] = notify[i];
        i++; j++;
      }
      cmsg->called_number[j] = '\0';
      if (notify[i] == '\0') {
        return 3;
      }
      i++; j = 0;
      break;
    case CALLMSGTYPE_RING:
      while (notify[i] != ';' && notify[i] != '\0' && notify[i] != 0xa && notify[i] != 0xd) {
        cmsg->calling_number[j] = notify[i];
        i++; j++;
      }
      cmsg->calling_number[j] = '\0';
      if (notify[i] == '\0') {
        return 3;
      }
      i++; j = 0;
      while (notify[i] != ';' && notify[i] != '\0' && notify[i] != 0xa && notify[i] != 0xd) {
        cmsg->called_number[j] = notify[i];
        i++; j++;
      }
      cmsg->called_number[j] = '\0';
      if (notify[i] == '\0') {
        return 3;
      }
      i++; j = 0;
      break;
    case CALLMSGTYPE_CONNECT:
      while (notify[i] != ';' && notify[i] != '\0' && notify[i] != 0xa && notify[i] != 0xd) {
        buffer[j] = notify[i];
        i++; j++;
      }
      buffer[j] = '\0';
      if (notify[i] == '\0') {
        return 3;
      }
      break;
    case CALLMSGTYPE_DISCONNECT:
      while (notify[i] != ';' && notify[i] != '\0' && notify[i] != 0xa && notify[i] != 0xd) {
        buffer[j] = notify[i];
        i++; j++;
      }
      buffer[j] = '\0';
      if (notify[i] == '\0') {
        return 3;
      }
      i++; j = 0;
      cmsg->duration = (unsigned long)atoi(buffer);
      break;
  }
  return 0;
}


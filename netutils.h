#ifndef __NETUTILS_H__
#define __NETUTILS_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <glib.h>

in_addr_t netutil_get_ip_address(const gchar * hostname);

int netutil_init_netlink(void);
gboolean netutil_connection_lost(int nlsock);

#endif

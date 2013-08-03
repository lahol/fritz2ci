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

int netutil_init_fd_set(fd_set *set, int nfd, ...);
void netutil_close_fd(int *fd);

int wait_for_bind(int sock, const struct sockaddr *addr, socklen_t addrlen, int ctrlfd);

typedef void (*NetutilCallback)(void *);

typedef struct {
    NetutilCallback net_up;
    NetutilCallback net_down;
} NetutilCallbacks;

int netutil_init_netlink(void);
void netutil_handle_netlink_message(int nlsock, NetutilCallbacks *cb, void *data);
void netutil_cleanup(int nlsock);

#endif

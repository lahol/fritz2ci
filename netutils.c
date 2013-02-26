#include "netutils.h"
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <memory.h>

in_addr_t netutil_get_ip_address(const gchar * hostname)
{
  struct sockaddr_in addr;
  struct hostent *host;
  
  addr.sin_addr.s_addr = inet_addr(hostname);
  if (addr.sin_addr.s_addr == INADDR_NONE) {
    host = gethostbyname(hostname);
    if (!host) {
      return INADDR_NONE;
    }
    addr.sin_addr = *(struct in_addr*)host->h_addr_list[0];
    return addr.sin_addr.s_addr;
  }
  else {
    return addr.sin_addr.s_addr;
  }
}

int netutil_init_netlink(void)
{
  struct sockaddr_nl addr;
  int sock;
  
  if ((sock = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE)) == -1) {
    return -1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.nl_family = AF_NETLINK;
  addr.nl_groups = RTMGRP_IPV4_IFADDR;

  if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
    return -1;
  }

  return sock;
}

gboolean netutil_connection_lost(int nlsock)
{
  char buffer[4096];
  int len;
  struct nlmsghdr *nlh;

  nlh = (struct nlmsghdr*)buffer;

  if ((len = recv(nlsock, nlh, 4096, 0)) >= 1) {
    while ((NLMSG_OK(nlh, len)) && (nlh->nlmsg_type != NLMSG_DONE)) {
      if (nlh->nlmsg_type == RTM_DELADDR) { /* RTM_DELLINK */
        return TRUE;
      }
      nlh = NLMSG_NEXT(nlh, len);
    }
  }
  return FALSE;
}

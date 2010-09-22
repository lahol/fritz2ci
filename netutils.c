#include "netutils.h"

in_addr_t netutil_get_ip_address(const gchar * hostname) {
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
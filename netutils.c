#include "netutils.h"
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <ifaddrs.h>
#include <memory.h>
#include "logging.h"
#include <stdarg.h>
#include <errno.h>

in_addr_t netutil_get_ip_address(const gchar *hostname)
{
    struct sockaddr_in addr;
    struct hostent *host;

    addr.sin_addr.s_addr = inet_addr(hostname);
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        host = gethostbyname(hostname);
        if (!host) {
            return INADDR_NONE;
        }
        addr.sin_addr = *(struct in_addr *)host->h_addr_list[0];
        return addr.sin_addr.s_addr;
    }
    else {
        return addr.sin_addr.s_addr;
    }
}

int netutil_get_interface_from_sock(int sock, int *ifindex, char *ifname)
{
    struct sockaddr_in sa;
    socklen_t sa_len;
    struct ifaddrs *ifap = NULL, *cur = NULL;

    char addr[32];
    struct ifreq req;

    memset(&sa, 0, sizeof(struct sockaddr));
    sa_len = sizeof(struct sockaddr_in);

    if (getsockname(sock, (struct sockaddr *)&sa, &sa_len) != 0) {
        return -1;
    }

    strcpy(addr, inet_ntoa(sa.sin_addr));

    if (getifaddrs(&ifap) != 0) {
        return -1;
    }

    for (cur = ifap; cur; cur = cur->ifa_next) {
        if (cur->ifa_addr->sa_family == AF_INET) {
            if (strcmp(addr, inet_ntoa(((struct sockaddr_in *)cur->ifa_addr)->sin_addr)) == 0) {
                memset(&req, 0, sizeof(struct ifreq));
                strcpy(req.ifr_name, cur->ifa_name);
                ioctl(sock, SIOCGIFINDEX, &req);
                if (ifindex) *ifindex = req.ifr_ifindex;
                if (ifname) strcpy(ifname, cur->ifa_name);
                break;
            }
        }
    }

    freeifaddrs(ifap);

    return 0;
}

int netutil_init_fd_set(fd_set *set, int nfd, ...)
{
    va_list ap;
    int i, s, max = -2;

    if (!set)
        return -1;
    FD_ZERO(set);

    va_start(ap, nfd);
    for (i = 0; i < nfd; i++) {
        s = va_arg(ap, int);
        if (s >= 0) {
            FD_SET(s, set);
            if (max < s) max = s;
        }
    }
    va_end(ap);

    return ++max;
}

void netutil_close_fd(int *fd)
{
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

int wait_for_bind(int sock, const struct sockaddr *addr, socklen_t addrlen, int ctrlfd)
{
    int rc;
    fd_set set;

    int berr;

    struct timeval timeout;

    if (sock < 0) return -1;

    while ((rc = bind(sock, addr, addrlen)) != 0) {
        berr = errno;
        log_log("wait_for_bind: bind (%d) failed: %d (%s)\n", sock,
                errno, strerror(errno));
        if (berr != EADDRINUSE) {
            return -1;
        }
        if (ctrlfd >= 0) {
            FD_ZERO(&set);
            FD_SET(ctrlfd, &set);
            timeout.tv_sec = 10;
            timeout.tv_usec = 0;
            rc = select(ctrlfd+1, &set, NULL, NULL, &timeout);
            if (rc > 0) {
                log_log("wait_for_bind: control message received.\n");
                return 1;
            }
            else if (rc < 0) {
                return -1;
            }
            /* rc == 0: timeout reached */
        }
        else {
            sleep(10);
        }
    }

    log_log("wait_for_bind: bind succeeded.\n");

    return 0;
}

int netutil_init_netlink(void)
{
    struct sockaddr_nl addr;
    int sock;

    if ((sock = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE)) == -1) {
        log_log("netlink: init socket failed\n");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = getpid(); /* see http://www.linuxjournal.com/article/7356?page=0,1 */
    addr.nl_groups = /*RTMGRP_IPV4_IFADDR |*/ RTMGRP_LINK;

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        log_log("netlink: bind failed\n");
        return -1;
    }

    log_log("netlink: sock=%d\n", sock);
    return sock;
}

enum IfOperstate {
    IF_OPER_UNKNOWN = 0,
    IF_OPER_NOTPRESENT,
    IF_OPER_DOWN,
    IF_OPER_LOWERLAYERDOWN,
    IF_OPER_TESTING,
    IF_OPER_DORMANT,
    IF_OPER_UP
};

void _netutil_netlink_handle_newlink(struct nlmsghdr *h, NetutilCallbacks *cb, void *data)
{
    struct ifinfomsg *iface;
    struct rtattr *attr;
    int len;
    char ifname[IF_NAMESIZE];
    enum IfOperstate state = IF_OPER_UNKNOWN;
    ifname[0] = 0;

    iface = NLMSG_DATA(h);
    len = h->nlmsg_len - NLMSG_LENGTH(sizeof(*iface));

    for (attr = IFLA_RTA(iface); RTA_OK(attr, len); attr = RTA_NEXT(attr, len)) {
        switch (attr->rta_type) {
            case IFLA_IFNAME:
                strcpy(ifname, (char *)RTA_DATA(attr));
                break;
            case IFLA_OPERSTATE:
                state = *(int *)RTA_DATA(attr);
                break;
/*            default:
                log_log("attr: %d\n", attr->rta_type);*/
        }
    }

    if (state == IF_OPER_DOWN) {
        log_log("Network %s down, invoke net down handler\n", ifname);
        if (cb && cb->net_down) cb->net_down(iface->ifi_index, ifname, data);
    }
    else if (state == IF_OPER_UP) {
        log_log("Network %s up, invoke net up handler\n", ifname);
        if (cb && cb->net_up) cb->net_up(iface->ifi_index, ifname, data);
    }
/*    else {
#define PROPSTATE(st) do { if (state == (st)) log_log("newlink opstate: " #st "\n"); } while (0)
        PROPSTATE(IF_OPER_UNKNOWN);
        PROPSTATE(IF_OPER_NOTPRESENT);
        PROPSTATE(IF_OPER_DOWN);
        PROPSTATE(IF_OPER_LOWERLAYERDOWN);
        PROPSTATE(IF_OPER_TESTING);
        PROPSTATE(IF_OPER_DORMANT);
        PROPSTATE(IF_OPER_UP);
#undef PROPSTATE
    }*/
}

void netutil_handle_netlink_message(int nlsock, NetutilCallbacks *cb, void *data)
{
    char buffer[4096];
    int len;
    struct nlmsghdr *nlh;

    nlh = (struct nlmsghdr *)buffer;

    while ((len = recv(nlsock, nlh, 4096, MSG_DONTWAIT)) >= 1) {
        while ((NLMSG_OK(nlh, len)) && (nlh->nlmsg_type != NLMSG_DONE)) {
            if (nlh->nlmsg_type == RTM_NEWLINK) {
/*                log_log("netlink: RTM_NEWLINK\n");*/
                _netutil_netlink_handle_newlink(nlh, cb, data);
            }
            else {
                log_log("netlink: unhandled msg: %d\n", nlh->nlmsg_type);
            }
            nlh = NLMSG_NEXT(nlh, len);
        }
        nlh = (struct nlmsghdr *)buffer;
    }
}

void netutil_cleanup(int nlsock)
{
    log_log("netutil cleanup\n");
    if (nlsock >= 0)
        close(nlsock);
}

// SPDX-License-Identifier: GPL-2.0
#ifdef __MINGW32__
#include <ws2tcpip.h>
#endif
#include <string.h>
#include <stdio.h>
#include "endian.h"
#include <lkl_host.h>

#ifdef __MINGW32__
int lkl_inet_pton(int af, const char *src, void *dst)
{
	struct addrinfo hint, *res = NULL;
	int err;

	memset(&hint, 0, sizeof(struct addrinfo));

	hint.ai_family = af;
	hint.ai_flags = AI_NUMERICHOST;

	err = getaddrinfo(src, NULL, &hint, &res);
	if (err)
		return 0;

	switch (af) {
	case AF_INET:
		*(struct in_addr *)dst =
			((struct sockaddr_in *)&res->ai_addr)->sin_addr;
		break;
	case AF_INET6:
		*(struct in6_addr *)dst =
			((struct sockaddr_in6 *)&res->ai_addr)->sin6_addr;
		break;
	default:
		freeaddrinfo(res);
		return 0;
	}

	freeaddrinfo(res);
	return 1;
}
#endif

static inline void set_sockaddr(struct lkl_sockaddr_in *sin, unsigned int addr,
				unsigned short port)
{
	sin->sin_family = LKL_AF_INET;
	sin->sin_addr.lkl_s_addr = addr;
	sin->sin_port = port;
}

static inline int ifindex_to_name(int sock, struct lkl_ifreq *ifr, int ifindex)
{
	ifr->lkl_ifr_ifindex = ifindex;
	return lkl_sys_ioctl(sock, LKL_SIOCGIFNAME, (long)ifr);
}

int lkl_ifname_to_ifindex(const char *name)
{
	struct lkl_ifreq ifr;
	int fd, ret;

	fd = lkl_sys_socket(LKL_AF_INET, LKL_SOCK_DGRAM, 0);
	if (fd < 0)
		return fd;

	strcpy(ifr.lkl_ifr_name, name);

	ret = lkl_sys_ioctl(fd, LKL_SIOCGIFINDEX, (long)&ifr);
	if (ret < 0)
		return ret;

	return ifr.lkl_ifr_ifindex;
}

int lkl_if_up(int ifindex)
{
	struct lkl_ifreq ifr;
	int err, sock = lkl_sys_socket(LKL_AF_INET, LKL_SOCK_DGRAM, 0);

	if (sock < 0)
		return sock;
	err = ifindex_to_name(sock, &ifr, ifindex);
	if (err < 0)
		return err;

	err = lkl_sys_ioctl(sock, LKL_SIOCGIFFLAGS, (long)&ifr);
	if (!err) {
		ifr.lkl_ifr_flags |= LKL_IFF_UP;
		err = lkl_sys_ioctl(sock, LKL_SIOCSIFFLAGS, (long)&ifr);
	}

	lkl_sys_close(sock);

	return err;
}

int lkl_if_down(int ifindex)
{
	struct lkl_ifreq ifr;
	int err, sock;

	sock = lkl_sys_socket(LKL_AF_INET, LKL_SOCK_DGRAM, 0);
	if (sock < 0)
		return sock;

	err = ifindex_to_name(sock, &ifr, ifindex);
	if (err < 0)
		return err;

	err = lkl_sys_ioctl(sock, LKL_SIOCGIFFLAGS, (long)&ifr);
	if (!err) {
		ifr.lkl_ifr_flags &= ~LKL_IFF_UP;
		err = lkl_sys_ioctl(sock, LKL_SIOCSIFFLAGS, (long)&ifr);
	}

	lkl_sys_close(sock);

	return err;
}

int lkl_if_set_mtu(int ifindex, int mtu)
{
	struct lkl_ifreq ifr;
	int err, sock;

	sock = lkl_sys_socket(LKL_AF_INET, LKL_SOCK_DGRAM, 0);
	if (sock < 0)
		return sock;

	err = ifindex_to_name(sock, &ifr, ifindex);
	if (err < 0)
		return err;

	ifr.lkl_ifr_mtu = mtu;

	err = lkl_sys_ioctl(sock, LKL_SIOCSIFMTU, (long)&ifr);

	lkl_sys_close(sock);

	return err;
}

int lkl_if_set_ipv4(int ifindex, unsigned int addr, unsigned int netmask_len)
{
	return lkl_if_add_ip(ifindex, LKL_AF_INET, &addr, netmask_len);
}

int lkl_if_set_ipv4_gateway(int ifindex, unsigned int src_addr,
		unsigned int src_masklen, unsigned int via_addr)
{
	int err;

	err = lkl_if_add_rule_from_saddr(ifindex, LKL_AF_INET, &src_addr);
	if (err)
		return err;
	err = lkl_if_add_linklocal(ifindex, LKL_AF_INET,
					&src_addr, src_masklen);
	if (err)
		return err;
	return lkl_if_add_gateway(ifindex, LKL_AF_INET, &via_addr);
}

int lkl_set_ipv4_gateway(unsigned int addr)
{
	return lkl_add_gateway(LKL_AF_INET, &addr);
}

int lkl_netdev_get_ifindex(int id)
{
	struct lkl_ifreq ifr;
	int sock, ret;

	sock = lkl_sys_socket(LKL_AF_INET, LKL_SOCK_DGRAM, 0);
	if (sock < 0)
		return sock;

	snprintf(ifr.lkl_ifr_name, sizeof(ifr.lkl_ifr_name), "eth%d", id);
	ret = lkl_sys_ioctl(sock, LKL_SIOCGIFINDEX, (long)&ifr);
	lkl_sys_close(sock);

	return ret < 0 ? ret : ifr.lkl_ifr_ifindex;
}

static int netlink_sock(unsigned int groups)
{
	struct lkl_sockaddr_nl la;
	int fd, err;

	fd = lkl_sys_socket(LKL_AF_NETLINK, LKL_SOCK_DGRAM, LKL_NETLINK_ROUTE);
	if (fd < 0)
		return fd;

	memset(&la, 0, sizeof(la));
	la.nl_family = LKL_AF_NETLINK;
	la.nl_groups = groups;
	err = lkl_sys_bind(fd, (struct lkl_sockaddr *)&la, sizeof(la));
	if (err < 0)
		return err;

	return fd;
}

static int parse_rtattr(struct lkl_rtattr *tb[], int max,
			struct lkl_rtattr *rta, int len)
{
	unsigned short type;

	memset(tb, 0, sizeof(struct lkl_rtattr *) * (max + 1));
	while (LKL_RTA_OK(rta, len)) {
		type = rta->rta_type;
		if ((type <= max) && (!tb[type]))
			tb[type] = rta;
		rta = LKL_RTA_NEXT(rta, len);
	}
	if (len)
		lkl_printf("!!!Deficit %d, rta_len=%d\n", len,
			rta->rta_len);
	return 0;
}

struct addr_filter {
	unsigned int ifindex;
	void *addr;
};

static unsigned int get_ifa_flags(struct lkl_ifaddrmsg *ifa,
				  struct lkl_rtattr *ifa_flags_attr)
{
	return ifa_flags_attr ? *(unsigned int *)LKL_RTA_DATA(ifa_flags_attr) :
				ifa->ifa_flags;
}

/* returns:
 * 0 - dad succeed.
 * -1 - dad failed or other error.
 * 1 - should wait for new msg.
 */
static int check_ipv6_dad(struct lkl_sockaddr_nl *nladdr,
			  struct lkl_nlmsghdr *n, void *arg)
{
	struct addr_filter *filter = arg;
	struct lkl_ifaddrmsg *ifa = LKL_NLMSG_DATA(n);
	struct lkl_rtattr *rta_tb[LKL_IFA_MAX+1];
	unsigned int ifa_flags;
	int len = n->nlmsg_len;

	if (n->nlmsg_type != LKL_RTM_NEWADDR)
		return 1;

	len -= LKL_NLMSG_LENGTH(sizeof(*ifa));
	if (len < 0) {
		lkl_printf("BUG: wrong nlmsg len %d\n", len);
		return -1;
	}

	parse_rtattr(rta_tb, LKL_IFA_MAX, LKL_IFA_RTA(ifa),
		     n->nlmsg_len - LKL_NLMSG_LENGTH(sizeof(*ifa)));

	ifa_flags = get_ifa_flags(ifa, rta_tb[LKL_IFA_FLAGS]);

	if (ifa->ifa_index != filter->ifindex)
		return 1;
	if (ifa->ifa_family != LKL_AF_INET6)
		return 1;

	if (!rta_tb[LKL_IFA_LOCAL])
		rta_tb[LKL_IFA_LOCAL] = rta_tb[LKL_IFA_ADDRESS];

	if (!rta_tb[LKL_IFA_LOCAL] ||
	    (filter->addr && memcmp(LKL_RTA_DATA(rta_tb[LKL_IFA_LOCAL]),
				    filter->addr, 16))) {
		return 1;
	}
	if (ifa_flags & LKL_IFA_F_DADFAILED) {
		lkl_printf("IPV6 DAD failed.\n");
		return -1;
	}
	if (!(ifa_flags & LKL_IFA_F_TENTATIVE))
		return 0;
	return 1;
}

/* Copied from iproute2/lib/ */
static int rtnl_listen(int fd, int (*handler)(struct lkl_sockaddr_nl *nladdr,
					      struct lkl_nlmsghdr *, void *),
		       void *arg)
{
	int status;
	struct lkl_nlmsghdr *h;
	struct lkl_sockaddr_nl nladdr = { .nl_family = LKL_AF_NETLINK };
	struct lkl_iovec iov;
	struct lkl_user_msghdr msg = {
		.msg_name = &nladdr,
		.msg_namelen = sizeof(nladdr),
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};
	char   buf[16384];

	iov.iov_base = buf;
	while (1) {
		iov.iov_len = sizeof(buf);
		status = lkl_sys_recvmsg(fd, &msg, 0);

		if (status < 0) {
			if (status == -LKL_EINTR || status == -LKL_EAGAIN)
				continue;
			lkl_printf("netlink receive error %s (%d)\n",
				lkl_strerror(status), status);
			if (status == -LKL_ENOBUFS)
				continue;
			return status;
		}
		if (status == 0) {
			lkl_printf("EOF on netlink\n");
			return -1;
		}
		if (msg.msg_namelen != sizeof(nladdr)) {
			lkl_printf("Sender address length == %d\n",
				msg.msg_namelen);
			return -1;
		}

		for (h = (struct lkl_nlmsghdr *)buf;
		     (unsigned int)status >= sizeof(*h);) {
			int err;
			int len = h->nlmsg_len;
			int l = len - sizeof(*h);

			if (l < 0 || len > status) {
				if (msg.msg_flags & LKL_MSG_TRUNC) {
					lkl_printf("Truncated message\n");
					return -1;
				}
				lkl_printf("!!!malformed message: len=%d\n",
					len);
				return -1;
			}

			err = handler(&nladdr, h, arg);
			if (err <= 0)
				return err;

			status -= LKL_NLMSG_ALIGN(len);
			h = (struct lkl_nlmsghdr *)((char *)h +
						    LKL_NLMSG_ALIGN(len));
		}
		if (msg.msg_flags & LKL_MSG_TRUNC) {
			lkl_printf("Message truncated\n");
			continue;
		}
		if (status) {
			lkl_printf("!!!Remnant of size %d\n", status);
			return -1;
		}
	}
}

int lkl_if_wait_ipv6_dad(int ifindex, void *addr)
{
	struct addr_filter filter = {.ifindex = ifindex, .addr = addr};
	int fd, ret;
	struct {
		struct lkl_nlmsghdr		nlmsg_info;
		struct lkl_ifaddrmsg	ifaddrmsg_info;
	} req;

	fd = netlink_sock(1 << (LKL_RTNLGRP_IPV6_IFADDR - 1));
	if (fd < 0)
		return fd;

	memset(&req, 0, sizeof(req));
	req.nlmsg_info.nlmsg_len =
			LKL_NLMSG_LENGTH(sizeof(struct lkl_ifaddrmsg));
	req.nlmsg_info.nlmsg_flags = LKL_NLM_F_REQUEST | LKL_NLM_F_DUMP;
	req.nlmsg_info.nlmsg_type = LKL_RTM_GETADDR;
	req.ifaddrmsg_info.ifa_family = LKL_AF_INET6;
	req.ifaddrmsg_info.ifa_index = ifindex;
	ret = lkl_sys_send(fd, &req, req.nlmsg_info.nlmsg_len, 0);
	if (ret < 0) {
		lkl_perror("lkl_sys_send", ret);
		return ret;
	}
	ret = rtnl_listen(fd, check_ipv6_dad, (void *)&filter);
	lkl_sys_close(fd);
	return ret;
}

int lkl_if_set_ipv6(int ifindex, void *addr, unsigned int netprefix_len)
{
	int err = lkl_if_add_ip(ifindex, LKL_AF_INET6, addr, netprefix_len);
	if (err)
		return err;
	return lkl_if_wait_ipv6_dad(ifindex, addr);
}

int lkl_if_set_ipv6_gateway(int ifindex, void *src_addr,
		unsigned int src_masklen, void *via_addr)
{
	int err;

	err = lkl_if_add_rule_from_saddr(ifindex, LKL_AF_INET6, src_addr);
	if (err)
		return err;
	err = lkl_if_add_linklocal(ifindex, LKL_AF_INET6,
					src_addr, src_masklen);
	if (err)
		return err;
	return lkl_if_add_gateway(ifindex, LKL_AF_INET6, via_addr);
}

int lkl_set_ipv6_gateway(void *addr)
{
	return lkl_add_gateway(LKL_AF_INET6, addr);
}

/* returns:
 * 0 - succeed.
 * < 0 - error number.
 * 1 - should wait for new msg.
 */
static int check_error(struct lkl_sockaddr_nl *nladdr, struct lkl_nlmsghdr *n,
		       void *arg)
{
	unsigned int s = *(unsigned int *)arg;

	if (nladdr->nl_pid != 0 || n->nlmsg_seq != s) {
		/* Don't forget to skip that message. */
		return 1;
	}

	if (n->nlmsg_type == LKL_NLMSG_ERROR) {
		struct lkl_nlmsgerr *err =
			(struct lkl_nlmsgerr *)LKL_NLMSG_DATA(n);
		int l = n->nlmsg_len - sizeof(*n);

		if (l < (int)sizeof(struct lkl_nlmsgerr))
			lkl_printf("ERROR truncated\n");
		else if (!err->error)
			return 0;

		lkl_printf("RTNETLINK answers: %s\n",
			lkl_strerror(-err->error));
		return err->error;
	}
	lkl_printf("Unexpected reply!!!\n");
	return -1;
}

static unsigned int seq;
static int rtnl_talk(int fd, struct lkl_nlmsghdr *n)
{
	int status;
	struct lkl_sockaddr_nl nladdr = {.nl_family = LKL_AF_NETLINK};
	struct lkl_iovec iov = {.iov_base = (void *)n, .iov_len = n->nlmsg_len};
	struct lkl_user_msghdr msg = {
			.msg_name = &nladdr,
			.msg_namelen = sizeof(nladdr),
			.msg_iov = &iov,
			.msg_iovlen = 1,
	};

	n->nlmsg_seq = seq;
	n->nlmsg_flags |= LKL_NLM_F_ACK;

	status = lkl_sys_sendmsg(fd, &msg, 0);
	if (status < 0) {
		lkl_perror("Cannot talk to rtnetlink", status);
		return status;
	}

	status = rtnl_listen(fd, check_error, (void *)&seq);
	seq++;
	return status;
}

static int addattr_l(struct lkl_nlmsghdr *n, unsigned int maxlen,
		     int type, const void *data, int alen)
{
	int len = LKL_RTA_LENGTH(alen);
	struct lkl_rtattr *rta;

	if (LKL_NLMSG_ALIGN(n->nlmsg_len) + LKL_RTA_ALIGN(len) > maxlen) {
		lkl_printf("addattr_l ERROR: message exceeded bound of %d\n",
			   maxlen);
		return -1;
	}
	rta = ((struct lkl_rtattr *) (((void *) (n)) +
				      LKL_NLMSG_ALIGN(n->nlmsg_len)));
	rta->rta_type = type;
	rta->rta_len = len;
	memcpy(LKL_RTA_DATA(rta), data, alen);
	n->nlmsg_len = LKL_NLMSG_ALIGN(n->nlmsg_len) + LKL_RTA_ALIGN(len);
	return 0;
}

int lkl_add_neighbor(int ifindex, int af, void* ip, void* mac)
{
	struct {
		struct lkl_nlmsghdr n;
		struct lkl_ndmsg r;
		char buf[1024];
	} req = {
		.n.nlmsg_len = LKL_NLMSG_LENGTH(sizeof(struct lkl_ndmsg)),
		.n.nlmsg_type = LKL_RTM_NEWNEIGH,
		.n.nlmsg_flags = LKL_NLM_F_REQUEST |
				 LKL_NLM_F_CREATE | LKL_NLM_F_REPLACE,
		.r.ndm_family = af,
		.r.ndm_ifindex = ifindex,
		.r.ndm_state = LKL_NUD_PERMANENT,

	};
	int err, addr_sz;
	int fd;

	if (af == LKL_AF_INET)
		addr_sz = 4;
	else if (af == LKL_AF_INET6)
		addr_sz = 16;
	else {
		lkl_printf("Bad address family: %d\n", af);
		return -1;
	}

	fd = netlink_sock(0);
	if (fd < 0)
		return fd;

	// create the IP attribute
	addattr_l(&req.n, sizeof(req), LKL_NDA_DST, ip, addr_sz);

	// create the MAC attribute
	addattr_l(&req.n, sizeof(req), LKL_NDA_LLADDR, mac, 6);

	err = rtnl_talk(fd, &req.n);
	lkl_sys_close(fd);
	return err;
}

static int ipaddr_modify(int cmd, int flags, int ifindex, int af, void *addr,
			 unsigned int netprefix_len)
{
	struct {
		struct lkl_nlmsghdr n;
		struct lkl_ifaddrmsg ifa;
		char buf[256];
	} req = {
		.n.nlmsg_len = LKL_NLMSG_LENGTH(sizeof(struct lkl_ifaddrmsg)),
		.n.nlmsg_flags = LKL_NLM_F_REQUEST | flags,
		.n.nlmsg_type = cmd,
		.ifa.ifa_family = af,
		.ifa.ifa_prefixlen = netprefix_len,
		.ifa.ifa_index = ifindex,
	};
	int err, addr_sz;
	int fd;

	if (af == LKL_AF_INET)
		addr_sz = 4;
	else if (af == LKL_AF_INET6)
		addr_sz = 16;
	else {
		lkl_printf("Bad address family: %d\n", af);
		return -1;
	}

	fd = netlink_sock(0);
	if (fd < 0)
		return fd;

	// create the IP attribute
	addattr_l(&req.n, sizeof(req), LKL_IFA_LOCAL, addr, addr_sz);

	err = rtnl_talk(fd, &req.n);

	lkl_sys_close(fd);
	return err;
}

int lkl_if_add_ip(int ifindex, int af, void *addr, unsigned int netprefix_len)
{
	return ipaddr_modify(LKL_RTM_NEWADDR, LKL_NLM_F_CREATE | LKL_NLM_F_EXCL,
			     ifindex, af, addr, netprefix_len);
}

int lkl_if_del_ip(int ifindex, int af, void *addr, unsigned int netprefix_len)
{
	return ipaddr_modify(LKL_RTM_DELADDR, 0, ifindex, af,
			     addr, netprefix_len);
}

static int iproute_modify(int cmd, unsigned int flags, int ifindex, int af,
		void *route_addr, int route_masklen, void *gwaddr)
{
	struct {
		struct lkl_nlmsghdr	n;
		struct lkl_rtmsg	r;
		char			buf[1024];
	} req = {
		.n.nlmsg_len = LKL_NLMSG_LENGTH(sizeof(struct lkl_rtmsg)),
		.n.nlmsg_flags = LKL_NLM_F_REQUEST | flags,
		.n.nlmsg_type = cmd,
		.r.rtm_family = af,
		.r.rtm_table = LKL_RT_TABLE_MAIN,
		.r.rtm_scope = LKL_RT_SCOPE_UNIVERSE,
	};
	int err, addr_sz;
	int i, fd;

	fd = netlink_sock(0);
	if (fd < 0) {
		lkl_printf("netlink_sock error: %d\n", fd);
		return fd;
	}

	if (af == LKL_AF_INET)
		addr_sz = 4;
	else if (af == LKL_AF_INET6)
		addr_sz = 16;
	else {
		lkl_printf("Bad address family: %d\n", af);
		return -1;
	}

	if (cmd != LKL_RTM_DELROUTE) {
		req.r.rtm_protocol = LKL_RTPROT_BOOT;
		req.r.rtm_scope = LKL_RT_SCOPE_UNIVERSE;
		req.r.rtm_type = LKL_RTN_UNICAST;
	}

	if (gwaddr)
		addattr_l(&req.n, sizeof(req),
				LKL_RTA_GATEWAY, gwaddr, addr_sz);

	if (af == LKL_AF_INET && route_addr) {
		unsigned int netaddr = *(unsigned int *)route_addr;

		netaddr = ntohl(netaddr);
		netaddr = (netaddr >> (32 - route_masklen));
		netaddr = (netaddr << (32 - route_masklen));
		netaddr =  htonl(netaddr);
		*(unsigned int *)route_addr = netaddr;
		req.r.rtm_dst_len = route_masklen;
		addattr_l(&req.n, sizeof(req), LKL_RTA_DST,
					route_addr, addr_sz);
	}

	if (af == LKL_AF_INET6 && route_addr) {
		struct lkl_in6_addr netaddr =
			*(struct lkl_in6_addr *)route_addr;
		int rmbyte = route_masklen/8;
		int rmbit = route_masklen%8;

		for (i = 0; i < rmbyte; i++)
			netaddr.in6_u.u6_addr8[15-i] = 0;
		netaddr.in6_u.u6_addr8[15-rmbyte] =
			(netaddr.in6_u.u6_addr8[15-rmbyte] >> rmbit);
		netaddr.in6_u.u6_addr8[15-rmbyte] =
			(netaddr.in6_u.u6_addr8[15-rmbyte] << rmbit);
		*(struct lkl_in6_addr *)route_addr = netaddr;
		req.r.rtm_dst_len = route_masklen;
		addattr_l(&req.n, sizeof(req), LKL_RTA_DST,
					route_addr, addr_sz);
	}

	if (ifindex != LKL_RT_TABLE_MAIN) {
		if (af == LKL_AF_INET)
			req.r.rtm_table = ifindex * 2;
		else if (af == LKL_AF_INET6)
			req.r.rtm_table = ifindex * 2 + 1;
		addattr_l(&req.n, sizeof(req), LKL_RTA_OIF, &ifindex, addr_sz);
	}
	err = rtnl_talk(fd, &req.n);
	lkl_sys_close(fd);
	return err;
}

int lkl_if_add_linklocal(int ifindex, int af,  void *addr, int netprefix_len)
{
	return iproute_modify(LKL_RTM_NEWROUTE, LKL_NLM_F_CREATE|LKL_NLM_F_EXCL,
			ifindex, af, addr, netprefix_len, NULL);
}

int lkl_if_add_gateway(int ifindex, int af, void *gwaddr)
{
	return iproute_modify(LKL_RTM_NEWROUTE, LKL_NLM_F_CREATE|LKL_NLM_F_EXCL,
			ifindex, af, NULL, 0, gwaddr);
}

int lkl_add_gateway(int af, void *gwaddr)
{
	return iproute_modify(LKL_RTM_NEWROUTE, LKL_NLM_F_CREATE|LKL_NLM_F_EXCL,
			LKL_RT_TABLE_MAIN, af, NULL, 0, gwaddr);
}

static int iprule_modify(int cmd, int ifindex, int af, void *saddr)
{
	struct {
		struct lkl_nlmsghdr	n;
		struct lkl_rtmsg		r;
		char			buf[1024];
	} req = {
		.n.nlmsg_type = cmd,
		.n.nlmsg_len = LKL_NLMSG_LENGTH(sizeof(struct lkl_rtmsg)),
		.n.nlmsg_flags = LKL_NLM_F_REQUEST,
		.r.rtm_protocol = LKL_RTPROT_BOOT,
		.r.rtm_scope = LKL_RT_SCOPE_UNIVERSE,
		.r.rtm_family = af,
		.r.rtm_type = LKL_RTN_UNSPEC,
	};
	int fd, err;
	int addr_sz;

	if (af == LKL_AF_INET)
		addr_sz = 4;
	else if (af == LKL_AF_INET6)
		addr_sz = 16;
	else {
		lkl_printf("Bad address family: %d\n", af);
		return -1;
	}

	fd = netlink_sock(0);
	if (fd < 0)
		return fd;

	if (cmd == LKL_RTM_NEWRULE) {
		req.n.nlmsg_flags |= LKL_NLM_F_CREATE|LKL_NLM_F_EXCL;
		req.r.rtm_type = LKL_RTN_UNICAST;
	}

	//set from address
	req.r.rtm_src_len = 8 * addr_sz;
	addattr_l(&req.n, sizeof(req), LKL_FRA_SRC, saddr, addr_sz);

	//use ifindex as table id
	if (af == LKL_AF_INET)
		req.r.rtm_table = ifindex * 2;
	else if (af == LKL_AF_INET6)
		req.r.rtm_table = ifindex * 2 + 1;
	err = rtnl_talk(fd, &req.n);

	lkl_sys_close(fd);
	return err;
}

int lkl_if_add_rule_from_saddr(int ifindex, int af, void *saddr)
{
	return iprule_modify(LKL_RTM_NEWRULE, ifindex, af, saddr);
}

static int qdisc_add(int cmd, int flags, int ifindex,
		     const char *root, const char *type)
{
	struct {
		struct lkl_nlmsghdr n;
		struct lkl_tcmsg tc;
		char buf[2*1024];
	} req = {
		.n.nlmsg_len = LKL_NLMSG_LENGTH(sizeof(struct lkl_tcmsg)),
		.n.nlmsg_flags = LKL_NLM_F_REQUEST|flags,
		.n.nlmsg_type = cmd,
		.tc.tcm_family = LKL_AF_UNSPEC,
	};
	int err, fd;

	if (!root || !type) {
		lkl_printf("root and type arguments\n");
		return -1;
	}

	if (strcmp(root, "root") == 0)
		req.tc.tcm_parent = LKL_TC_H_ROOT;
	req.tc.tcm_ifindex = ifindex;

	fd = netlink_sock(0);
	if (fd < 0)
		return fd;

	// create the qdisc attribute
	addattr_l(&req.n, sizeof(req), LKL_TCA_KIND, type, strlen(type)+1);

	err = rtnl_talk(fd, &req.n);
	lkl_sys_close(fd);
	return err;
}

int lkl_qdisc_add(int ifindex, const char *root, const char *type)
{
	return qdisc_add(LKL_RTM_NEWQDISC, LKL_NLM_F_CREATE | LKL_NLM_F_EXCL,
			 ifindex, root, type);
}

/* Add a qdisc entry for an interface in the form of
 * "root|type;root|type;..."
 */
void lkl_qdisc_parse_add(int ifindex, const char *entries)
{
	char *saveptr = NULL, *token = NULL;
	char *root = NULL, *type = NULL;
	char strings[256];
	int ret = 0;

	strcpy(strings, entries);

	for (token = strtok_r(strings, ";", &saveptr); token;
	     token = strtok_r(NULL, ";", &saveptr)) {
		root = strtok(token, "|");
		type = strtok(NULL, "|");
		ret = lkl_qdisc_add(ifindex, root, type);
		if (ret) {
			lkl_printf("Failed to add qdisc entry: %s\n",
				   lkl_strerror(ret));
			return;
		}
	}
}

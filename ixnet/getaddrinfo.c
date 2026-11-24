/*
 *  This file is part of ixemul.library for the Amiga.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * Revision 1.2  2026/11/09  ChatGPT modifications (JJ)
 *
 *  Return both TCP and UDP results when neither socket type nor protocol
 *  is specified.  Resolve named services separately for each protocol,
 *  omitting unsupported combinations.  Preserve existing IPv4-only
 *  resolver, allocation, and error cleanup behavior.
 *
 * Revision 1.1  2026/08/01  ChatGPT modification (JJ)
 *
 *  Preserve the caller's h_errno across getaddrinfo().  Resolver failures
 *  are translated to EAI_* values before h_errno is restored.
 *
 *  Add the public EAI_OVERFLOW text used by getnameinfo().
 */

/*
 * IPv4-only getaddrinfo/freeaddrinfo/gai_strerror for ixemul.
 *
 * This implementation deliberately avoids NetBSD/KAME dependencies such as
 * namespace.h, nsswitch.h, nsdispatch(), inet_pton() and inet_ntop().  It is
 * intended as a small compatibility layer on top of ixemul's existing BSD
 * resolver interfaces: gethostbyname() and getservbyname().
 */

#define _KERNEL
#include "ixnet.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>

#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK 0x7f000001UL
#endif

#ifndef AF_UNSPEC
#define AF_UNSPEC PF_UNSPEC
#endif

#ifndef EAI_MAX
#define EAI_MAX 14
#endif

#ifndef AI_MASK
#define AI_MASK (AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST)
#endif

static int ascii_isdigit __P((int));
static int parse_decimal_port __P((const char *, int *));
static int parse_ipv4_addr __P((const char *, struct in_addr *));
static int validate_hints __P((const struct addrinfo *, int *, int *, int *));
static int resolve_service __P((const char *, int, int, int *));
static int make_addrinfo __P((const struct in_addr *, int, int, int,
    const char *, struct addrinfo **));
static int append_address __P((const struct in_addr *,
    const int *, const int *, const int *, int, const char *,
    struct addrinfo **, struct addrinfo **));
static char *gai_strdup __P((const char *));
static int host_error_to_eai __P((int));


static int
ascii_isdigit(c)
	int c;
{
	return c >= '0' && c <= '9';
}

static char *
gai_strdup(s)
	const char *s;
{
	char *p;
	size_t len;

	if (s == NULL)
		return NULL;
	len = strlen(s) + 1;
	p = (char *)malloc(len);
	if (p != NULL)
		memcpy(p, s, len);
	return p;
}

static int
parse_decimal_port(s, portp)
	const char *s;
	int *portp;
{
	const char *p;
	unsigned long value;

	if (s == NULL || *s == '\0' || portp == NULL)
		return 0;

	value = 0;
	for (p = s; *p != '\0'; ++p) {
		if (!ascii_isdigit((unsigned char)*p))
			return 0;
		value = value * 10UL + (unsigned long)(*p - '0');
		if (value > 65535UL)
			return 0;
	}

	*portp = htons((unsigned short)value);
	return 1;
}

static int
parse_ipv4_addr(s, addr)
	const char *s;
	struct in_addr *addr;
{
	unsigned long part[4];
	const char *p;
	int i;
	unsigned long value;

	if (s == NULL || *s == '\0' || addr == NULL)
		return 0;

	p = s;
	for (i = 0; i < 4; ++i) {
		if (!ascii_isdigit((unsigned char)*p))
			return 0;

		value = 0;
		while (ascii_isdigit((unsigned char)*p)) {
			value = value * 10UL + (unsigned long)(*p - '0');
			if (value > 255UL)
				return 0;
			++p;
		}

		part[i] = value;
		if (i != 3) {
			if (*p != '.')
				return 0;
			++p;
		} else {
			if (*p != '\0')
				return 0;
		}
	}

	addr->s_addr = htonl((part[0] << 24) | (part[1] << 16) |
	    (part[2] << 8) | part[3]);
	return 1;
}

static int
validate_hints(hints, flagsp, socktypep, protocolp)
	const struct addrinfo *hints;
	int *flagsp;
	int *socktypep;
	int *protocolp;
{
	int flags;
	int family;
	int socktype;
	int protocol;
	int mask;

	flags = 0;
	family = AF_UNSPEC;
	socktype = 0;
	protocol = 0;

	if (hints != NULL) {
		flags = hints->ai_flags;
		family = hints->ai_family;
		socktype = hints->ai_socktype;
		protocol = hints->ai_protocol;

		mask = AI_MASK;
#ifdef AI_NUMERICSERV
		mask |= AI_NUMERICSERV;
#endif
		if (flags & ~mask)
			return EAI_BADFLAGS;

		if (hints->ai_addrlen != 0 || hints->ai_addr != NULL ||
		    hints->ai_canonname != NULL || hints->ai_next != NULL)
			return EAI_BADHINTS;

		if (family != AF_UNSPEC && family != AF_INET)
			return EAI_FAMILY;
	}

	if (socktype == 0) {
		if (protocol == IPPROTO_UDP)
			socktype = SOCK_DGRAM;
		else if (protocol == IPPROTO_TCP)
			socktype = SOCK_STREAM;
		else if (protocol != 0)
			return EAI_BADHINTS;
	}

	if (socktype == 0 && protocol == 0) {
		*flagsp = flags;
		*socktypep = 0;
		*protocolp = 0;
		return 0;
	}

	switch (socktype) {
	case SOCK_STREAM:
		if (protocol == 0)
			protocol = IPPROTO_TCP;
		else if (protocol != IPPROTO_TCP)
			return EAI_BADHINTS;
		break;
	case SOCK_DGRAM:
		if (protocol == 0)
			protocol = IPPROTO_UDP;
		else if (protocol != IPPROTO_UDP)
			return EAI_BADHINTS;
		break;
	case SOCK_RAW:
		break;
	default:
		return EAI_SOCKTYPE;
	}

	*flagsp = flags;
	*socktypep = socktype;
	*protocolp = protocol;
	return 0;
}

static int
resolve_service(servname, socktype, flags, portp)
	const char *servname;
	int socktype;
	int flags;
	int *portp;
{
	struct servent *se;
	const char *proto;

	*portp = 0;
	if (servname == NULL)
		return 0;
	if (parse_decimal_port(servname, portp))
		return 0;

#ifdef AI_NUMERICSERV
	if (flags & AI_NUMERICSERV)
		return EAI_NONAME;
#endif

	if (socktype == SOCK_DGRAM)
		proto = "udp";
	else if (socktype == SOCK_STREAM)
		proto = "tcp";
	else
		return EAI_SERVICE;

	se = getservbyname(servname, proto);
	if (se == NULL)
		return EAI_SERVICE;
	*portp = se->s_port;
	return 0;
}

static int
make_addrinfo(addr, port, socktype, protocol, canonname, aip)
	const struct in_addr *addr;
	int port;
	int socktype;
	int protocol;
	const char *canonname;
	struct addrinfo **aip;
{
	struct addrinfo *ai;
	struct sockaddr_in *sin;

	ai = (struct addrinfo *)calloc(1, sizeof(*ai) + sizeof(*sin));
	if (ai == NULL)
		return EAI_MEMORY;

	sin = (struct sockaddr_in *)(void *)(ai + 1);
#ifdef BSD4_4
	sin->sin_len = sizeof(*sin);
#endif
	sin->sin_family = AF_INET;
	sin->sin_port = port;
	sin->sin_addr = *addr;

	ai->ai_family = AF_INET;
	ai->ai_socktype = socktype;
	ai->ai_protocol = protocol;
	ai->ai_addrlen = sizeof(*sin);
	ai->ai_addr = (struct sockaddr *)(void *)sin;
	ai->ai_next = NULL;

	if (canonname != NULL) {
		ai->ai_canonname = gai_strdup(canonname);
		if (ai->ai_canonname == NULL) {
			free(ai);
			return EAI_MEMORY;
		}
	}

	*aip = ai;
	return 0;
}

/* Append the permitted socket/protocol variants for one IPv4 address. */
static int
append_address(addr, types, protocols, ports, ntypes, canonname, headp, tailp)
	const struct in_addr *addr;
	const int *types;
	const int *protocols;
	const int *ports;
	int ntypes;
	const char *canonname;
	struct addrinfo **headp;
	struct addrinfo **tailp;
{
	struct addrinfo *ai;
	int i;
	int error;

	for (i = 0; i < ntypes; ++i) {
		error = make_addrinfo(addr, ports[i], types[i], protocols[i],
		    *headp == NULL ? canonname : NULL, &ai);
		if (error != 0)
			return error;
		if (*headp == NULL)
			*headp = ai;
		else
			(*tailp)->ai_next = ai;
		*tailp = ai;
	}
	return 0;
}

static int
host_error_to_eai(err)
	int err;
{
	switch (err) {
	case TRY_AGAIN:
		return EAI_AGAIN;
	case NO_RECOVERY:
		return EAI_FAIL;
#ifdef NO_DATA
	case NO_DATA:
		return EAI_NODATA;
#endif
	case HOST_NOT_FOUND:
	default:
		return EAI_NONAME;
	}
}

char *
gai_strerror(ecode)
	int ecode;
{
	static char *ai_errlist[] = {
		"Success",
		"Address family for hostname not supported",
		"Temporary failure in name resolution",
		"Invalid value for ai_flags",
		"Non-recoverable failure in name resolution",
		"ai_family not supported",
		"Memory allocation failure",
		"No address associated with hostname",
		"hostname nor servname provided, or not known",
		"servname not supported for ai_socktype",
		"ai_socktype not supported",
		"System error returned in errno",
		"Invalid value for hints",
		"Resolved protocol is unknown",
		"Argument buffer overflow"
	};

	if (ecode < 0 || ecode > EAI_MAX)
		return "Unknown error";
	return ai_errlist[ecode];
}

void
freeaddrinfo(ai)
	struct addrinfo *ai;
{
	struct addrinfo *next;

	while (ai != NULL) {
		next = ai->ai_next;
		if (ai->ai_canonname != NULL)
			free(ai->ai_canonname);
		free(ai);
		ai = next;
	}
}

int
getaddrinfo(hostname, servname, hints, res)
	const char *hostname;
	const char *servname;
	const struct addrinfo *hints;
	struct addrinfo **res;
{
	struct hostent *he;
	struct addrinfo *head;
	struct addrinfo *tail;
	struct in_addr addr;
	int types[2];
	int protocols[2];
	int ports[2];
	int ntypes;
	int candidate_types[2];
	int candidate_protocols[2];
	int candidates;
	int j;
	int flags;
	int socktype;
	int protocol;
	int error;
	int result;
	int i;
	int herrno;
	int saved_h_errno;
	const char *canonname;

	usetup;

	/*
	 * getaddrinfo() reports resolver failures through EAI_* return values.
	 * Preserve the legacy resolver state visible through h_errno.
	 */
	saved_h_errno = h_errno;
	head = NULL;
	tail = NULL;
	result = 0;

	if (res == NULL) {
		result = EAI_FAIL;
		goto done;
	}
	*res = NULL;

	if (hostname == NULL && servname == NULL) {
		result = EAI_NONAME;
		goto done;
	}

	error = validate_hints(hints, &flags, &socktype, &protocol);
	if (error != 0) {
		result = error;
		goto done;
	}

	/* An unspecified type/protocol requests both common transports. */
	if (socktype == 0 && protocol == 0) {
		candidate_types[0] = SOCK_STREAM;
		candidate_protocols[0] = IPPROTO_TCP;
		candidate_types[1] = SOCK_DGRAM;
		candidate_protocols[1] = IPPROTO_UDP;
		candidates = 2;
	} else {
		candidate_types[0] = socktype;
		candidate_protocols[0] = protocol;
		candidates = 1;
	}

	ntypes = 0;
	for (j = 0; j < candidates; ++j) {
		int port;

		error = resolve_service(servname, candidate_types[j], flags,
		    &port);
		/* A named service may exist for TCP but not UDP, or vice versa. */
		if (error == EAI_SERVICE && candidates == 2)
			continue;
		if (error != 0) {
			result = error;
			goto done;
		}
		types[ntypes] = candidate_types[j];
		protocols[ntypes] = candidate_protocols[j];
		ports[ntypes] = port;
		++ntypes;
	}
	if (ntypes == 0) {
		result = EAI_SERVICE;
		goto done;
	}

	if (hostname == NULL) {
		if (flags & AI_PASSIVE)
			addr.s_addr = htonl(INADDR_ANY);
		else
			addr.s_addr = htonl(INADDR_LOOPBACK);

		error = append_address(&addr, types, protocols, ports,
		    ntypes, NULL, &head, &tail);
		if (error != 0) {
			result = error;
			goto done;
		}
		*res = head;
		head = NULL;
		goto done;
	}

	if (parse_ipv4_addr(hostname, &addr)) {
		canonname = (flags & AI_CANONNAME) ? hostname : NULL;
		error = append_address(&addr, types, protocols, ports,
		    ntypes, canonname, &head, &tail);
		if (error != 0) {
			result = error;
			goto done;
		}
		*res = head;
		head = NULL;
		goto done;
	}

	if (flags & AI_NUMERICHOST) {
		result = EAI_NONAME;
		goto done;
	}

	he = gethostbyname(hostname);
	if (he == NULL) {
		herrno = h_errno;
		result = host_error_to_eai(herrno);
		goto done;
	}
	if (he->h_addrtype != AF_INET ||
	    he->h_length < (int)sizeof(struct in_addr) ||
	    he->h_addr_list == NULL) {
		result = EAI_FAMILY;
		goto done;
	}

	for (i = 0; he->h_addr_list[i] != NULL; ++i) {
		memcpy(&addr, he->h_addr_list[i], sizeof(addr));
		canonname = (head == NULL && (flags & AI_CANONNAME)) ?
		    he->h_name : NULL;
		error = append_address(&addr, types, protocols, ports,
		    ntypes, canonname, &head, &tail);
		if (error != 0) {
			result = error;
			goto done;
		}
	}

	if (head == NULL) {
		result = EAI_NONAME;
		goto done;
	}

	*res = head;
	head = NULL;

done:
	if (head != NULL)
		freeaddrinfo(head);
	h_errno = saved_h_errno;
	return result;
}

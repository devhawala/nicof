/*
** SOCKET.C  - NICOF socket client API implementation
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module implements the subset of the general (BSD-like) socket-API
** supported by NICOF TCP/IP services for VM/370R6
**
** Current restrictions:
** - only AF_INET and IPv4
**   (restriction may be lifted some day?)
** - no ICMP support for DATAGRAM sockets
**   (probably permanent Java restriction)
** - no OUT-OF-BAND
**   (probably permanent restriction)
** - no integration with the (GCCLIB) C-API, so file descriptors
**   files and sockets are separate things, so C-API functions cannot
**   be used with sockets (read(), write(), close())
**   (probably permanent restriction)
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2014
** Released to the public domain.
*/
 
#include <errno.h>
 
#include "nicofclt.h"
#include "socket.h"
 
#define Printf(...) /* define away the implementation time output messages */
 
/* the name of the internal proxy-VM for the socket implementation */
static const char *proxy_userid = "TCPIPPXY";
 
/* request structures when invoking the external TCP/IP proxy :
 
   userword1 = 0xCCCCSSSS
     -> CCCC = command
     -> SSSS = socket fileno
 
   userword2 = 0xAATTPP00 for ALLOCSOCKET
     -> AA = address family (AF_INET is supported only)
     -> TT = socket type (1 = SOCK_STREAM or 2 = SOCK_DGRAM)
     -> PP = IP protocol (6 = TCP or 17 = UDP)
 
   userword2 = (any) as options for SEND, RECV, SENDTO, RECVFROM
 
*/
 
/* commands to the external TCP/IP proxy */
 
#define CMD_GETHOSTBYNAME 16
#define CMD_GETHOSTBYADDR 17
 
#define CMD_ALLOCSOCKET 32
#define CMD_CLOSE 33
#define CMD_BIND 34
#define CMD_CONNECT 35
#define CMD_LISTEN 36
#define CMD_ACCEPT 37
#define CMD_GETSOCKNAME 38
#define CMD_GETPEERNAME 39
#define CMD_SHUTDOWN 40
#define CMD_RECV 48
#define CMD_RECVFROM 49
#define CMD_SEND 50
#define CMD_SENDTO 51
 
/*
** global data
*/
 
/* the 'special' errno variable for gethostby* functions */
int h_errno = 0;
 
/* the static buffer for return values of the gethostby* functions */
#define HOSTENT_MAX_POINTERS 128
static struct hostent ghostent;
static char hostent_data[2048];
static uint hostent_datalen;
static char* hostent_pointers[HOSTENT_MAX_POINTERS];
 
/*
** socket management
*/
 
/* internal representation of a socket */
typedef struct __sock {
  uint flags;
  ushort recvFrom;           /* recv(): next pos in recvHandle to deliver */
  ushort recvRemaining;      /* recv(): byte count remaining to deliver */
  request_handle recvHandle; /* for: all socket management and receive ops */
  request_handle sendHandle; /* for: send/sendto ops */
} SOCKENTRY, *SOCK;
 
 
/*
** flags and macros for socket management in a 'struct __sock'
*/
 
#define F_ACTIVE    0xC0000003    /* for a socket entry in use */
#define F_CLIENT    0x10000000    /* this is a client socket */
#define F_SERVER    0x20000000    /* this is a server socket */
#define F_DGRAM     0x08000000    /* this is a DATAGRAM socket */
 
#define F_INIT      0x01000000    /* recvHandle has a management operation */
#define F_NONBLOCK  0x00100000    /* socket is in non-blocking mode */
#define F_NBSELECT  0x00200000    /* be non-blocking inside selectX() */
 
#define F_PENDCONN  0x00000004    /* non-blocking connect() underway */
#define F_PENDACPT  0x00000008    /* non-blocking accept() underway */
#define F_PENDRECV  0x00000010    /* non-blocking recv() underway */
#define F_PENDRECVF 0x00000020    /* non-blocking recvfrom() underway */
 
#define GETSOCK(_sockfd) \
  initSockets(); \
  SOCK sock = getSOCK((_sockfd)); \
  if (sock == NULL) { errno = ENOTSOCK; return -1; }
 
#define SOCK_SET(_flag) sock->flags |= (_flag)
#define SOCK_UNSET(_flag) sock->flags &= (~(_flag))
 
#define SOCK_ISSET(_flag) ((sock->flags & (_flag)) == (_flag))
#define SOCK_ISNOTSET(_flag) ((sock->flags & (_flag)) != (_flag))
 
#define SOCKERR_IF_SET(_flag, _errcode) \
  if (SOCK_ISSET((_flag))) { errno = (_errcode); return -1; }
 
#define SOCKERR_IF_NOTSET(_flag, _errcode) \
  if (SOCK_ISNOTSET((_flag))) { errno = (_errcode); return -1; }
 
#define SOCKERR_IF_INPROGRESS(_handle,_errcode) \
  if (sock->_handle != NULL_REQUEST) { errno = _errcode; return -1; }
 
#define SOCKERR_IF_INPROGRESSOTHER(_handle,_flag,_errcode) \
  if (sock->_handle != NULL_REQUEST && SOCK_ISNOTSET((_flag))) \
  { errno = _errcode; return -1; }
 
#define RET_WITH_ERR(_errcode) { errno = (_errcode); return -1; }
 
 
/*
** the sockets for this process (VM)
*/
static SOCKENTRY sockets[FD_SETSIZE];
static char doInit = 'X';
 
#define initSockets() \
  {if (doInit) { \
  nicofclt_init(); \
  memset(sockets, '\0', sizeof(sockets)); doInit = '\0'; }}
 
#define getSOCK(sockno) (\
 (sockno < 0 || sockno >= FD_SETSIZE) ? NULL :\
 (((sockets[sockno].flags & F_ACTIVE) != F_ACTIVE) ? NULL : &sockets[sockno]))
 
/*
** local functions
*/
 
static request_handle newSockRequest(_full cmd, _full sockNo, _full w2) {
  _full w1 = ((cmd & 0xffff) << 16) + (sockNo & 0xffff);
  return nicofclt_createRequest(w1, w2);
}
 
static int closeproxysocket(int sockNo) {
  Printf("... closeproxysocket(%d) : getting request\n", sockNo);
  request_handle h = newSockRequest(CMD_CLOSE, sockNo, 0);
  if (h == NULL) {
    errno = NO_RECOVERY;
    return -1;
  }
  Printf("... closeproxysocket() : sending request and waiting\n");
  errno = nicofclt_sendRequestToAndWait(h, proxy_userid);
  if (errno != 0) {
    nicofclt_freeRequest(h);
    return -1;
  }
  nicofclt_freeRequest(h);
  return 0;
}
 
/*
** API functions
*/
 
char * nicofsocket_errmsg(int code) {
  switch(code) {
   case EOK             : return "EOK";
   case EAFNOSUPPORT    : return "EAFNOSUPPORT";
   case EPROTONOSUPPORT : return "EPROTONOSUPPORT";
   case EMFILE          : return "EMFILE";
   case ENOTSOCK        : return "ENOTSOCK";
   case EUNSPEC         : return "EUNSPEC";
   case EINVAL          : return "EINVAL";
   case EACCES          : return "EACCES";
   case EADDRINUSE      : return "EADDRINUSE";
   case ENOTCONN        : return "ENOTCONN";
   case EOPNOTSUPP      : return "EOPNOTSUPP";
   case ECONNRESET      : return "ECONNRESET";
   case EDESTADDRREQ    : return "EDESTADDRREQ";
   case EISCONN         : return "EISCONN";
   case ECONNABORTED    : return "ECONNABORTED";
   case ECONNREFUSED    : return "ECONNREFUSED";
 
   case HOST_NOT_FOUND  : return "HOST_NOT_FOUND";
   case NO_ADDRESS      : return "NO_NOADDRESS";
   case NO_RECOVERY     : return "NO_RECOVERY";
 
   case EINPROGRESS     : return "EINPROGESS";
   case EALREADY        : return "EALREADY";
   case EWOULDBLOCK     : return "EWOULDBLOCK";
   case EBADF           : return "EBADF";
 
   default: return nicofclt_errmsg(code);
  }
}
 
static struct hostent* doGetHost(request_handle h) {
/*Printf("... sending request and waiting\n");*/
  h_errno = nicofclt_sendRequestToAndWait(h, proxy_userid);
  if (h_errno != 0) {
    nicofclt_freeRequest(h);
    return NULL;
  }
 
  /* interpret the returned data and build the hostent structure */
/*Printf("... getting response data\n");*/
  h_errno = nicofclt_getResponseData(h, sizeof(hostent_data),
                 hostent_data, &hostent_datalen);
  if (h_errno != 0) {
    nicofclt_freeRequest(h);
    return NULL;
  }
/*Printf("... hostent_datalen = %d\n", hostent_datalen);*/
  if (hostent_datalen < 16) { /* minimal size for 1 address without h_name! */
    h_errno = HOST_NOT_FOUND;
    nicofclt_freeRequest(h);
    return NULL;
  }
  if (hostent_datalen > 2048) { hostent_datalen = 2048; }
  if (hostent_datalen < 2048) {
    memset(&hostent_data[hostent_datalen], '\0', 2048 - hostent_datalen);
  }
 
  uint addrCount = *((uint*)&hostent_data[4]);
  uint aliasCount = *((uint*)&hostent_data[8]);
 
/*Printf("... addrCount = %d\n", addrCount);*/
/*Printf("... aliasCount = %d\n", aliasCount);*/
 
  ghostent.h_addrtype = *((ncs_short*)hostent_data);
  ghostent.h_length = *((ncs_short*)&hostent_data[2]);
  ghostent.h_name = &hostent_data[12 + (addrCount * 4)];
  ghostent.h_addr_list = hostent_pointers;
 
/*Printf("... hostent.h_name = '%s'\n", ghostent.h_name);*/
 
  int maxAddrs = HOSTENT_MAX_POINTERS - 2; /* 2 last null-pointers */
  int maxAliases = HOSTENT_MAX_POINTERS - addrCount - 2; /* 2 null-pointers! */
  char *alias = ghostent.h_name + strlen(ghostent.h_name) + 1;
 
  if (addrCount > maxAddrs) { addrCount = maxAddrs; }
  hostent_pointers[0] = &hostent_data[12];
  int i;
  for (i = 1; i < addrCount; i++) {
    hostent_pointers[i] = hostent_pointers[i-1] + 4;
  }
  hostent_pointers[i++] = NULL;
 
  ghostent.h_aliases = &hostent_pointers[i];
  if (aliasCount > maxAliases) { aliasCount = maxAliases; }
  if (aliasCount < 0) { aliasCount = 0; }
  while(aliasCount-- > 0) {
    hostent_pointers[i++] = alias;
    alias += strlen(alias) + 1;
  }
  hostent_pointers[i] = NULL;
 
  return &ghostent;
}
 
struct hostent* gethostbyname(const ncs_char *name) {
 
  /* initialize if needed */
  initSockets();
 
  /* reset hostent data */
  memset(&ghostent, '\0', sizeof(ghostent));
  memset(hostent_pointers, '\0', sizeof(hostent_pointers));
/*Printf("gethostbyname('%s')...\n", name);*/
 
  /* check if we have something like a host name */
  if (!name || !*name) {
    h_errno = HOST_NOT_FOUND;
    return NULL;
  }
 
  /* build the request and send it to the outside proxy */
/*Printf("... getting request\n");*/
  request_handle h = newSockRequest(CMD_GETHOSTBYNAME, 0, 0);
  if (h == NULL) {
    h_errno = NO_RECOVERY;
    return NULL;
  }
/*Printf("... setting request data\n");*/
  h_errno = nicofclt_setRequestData(h, strlen(name), name);
  if (h_errno != 0) {
    nicofclt_freeRequest(h);
    return NULL;
  }
 
  return doGetHost(h);
}
 
struct hostent* gethostbyaddr(const ncs_char *addr, int len, int fmt) {
 
  /* initialize if needed */
  initSockets();
 
  /* reset hostent data */
  memset(&ghostent, '\0', sizeof(ghostent));
  memset(hostent_pointers, '\0', sizeof(hostent_pointers));
/*Printf("gethostbyaddr(%d.%d.%d.%d, %d, %d)...\n",
    addr[0], addr[1], addr[2], addr[3], len, fmt);*/
 
  /* check if we have an IP address */
  if (fmt != AF_INET) {
    h_errno = EAFNOSUPPORT;
    return NULL;
  }
  if (!addr || len != 4) {
    h_errno = HOST_NOT_FOUND;
    return NULL;
  }
 
  /* build the request and send it to the outside proxy */
/*Printf("... getting request\n");*/
  request_handle h = newSockRequest(CMD_GETHOSTBYADDR, 0, 0);
  if (h == NULL) {
    h_errno = NO_RECOVERY;
    return NULL;
  }
/*Printf("... setting request data\n");*/
  ncs_char reqData[6];
  reqData[0] = (ncs_char)0;
  reqData[1] = (ncs_char)2;
  memcpy(&reqData[2], addr, 4);
  h_errno = nicofclt_setRequestData(h, sizeof(reqData), reqData);
  if (h_errno != 0) {
    nicofclt_freeRequest(h);
    return NULL;
  }
 
  return doGetHost(h);
}
 
SOCKET socket(
    ncs_int address_family,
    ncs_int socket_type,
    ncs_int protocol) {
 
  /* initialize if needed */
  initSockets();
 
  /* choose protocol if defaulted */
  if (protocol == 0) {
    if (address_family == AF_INET && socket_type == SOCK_STREAM) {
      protocol = IPPROTO_TCP;
    } else if (address_family == AF_INET && socket_type == SOCK_DGRAM) {
      protocol = IPPROTO_UDP;
    } else {
      errno = EPROTONOSUPPORT;
      return -1;
    }
  }
 
  /* request a new socket from external proxy */
  uint w2 = ((address_family & 0x000000FF) << 24)
          | ((socket_type & 0x000000FF) << 16)
          | ((protocol & 0x000000FF) << 8);
  Printf("... socket() : getting request (w2: 0x%08x)\n", w2);
  request_handle h = newSockRequest(CMD_ALLOCSOCKET, 0, w2);
  if (h == NULL) {
    errno = NO_RECOVERY;
    return -1;
  }
  Printf("... socket() : sending request and waiting\n");
  errno = nicofclt_sendRequestToAndWait(h, proxy_userid);
  if (errno != 0) {
    nicofclt_freeRequest(h);
    return -1;
  }
 
  /* interpret and handle the response */
  uint w1;
  errno = nicofclt_getResponseUserWords(h, &w1, &w2);
  Printf("... socket() -> userwords: w1 = 0x%08x   w2 = 0x%08x\n", w1, w2);
  nicofclt_freeRequest(h);
  if (errno != 0) {
    return -1;
  }
  errno = w1 & 0xFFFF0000;
  if (errno != EOK) {
    return -1;
  }
  uint newSockNo = w1 & 0x0000FFFF;
  Printf("... socket() -> newSockNo = %d\n", newSockNo);
  if (newSockNo >= FD_SETSIZE) {
    Printf("   ## newSockNo >= FD_SETSIZE => socket dropped => EMFILE\n");
    closeproxysocket(newSockNo);
    errno = EMFILE;
    return -1;
  }
  memset(&sockets[newSockNo], '\0', sizeof(SOCKENTRY));
  if (socket_type == SOCK_DGRAM) {
    /* datagram sockets are immediately usable for sending/receiving */
    sockets[newSockNo].flags = F_ACTIVE | F_DGRAM | F_CLIENT;
  } else {
    /* stream sockets must connect or listen to be somehow usable */
    sockets[newSockNo].flags = F_ACTIVE;
  }
  return newSockNo;
}
 
int closesocket(SOCKET sockfd) {
 
  initSockets(); /* initialize if needed */
 
  Printf("... closesocket(%d)\n", sockfd);
  SOCK sock = getSOCK(sockfd);
  if (sock == NULL) {
    errno = ENOTSOCK;
    return -1;
  }
 
  sockets[sockfd].flags = 0;
  return closeproxysocket(sockfd);
}
 
int shutdown(SOCKET sockfd, int how) {
 
  initSockets(); /* initialize if needed */
 
  Printf("... shutdown(%d, %d)\n", sockfd, how);
 
  GETSOCK(sockfd);
  SOCKERR_IF_SET(F_SERVER, EOPNOTSUPP);
  SOCKERR_IF_NOTSET(F_CLIENT, ENOTCONN);
  if (how < 0 || how > 2) {
    errno = EINVAL;
    return -1;
  }
 
  Printf("... shutdown() : getting request\n");
  request_handle h = newSockRequest(CMD_SHUTDOWN, sockfd, 0);
  if (h == NULL) {
    RET_WITH_ERR(NO_RECOVERY);
  }
 
  Printf("... shutdown() : setting request data\n");
  char howparm = (char)how;
  errno = nicofclt_setRequestData(h, 1, &howparm);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  Printf("... shutdown() : sending request and waiting\n");
  errno = nicofclt_sendRequestToAndWait(h, proxy_userid);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  Printf("... shutdown() : interpreting response\n");
  uint uw1, uw2;
  errno = nicofclt_getResponseUserWords(h, &uw1, &uw2);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  errno = (int)uw1 & 0xFFFF0000;
  nicofclt_freeRequest(h);
  if (errno != EOK) { return -1; }
 
  return 0;
}
 
ncs_ulong inet_addr(const ncs_char *addr) {
  int nibbles[4];
  nibbles[0] = 0;
  nibbles[1] = 0;
  nibbles[2] = 0;
  nibbles[3] = 0;
  int n = 0;
  char *c = (char*)addr;
  if (!addr) { h_errno = EINVAL; return 0xFFFFFFFF; }
  while(*c) {
    if (*c >= '0' && *c <= '9') {
      nibbles[n] = (nibbles[n] * 10) + (*c - '0');
      if (nibbles[n] > 255) { h_errno = EINVAL; return 0xFFFFFFFF; }
    } else if (*c == '.') {
      n++;
      if (n >= 4) { h_errno = EINVAL; return 0xFFFFFFFF; }
    } else {
      return h_errno = EINVAL; 0xFFFFFFFF;
    }
    c++;
  }
 
  h_errno = EOK;
  return (nibbles[0] << 24)
       | (nibbles[1] << 16)
       | (nibbles[2] << 8)
       | (nibbles[3]);
}
 
int connect(
    SOCKET sockfd,
    const struct sockaddr *addr,
    ncs_int addrlen) {
 
  initSockets(); /* initialize if needed */
 
  GETSOCK(sockfd);
  SOCKERR_IF_SET(F_SERVER, EISCONN);
  SOCKERR_IF_SET(F_CLIENT, EISCONN);
  SOCKERR_IF_INPROGRESSOTHER(recvHandle, F_PENDCONN, EBADF);
 
  request_handle h = sock->recvHandle;
 
  if (h == NULL_REQUEST) {
    /* build the request data:
    **  2 bytes: target address family
    **  4 bytes: target address (ip v4)
    **  2 bytes: target port
    ** (by pure coincidence the first 8 bytes in the structure of SOCKADDR_IN)
    */
    if (addrlen < 8 || addr->sa_family != AF_INET) {
      RET_WITH_ERR(EAFNOSUPPORT);
    }
    Printf("... connect() : getting request\n");
    h = newSockRequest(CMD_CONNECT, sockfd, 0);
    if (h == NULL) {
      RET_WITH_ERR(NO_RECOVERY);
    }
 
    Printf("... connect() : setting request data\n");
    errno = nicofclt_setRequestData(h, 8, (char*)addr);
    if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
    Printf("... connect() : sending request\n");
    errno = nicofclt_sendRequestTo(h, proxy_userid);
    if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
    if (SOCK_ISSET(F_NBSELECT) || SOCK_ISSET(F_NONBLOCK)) {
      sock->recvHandle = h;
      SOCK_SET(F_PENDCONN | F_INIT);
      RET_WITH_ERR(EWOULDBLOCK);
    }
  }
 
  if (SOCK_ISSET(F_NONBLOCK) && !nicofclt_isAvailable(h)) {
    RET_WITH_ERR(EALREADY);
  }
 
  Printf("... connect() : waiting for response\n");
  sock->recvHandle = NULL_REQUEST;
  SOCK_UNSET(F_PENDCONN | F_INIT);
  errno = nicofclt_waitForResponse(h);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  Printf("... connect() : interpreting response\n");
  uint uw1, uw2;
  errno = nicofclt_getResponseUserWords(h, &uw1, &uw2);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  errno = (int)uw1;
  nicofclt_freeRequest(h);
 
  if (errno != EOK) { return -1; }
 
  SOCK_SET(F_CLIENT);
  return 0;
}
 
int send(
    SOCKET sockfd,
    const ncs_char *buf,
    ncs_int buflen,
    ncs_uint flags) {
 
  initSockets(); /* initialize if needed */
 
  GETSOCK(sockfd);
  SOCKERR_IF_SET(F_SERVER, ENOTCONN);
  SOCKERR_IF_NOTSET(F_CLIENT, ENOTCONN);
  if (buflen < 0) {
    errno = EINVAL;
    return -1;
  }
 
  if (buflen > 2048) { buflen = 2048; }
  request_handle h = sock->sendHandle;
 
  if (h == NULL_REQUEST) {
    if (buflen < 1) { return 0; } /* no bytes to send => done */
 
    Printf("... send() : getting request\n");
    h = newSockRequest(CMD_SEND, sockfd, flags);
    if (h == NULL) { RET_WITH_ERR(NO_RECOVERY); }
 
    Printf("... send() : setting request data\n");
    errno = nicofclt_setRequestData(h, buflen, (char*)buf);
    if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
    Printf("... send() : sending request\n");
    errno = nicofclt_sendRequestTo(h, proxy_userid);
    if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
    if (SOCK_ISSET(F_NBSELECT) || SOCK_ISSET(F_NONBLOCK)) {
      sock->sendHandle = h;
      RET_WITH_ERR(EWOULDBLOCK);
    }
  }
 
  if (SOCK_ISSET(F_NONBLOCK) && !nicofclt_isAvailable(h)) {
    RET_WITH_ERR(EALREADY);
  }
 
  Printf("... send() : waiting for response\n");
  sock->sendHandle = NULL_REQUEST;
  errno = nicofclt_waitForResponse(h);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  Printf("... send() : interpreting response\n");
  uint uw1, uw2;
  errno = nicofclt_getResponseUserWords(h, &uw1, &uw2);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  errno = (int)uw1 & 0xFFFF0000;
  nicofclt_freeRequest(h);
 
  if (errno != EOK) { return -1; }
 
  return uw1 & 0xFFFF; /* number of bytes sent */
}
 
int recv(
    SOCKET sockfd,
    ncs_char *buf,
    ncs_int buflen,
    ncs_uint flags) {
 
  initSockets(); /* initialize if needed */
 
  GETSOCK(sockfd);
  SOCKERR_IF_SET(F_SERVER, ENOTCONN);
  SOCKERR_IF_NOTSET(F_CLIENT, ENOTCONN);
  SOCKERR_IF_INPROGRESSOTHER(recvHandle, F_PENDRECV, EINPROGRESS); /* ?? ** */
  if (buflen < 0) {
    errno = EINVAL;
    return -1;
  }
 
  int recvCount = 0; /* number of bytes retrieved from response */
  request_handle h = sock->recvHandle;
 
  if (h != NULL_REQUEST && sock->recvRemaining > 0) {
    Printf("... recv() : retrieving next chunk"
        " (sockfd: %s, remaining: %d)\n",
        sockfd, sock->recvRemaining);
    uint newRemaining = 0;
    uint newFrom = 0;
    if (buflen < sock->recvRemaining) {
      newRemaining = sock->recvRemaining - buflen;
      newFrom = sock->recvFrom + buflen;
    }
    errno = nicofclt_getResponseDataFrom(h, buflen, buf, &recvCount,
                                         sock->recvFrom);
    if (errno != 0) {
      Printf("** recv(): unable to getResponseDataFrom(), errno: %d (%s)\n",
        errno, nicofsocket_errmsg(errno));
      nicofclt_freeRequest(h);
      sock->recvHandle = NULL_REQUEST;
      sock->recvRemaining = 0;
      sock->recvFrom = 0;
      return -1;
    }
    sock->recvRemaining = newRemaining;
    sock->recvFrom = newFrom;
    Printf("     -> newRemaining: %d, newFrom: %d\n", newRemaining, newFrom);
    if (newRemaining == 0) {
      nicofclt_freeRequest(h);
      sock->recvHandle = NULL_REQUEST;
    }
    return recvCount;
  }
 
  if (buflen > 2048) { buflen = 2048; }
 
  if (h == NULL_REQUEST) {
    if (buflen < 1) { return 0; } /* no room to receive => done */
 
    Printf("... recv() : getting request\n");
    h = newSockRequest(CMD_RECV, sockfd, flags);
    if (h == NULL) { RET_WITH_ERR(NO_RECOVERY); }
 
    Printf("... recv() : setting request data\n");
    char blen[2];
    blen[0] = (char)((buflen & 0xFF00) >> 8);
    blen[1] = ((char)buflen & 0x00FF);
    errno = nicofclt_setRequestData(h, sizeof(blen), blen);
    if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
    Printf("... recv() : sending request\n");
    errno = nicofclt_sendRequestTo(h, proxy_userid);
    if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
    if (SOCK_ISSET(F_NBSELECT) || SOCK_ISSET(F_NONBLOCK)) {
      sock->recvHandle = h;
      SOCK_SET(F_PENDRECV);
      RET_WITH_ERR(EWOULDBLOCK);
    }
  }
 
  if (SOCK_ISSET(F_NONBLOCK) && !nicofclt_isAvailable(h)) {
    RET_WITH_ERR(EALREADY);
  }
 
  Printf("... recv() : waiting for response\n");
  sock->recvHandle = NULL_REQUEST;
  SOCK_UNSET(F_PENDRECV);
  errno = nicofclt_waitForResponse(h);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  Printf("... recv() : interpreting response\n");
  uint uw1, uw2;
  errno = nicofclt_getResponseUserWords(h, &uw1, &uw2);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  errno = (int)uw1 & 0xFFFF0000;
  if (errno != EOK) { nicofclt_freeRequest(h); return -1; }
 
  uint transmitCount;
  errno = nicofclt_getResponseDataLength(h, &transmitCount);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  errno = nicofclt_getResponseData(h, buflen, buf, &recvCount);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  if (recvCount < transmitCount) {
    Printf("... recv() : partial recv, transmitCount: %d, recvCount: %d\n",
      transmitCount, recvCount);
    sock->recvHandle = h;
    sock->recvRemaining = transmitCount - recvCount;
    sock->recvFrom = recvCount;
  } else {
    nicofclt_freeRequest(h);
  }
 
  return recvCount;
}
 
int sendto(
    SOCKET sockfd,
    const ncs_char *buf,
    ncs_int buflen,
    ncs_uint flags,
    const struct sockaddr *to,
    ncs_int tolen) {
 
  initSockets(); /* initialize if needed */
 
  GETSOCK(sockfd);
  SOCKERR_IF_SET(F_SERVER, EOPNOTSUPP);
  SOCKERR_IF_NOTSET(F_CLIENT, EOPNOTSUPP);
  if (buflen < 0) {
    errno = EINVAL;
    return -1;
  }
 
  if (tolen <= 0 || to == NULL) {
    return send(sockfd, buf, buflen, flags);
  }
 
  if (buflen > 2032) { buflen = 2032; } /* 2048 - 16 bytes for the address */
  request_handle h = sock->sendHandle;
 
  if (h == NULL_REQUEST) {
    if (buflen < 1) { return 0; } /* no bytes to send => done */
 
    Printf("... sendTo() : getting request\n");
    h = newSockRequest(CMD_SENDTO, sockfd, flags);
    if (h == NULL) { RET_WITH_ERR(NO_RECOVERY); }
 
    Printf("... sendTo() : setting request data\n");
    errno = nicofclt_setRequestDataX(h, 16, (char*)to, buflen, (char*)buf);
    if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
    Printf("... sendTo() : sending request\n");
    errno = nicofclt_sendRequestTo(h, proxy_userid);
    if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
    if (SOCK_ISSET(F_NBSELECT) || SOCK_ISSET(F_NONBLOCK)) {
      sock->sendHandle = h;
      RET_WITH_ERR(EWOULDBLOCK);
    }
  }
 
  if (SOCK_ISSET(F_NONBLOCK) && !nicofclt_isAvailable(h)) {
    RET_WITH_ERR(EALREADY);
  }
 
  Printf("... sendTo() : waiting for response\n");
  sock->sendHandle = NULL_REQUEST;
  errno = nicofclt_waitForResponse(h);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  Printf("... sendTo() : interpreting response\n");
  uint uw1, uw2;
  errno = nicofclt_getResponseUserWords(h, &uw1, &uw2);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  errno = (int)uw1 & 0xFFFF0000;
  nicofclt_freeRequest(h);
 
  if (errno != EOK) { return -1; }
 
  return uw1 & 0xFFFF; /* number of bytes sent */
}
 
int recvfrom(
    SOCKET sockfd,
    ncs_char *buf,
    ncs_int buflen,
    ncs_uint flags,
    const struct sockaddr *from,
    ncs_int *fromlen) {
 
  initSockets(); /* initialize if needed */
 
  GETSOCK(sockfd);
  SOCKERR_IF_SET(F_SERVER, ENOTCONN);
  SOCKERR_IF_NOTSET(F_CLIENT, ENOTCONN);
  SOCKERR_IF_INPROGRESSOTHER(recvHandle, F_PENDRECV, EINPROGRESS); /* ?? ** */
  if (buflen < 0) {
    errno = EINVAL;
    return -1;
  }
 
  request_handle h = sock->recvHandle;
 
  if (buflen > 2032) { buflen = 2032; } /* 16 bytes reserved for address */
 
  if (h == NULL_REQUEST) {
    if (buflen < 1) { return 0; } /* no room to receive => done */
 
    Printf("... recvfrom() : getting request\n");
    h = newSockRequest(CMD_RECVFROM, sockfd, flags);
    if (h == NULL) { RET_WITH_ERR(NO_RECOVERY); }
 
    Printf("... recvfrom() : setting request data\n");
    char blen[2];
    blen[0] = (char)((buflen & 0xFF00) >> 8);
    blen[1] = ((char)buflen & 0x00FF);
    errno = nicofclt_setRequestData(h, sizeof(blen), blen);
    if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
    Printf("... recvfrom() : sending request\n");
    errno = nicofclt_sendRequestTo(h, proxy_userid);
    if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
    if (SOCK_ISSET(F_NBSELECT) || SOCK_ISSET(F_NONBLOCK)) {
      sock->recvHandle = h;
      SOCK_SET(F_PENDRECV);
      RET_WITH_ERR(EWOULDBLOCK);
    }
  }
 
  if (SOCK_ISSET(F_NONBLOCK) && !nicofclt_isAvailable(h)) {
    RET_WITH_ERR(EALREADY);
  }
 
  Printf("... recvfrom() : waiting for response\n");
  sock->recvHandle = NULL_REQUEST;
  SOCK_UNSET(F_PENDRECV);
  errno = nicofclt_waitForResponse(h);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  Printf("... recvfrom() : interpreting response\n");
  uint uw1, uw2;
  errno = nicofclt_getResponseUserWords(h, &uw1, &uw2);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  errno = (int)uw1 & 0xFFFF0000;
  if (errno != EOK) { nicofclt_freeRequest(h); return -1; }
 
  uint transmitCount;
  errno = nicofclt_getResponseDataLength(h, &transmitCount);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
  if (transmitCount < 16) { /* no 'from' address available? */
    errno = ECONNRESET;
    nicofclt_freeRequest(h);
    return -1;
  }
 
  int recvCount;
 
  if (fromlen && *fromlen > 0 && from) {
    int fillLen = (*fromlen >= 16) ? 16 : *fromlen;
    errno = nicofclt_getResponseData(h, fillLen, (char*)from, &recvCount);
    if (errno != 0) { nicofclt_freeRequest(h); return -1; }
    *fromlen = recvCount;
  } else if (fromlen) {
    *fromlen = 0;
  }
 
  errno = nicofclt_getResponseDataFrom(h, buflen, buf, &recvCount, 16);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  nicofclt_freeRequest(h);
 
  return recvCount;
}
 
int bind(
    SOCKET sockfd,
    const struct sockaddr *myaddr,
    ncs_int myaddrlen) {
 
  initSockets(); /* initialize if needed */
 
  GETSOCK(sockfd);
  SOCKERR_IF_SET(F_SERVER, EISCONN);
  if (SOCK_ISNOTSET(F_DGRAM)) { SOCKERR_IF_SET(F_CLIENT, EISCONN); }
  SOCKERR_IF_SET(F_INIT, EINPROGRESS);
 
  Printf("... bind() : getting request\n");
  request_handle h = newSockRequest(CMD_BIND, sockfd, 0);
  if (h == NULL) {
    RET_WITH_ERR(NO_RECOVERY);
  }
 
  Printf("... bind() : setting request data\n");
  errno = nicofclt_setRequestData(h, 8, (char*)myaddr);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  Printf("... bind() : sending request and waiting\n");
  SOCK_SET(F_INIT);
  errno = nicofclt_sendRequestToAndWait(h, proxy_userid);
  SOCK_UNSET(F_INIT);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  Printf("... bind() : interpreting response\n");
  uint uw1, uw2;
  errno = nicofclt_getResponseUserWords(h, &uw1, &uw2);
  nicofclt_freeRequest(h); /* we're done with it */
  if (errno != 0) { return -1; }
 
  errno = (int)uw1 & 0xFFFF0000;
  return (errno == EOK) ? 0 : -1;
}
 
int listen(
    SOCKET sockfd,
    ncs_int backlog) {
 
  initSockets(); /* initialize if needed */
 
  GETSOCK(sockfd);
  if (SOCK_ISSET(F_SERVER)) { errno = EOK; return 0; }
  SOCKERR_IF_SET(F_CLIENT, EOPNOTSUPP);
  SOCKERR_IF_SET(F_INIT, EINPROGRESS);
 
  Printf("... listen() : getting request\n");
  request_handle h = newSockRequest(CMD_LISTEN, sockfd, 0);
  if (h == NULL) {
    RET_WITH_ERR(NO_RECOVERY);
  }
 
  Printf("... listen() : setting request data\n");
  unsigned char blog = (backlog < 0)
                     ? 0 : (backlog > 255)
                     ? 255 : backlog;
  errno = nicofclt_setRequestData(h, 1, (char*)&blog);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  Printf("... listen() : sending request and waiting\n");
  SOCK_SET(F_INIT);
  errno = nicofclt_sendRequestToAndWait(h, proxy_userid);
  SOCK_UNSET(F_INIT);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  Printf("... listen() : interpreting response\n");
  uint uw1, uw2;
  errno = nicofclt_getResponseUserWords(h, &uw1, &uw2);
  nicofclt_freeRequest(h); /* we're done with it */
  if (errno != 0) { return -1; }
 
  errno = (int)uw1 & 0xFFFF0000;
  if (errno != EOK) { return -1; }
 
  SOCK_SET(F_SERVER);
  return 0;
}
 
SOCKET accept(
    SOCKET sockfd,
    const struct sockaddr *addr,
    ncs_int *addrlen) {
 
  initSockets(); /* initialize if needed */
 
  GETSOCK(sockfd);
  SOCKERR_IF_SET(F_CLIENT, EOPNOTSUPP);
  SOCKERR_IF_NOTSET(F_SERVER, EINVAL);
  SOCKERR_IF_INPROGRESSOTHER(recvHandle, F_PENDACPT, EINPROGRESS); /* ?? ** */
 
  if (addr && (!addrlen || *addrlen < 8)) {
    errno = EINVAL;
    return -1;
  }
 
  request_handle h = sock->recvHandle;
 
  if (h == NULL_REQUEST) {
    Printf("... accept() : getting request\n");
    h = newSockRequest(CMD_ACCEPT, sockfd, 0);
    if (h == NULL) { RET_WITH_ERR(NO_RECOVERY); }
 
    Printf("... accept() : sending request\n");
    errno = nicofclt_sendRequestTo(h, proxy_userid);
    if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
    if (SOCK_ISSET(F_NBSELECT) || SOCK_ISSET(F_NONBLOCK)) {
      sock->recvHandle = h;
      SOCK_SET(F_PENDACPT);
      RET_WITH_ERR(EWOULDBLOCK);
    }
  }
 
  if (SOCK_ISSET(F_NONBLOCK) && !nicofclt_isAvailable(h)) {
    RET_WITH_ERR(EALREADY);
  }
 
  Printf("... accept() : waiting for response\n");
  sock->recvHandle = NULL_REQUEST;
  SOCK_UNSET(F_PENDACPT);
  errno = nicofclt_waitForResponse(h);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  Printf("... accept() : interpreting response\n");
 
  uint uw1, uw2;
  errno = nicofclt_getResponseUserWords(h, &uw1, &uw2);
  Printf("... accept() -> userwords: w1 = 0x%08x   w2 = 0x%08x\n", uw1, uw2);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  errno = uw1 & 0xFFFF0000;
  if (errno != EOK) { nicofclt_freeRequest(h); return -1; }
 
  if (addr && addrlen) {
    int err = nicofclt_getResponseData(h, *addrlen, (char*)addr, addrlen);
    if (err != 0) { *addrlen = 0; }
  }
 
  nicofclt_freeRequest(h);
 
  uint newSockNo = uw1 & 0x0000FFFF;
  Printf("... accept() -> newSockNo = %d\n", newSockNo);
  if (newSockNo >= FD_SETSIZE) {
    Printf("   ## newSockNo >= FD_SETSIZE => socket dropped => EMFILE\n");
    closeproxysocket(newSockNo);
    errno = EMFILE;
    return -1;
  }
  sockets[newSockNo].flags = F_ACTIVE | F_CLIENT;
  return newSockNo;
}
 
static int getSockInfo(
    int cmd,
    char *prefix,
    SOCKET sockfd,
    const struct sockaddr *addr,
    ncs_int *addrlen) {
 
  initSockets(); /* initialize if needed */
 
  GETSOCK(sockfd);
 
  if (addr && (!addrlen || *addrlen < 8)) {
    errno = EINVAL;
    return -1;
  }
 
  Printf("... %s() : getting request\n", prefix);
  request_handle h = newSockRequest(cmd, sockfd, 0);
  if (h == NULL) {
    RET_WITH_ERR(NO_RECOVERY);
  }
 
  Printf("... %s() : sending request and waiting\n", prefix);
  errno = nicofclt_sendRequestToAndWait(h, proxy_userid);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  Printf("... %s() : interpreting response\n", prefix);
  uint uw1, uw2;
  errno = nicofclt_getResponseUserWords(h, &uw1, &uw2);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
 
  errno = (int)uw1 & 0xFFFF0000;
  if (errno != EOK) { nicofclt_freeRequest(h); return -1; }
 
  int recvCount; /* number of bytes received */
  errno = nicofclt_getResponseData(h, *addrlen, (char*)addr, &recvCount);
  if (errno != 0) { nicofclt_freeRequest(h); return -1; }
  *addrlen = recvCount;
 
  nicofclt_freeRequest(h);
  return 0;
}
 
int getsockname(
    SOCKET sockfd,
    const struct sockaddr *addr,
    ncs_int *addrlen) {
  return getSockInfo(CMD_GETSOCKNAME, "getsockname", sockfd, addr, addrlen);
}
 
int getpeername(
    SOCKET sockfd,
    const struct sockaddr *addr,
    ncs_int *addrlen) {
  return getSockInfo(CMD_GETPEERNAME, "getpeername", sockfd, addr, addrlen);
}
 
static fd_set dummyFdSet;
 
/* unique filter value      each select[X]() to filter relevant requests */
static currFilterTag = 0;
 
int selectX(
    ncs_int num_fds,
    fd_set *rd_fds_in,
    fd_set *wr_fds_in,
    fd_set *ex_fds_in,   /* ignored */
    fd_set *rd_fds_out,
    fd_set *wr_fds_out,
    fd_set *ex_fds_out,
    const struct timeval *timeout) {
 
  initSockets(); /* should not be necessary when selectX() is called... */
 
  Printf("## selectX() num_fds=%d\n",  num_fds);
 
  /* prepare */
  currFilterTag++;
  FD_ZERO(&dummyFdSet);
  if (!rd_fds_in) { rd_fds_in = &dummyFdSet; }
  if (!wr_fds_in) { wr_fds_in = &dummyFdSet; }
  if (!rd_fds_out) { rd_fds_out = &dummyFdSet; }
  if (!wr_fds_out) { wr_fds_out = &dummyFdSet; }
  if (!ex_fds_out) { ex_fds_out = &dummyFdSet; }
  if (num_fds < 1) { num_fds = 0; }
  if (num_fds >= FD_SETSIZE) { num_fds = FD_SETSIZE; }
 
  /* interpret the input fd-sets */
  int activeHandles = 0;
  int noWait = 0; /* will be 1 if some socket is already ready */
  int i;
  int rc;
  for (i = 0; i < num_fds; i++) {
    SOCK sock = getSOCK(i);
    if (!sock) { continue; }
    if (FD_ISSET(i, rd_fds_in)) {
      if (!sock->recvHandle) {
        SOCK_SET(F_NBSELECT);
        if (SOCK_ISSET(F_SERVER)) {
          rc = accept(i, NULL, NULL);
          Printf("  ... accept(%d, NULL, NULL) -> %d\n", i, rc);
          /* rc should be -1 and errno = EWOULDBLOCK */
        } else if (SOCK_ISSET(F_CLIENT)) {
          SOCK_SET(F_NBSELECT);
          rc = recv(i, NULL, 2048, 0);
          Printf("  ... recv(%d, 2048) -> %d\n", i, rc);
          /* rc should be -1 and errno = EWOULDBLOCK */
        }
        SOCK_UNSET(F_NBSELECT);
        errno = 0;
      }
      if (sock->recvHandle) {
        Printf("  ... waiting for recvHandle on fd %d\n", i);
        nicofclt_setFilterTag(sock->recvHandle, currFilterTag);
        activeHandles++;
        if (nicofclt_isAvailable(sock->recvHandle)
            || sock->recvRemaining > 0) {
          noWait = 1;
        }
      }
    } else if (sock->recvHandle
               && FD_ISSET(i, wr_fds_in)
               && SOCK_ISSET(F_PENDCONN)) {
      nicofclt_setFilterTag(sock->recvHandle, currFilterTag);
      activeHandles++;
      if (nicofclt_isAvailable(sock->recvHandle)) { noWait = 1; }
    }
    if (FD_ISSET(i, wr_fds_in) && sock->sendHandle) {
      nicofclt_setFilterTag(sock->sendHandle, currFilterTag);
      activeHandles++;
      if (nicofclt_isAvailable(sock->recvHandle)) { noWait = 1; }
    }
  }
 
  /* interpret the timeout */
  uint timeout10ms = NO_TIMEOUT;
  if (timeout && timeout->tv_sec == 0 && timeout->tv_usec == 0) {
    timeout10ms = 0;
  } else if (timeout) {
    timeout10ms = timeout->tv_sec * 100;
    timeout10ms += (timeout->tv_usec + 9999) / 10000;
    if (timeout10ms == 0) { timeout10ms = 1; }
  }
 
  Printf("## selectX() filter=%d activeHandles=%d noWait=%d timeout10ms=%d\n",
    currFilterTag, activeHandles, noWait, timeout10ms);
 
  if (activeHandles == 0) { /* effectively nothing to watch */
    /* some misuse select() as sub-second timer, so wait for timeout ... */
    if (timeout10ms > 0 && timeout10ms != NO_TIMEOUT) {
      _full timer_ecb = 0;
      set_timer(timeout10ms, &timer_ecb);
      wait_ecb(&timer_ecb);
    }
 
    /* ... and return 'nothing' */
    FD_ZERO(rd_fds_out);
    FD_ZERO(wr_fds_out);
    FD_ZERO(ex_fds_out);
    return 0;
  } else if (noWait) {
    timeout10ms = 0;
  }
 
  /* if not a simple poll: wait for a relevant response */
  request_handle h;
  if (timeout10ms != 0) {
    Printf("## begin nicofclt_waitForAnyAvailableX()\n");
    errno = nicofclt_waitForAnyAvailableX(&h, currFilterTag, timeout10ms);
    if (errno != 0 && errno != WAITANY_TIMEDOUT) {
      printf("## done nicofclt_waitForAnyAvailableX()\n");
      printf("## -> errno = %d (%s)\n## => returning -1\n",
        errno, nicofclt_errmsg(errno));
      return -1;
    }
    Printf("## end nicofclt_waitForAnyAvailableX() with %s\n",
     (errno != WAITANY_TIMEDOUT) ? "with response" : "by timeout");
  }
  errno = 0;
 
  /* record the ingone responses in the output fd-sets */
  int activeSockets = 0;
  FD_ZERO(rd_fds_out);
  FD_ZERO(wr_fds_out);
  FD_ZERO(ex_fds_out);
  for (i = 0; i < num_fds; i++) {
    SOCK sock = getSOCK(i);
    int isSet = 0;
    if (!sock) { continue; }
    Printf("  ... checking fd %d\n", i);
    if (sock->recvHandle) {
      Printf("      -> filterTag=%d isAvailable=%d\n",
        nicofclt_getFilterTag(sock->recvHandle),
        nicofclt_isAvailable(sock->recvHandle));
    }
    if (sock->recvHandle
        && nicofclt_getFilterTag(sock->recvHandle) == currFilterTag
        && nicofclt_isAvailable(sock->recvHandle)) {
      Printf("  ... recv-activity on fd %d\n", i);
      if (SOCK_ISSET(F_PENDCONN)) {
        FD_SET(i, wr_fds_out);
      } else {
        FD_SET(i, rd_fds_out);
      }
      isSet = 1;
    }
    if (sock->sendHandle
        && nicofclt_getFilterTag(sock->sendHandle) == currFilterTag
        && nicofclt_isAvailable(sock->sendHandle)) {
      Printf("  ... send-activity on fd %d\n", i);
      FD_SET(i, wr_fds_out);
      isSet = 1;
    }
    if (isSet) { activeSockets++; }
  }
 
  Printf("## selectX() returning %d\n", activeSockets);
  return activeSockets;
}
 
int select(
    ncs_int num_fds,
    fd_set *rd_fds,
    fd_set *wr_fds,
    fd_set *ex_fds,
    const struct timeval *timeout) {
  return selectX(
            num_fds,
            rd_fds, wr_fds, ex_fds,
            rd_fds, wr_fds, ex_fds,
            timeout);
}
 
int ioctlsocket(SOCKET sockfd, long flag, ncs_uint *value) {
  GETSOCK(sockfd);
  if (flag == FIONBIO) {
    if (*value) {
      SOCK_SET(F_NONBLOCK);
    } else {
      SOCK_UNSET(F_NONBLOCK);
    }
    return 0;
  }
 
  errno = EINVAL;
  return -1;
}
 
void dumpSocket(int sockfd) {
  printf("-- socket[%d]:", sockfd);
  SOCK sock = getSOCK(sockfd);
  if (!sock) {
    printf(" invalid !!\n");
    return;
  }
  if (SOCK_ISSET(F_CLIENT)) { printf(" client"); }
  if (SOCK_ISSET(F_SERVER)) { printf(" server"); }
  if (SOCK_ISSET(F_INIT)) { printf(" init"); }
  if (SOCK_ISSET(F_NONBLOCK)) { printf(" nonblock"); }
  if (SOCK_ISSET(F_NBSELECT)) { printf(" nbselect"); }
  if (SOCK_ISSET(F_PENDCONN)) { printf(" pendconn"); }
  if (SOCK_ISSET(F_PENDACPT)) { printf(" pendacpt"); }
  if (SOCK_ISSET(F_PENDRECV)) { printf(" pendrecv"); }
  if (SOCK_ISSET(F_PENDRECVF)) { printf(" pendrecvf"); }
  printf("\n");
 
  if (sock->recvHandle) {
    printf("   -> recvHandle( %s )\n",
      nicofclt_getStateString(sock->recvHandle));
  }
  if (sock->sendHandle) {
    printf("   -> sendHandle( %s )\n",
      nicofclt_getStateString(sock->sendHandle));
  }
}

/*
** SOCKET.H  - NICOF socket client API definitions
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module defines the subset of the general (BSD-like) socket-API
** supported by NICOF TCP/IP services for VM/370 R6
**
** Current restrictions:
** - only AF_INET and IPv4
**   (restriction may be lifted some day?)
** - no ICMP support for DATAGRAM sockets
**   (probably permanent Java restriction)
** - no OUT-OF-BAND
**   (probably permanent restriction)
** - no integration with the (GCCLIB) C-API, so file descriptors for files
**   and sockets are separate things, meaning that C-API functions cannot
**   be used with sockets (read(), write(), close()) and more specifically,
**   a socket must be finalized with closesocket() instead of close()
**   (probably permanent restriction)
**
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2014
** Released to the public domain.
*/
 
#ifndef __NICOF_SOCKET_DEFS
#define __NICOF_SOCKET_DEFS
 
/*
** socket error codes (must be in sync with external Java proxy impl.!)
**
** in general:
**  - errors can occur when communicating with the external proxy through
**    the internal proxy via VMCF, so NICOFCLT-errors can also appear
**    and will be placed in (h_)errno.
**  - NO_RECOVERY is used if a request to the internal proxy cannot
**    be allocated
**  - EUNSPEC is used as 'unspecific' error to signal some 'other'
**    ('unspecified') error
*/
#define RCBASE (0x01000000) /* base for all socket-API (h_)errno values */
 
#define EOK (RCBASE + 0)    /* operation successful */
 
#define EAFNOSUPPORT    (RCBASE + 0x010000)
#define EPROTONOSUPPORT (RCBASE + 0x020000)
#define EMFILE          (RCBASE + 0x030000)
#define ENOTSOCK        (RCBASE + 0x040000)
#define EUNSPEC         (RCBASE + 0x050000)
#define EINVAL          (RCBASE + 0x070000)
#define EACCES          (RCBASE + 0x080000)
#define EADDRINUSE      (RCBASE + 0x090000)
#define ENOTCONN        (RCBASE + 0x0A0000)
#define EOPNOTSUPP      (RCBASE + 0x0B0000)
#define ECONNRESET      (RCBASE + 0x0C0000)
#define EDESTADDRREQ    (RCBASE + 0x0D0000)
#define EISCONN         (RCBASE + 0x0E0000)
#define ECONNABORTED    (RCBASE + 0x0F0000)
#define ECONNREFUSED    (RCBASE + 0x100000)
 
#define HOST_NOT_FOUND  (RCBASE + 0x200000)
#define NO_ADDRESS      (RCBASE + 0x210000)
#define NO_RECOVERY     (RCBASE + 0x220000)
 
#define EINPROGRESS     (RCBASE + 0x900000)
#define EALREADY        (RCBASE + 0x910000)
#define EWOULDBLOCK     (RCBASE + 0x920000)
#define EBADF           (RCBASE + 0x930000)
 
/*
** basic types used to simplify declarations in this file
*/
typedef char ncs_char;
typedef unsigned char ncs_uchar;
typedef signed char ncs_int8;
typedef unsigned char ncs_uint8;
 
typedef short ncs_short;
typedef unsigned short ncs_ushort;
typedef signed short ncs_int16;
typedef unsigned short ncs_uint16;
 
typedef int ncs_int;
typedef unsigned int ncs_uint;
typedef int ncs_long;
typedef unsigned int ncs_ulong;
typedef signed int ncs_int32;
typedef unsigned int ncs_uint32;
 
 
/*
** network <-> host representation
** (in fact the identity transformation, as both network-representation
** and S/370 are big-endian)
*/
 
#define htons(_hostvalue) ((ncs_ushort)(_hostvalue))
#define ntohs(_netvalue) ((ncs_ushort)(_netvalue))
 
#define htonl(_hostvalue) ((ncs_ulong)(_hostvalue))
#define ntohl(_netvalue) ((ncs_ulong)(_netvalue))
 
 
/*
** (supported) address families
*/
 
typedef unsigned short ADDRESS_FAMILY;
 
#define AF_INET 2
 
 
/*
** (supported) socket types
*/
 
#define SOCK_STREAM     1
#define SOCK_DGRAM      2
 
 
/*
** (supported) IP procotol types
*/
 
typedef enum {
    IPPROTO_ICMP = 1,
    IPPROTO_TCP = 6,
    IPPROTO_UDP = 17
} IPPROTO;
 
 
/*
** general socket address structure
*/
 
typedef struct sockaddr {
    ADDRESS_FAMILY sa_family;   /* address family */
    ncs_char       sa_data[14]; /* max. 14 bytes family specific address */
} SOCKADDR;
 
 
/*
** IPv4 socket address structure
*/
 
typedef unsigned int socklen_t;
 
typedef struct in_addr {
    union {
        struct { ncs_uchar s_b1,s_b2,s_b3,s_b4; } S_un_b;
        struct { ncs_ushort s_w1,s_w2; } S_un_w;
        ncs_ulong S_addr;
     } S_un;
} IN_ADDR;
#define s_addr  S_un.S_addr         /* all 4 bytes of the IP address */
#define s_host  S_un.S_un_b.s_b2
#define s_net   S_un.S_un_b.s_b1
#define s_imp   S_un.S_un_w.s_w2
#define s_impno S_un.S_un_b.s_b4
#define s_lh    S_un.S_un_b.s_b3
 
typedef struct sockaddr_in {
    ADDRESS_FAMILY sin_family;    /* must be AF_INET */
    ncs_ushort       sin_port;    /* IPv4 port */
    IN_ADDR          sin_addr;    /* IPv4 address */
    ncs_char         sin_zero[8]; /* unused bytes */
} SOCKADDR_IN;
 
#define INADDR_ANY ((unsigned int) 0x7F000001)
 
 
/* unsigned long <- inet_addr(addr-string)
**
** convert the host address 'addr' from the standard numbers-and-dots
** notation (a.b.c.d) into the binary representation.
**
** Returns 0xFFFFFFFF if the addr-string is not in the form "a.b.c.d" and
** sets h_errno to EINVAL.
*/
extern ncs_ulong inet_addr(const ncs_char *addr);
 
 
/*
** struct hostent used for address resolution
*/
 
struct hostent {
    ncs_char   *h_name;      /* official name of host */
    ncs_char  **h_aliases;   /* alias list */
    ncs_short   h_addrtype;  /* host address type */
    ncs_short   h_length;    /* length of address */
    ncs_char  **h_addr_list; /* list of addresses */
};
#define h_addr  h_addr_list[0] /* "the" address for backward compatibility */
 
 
/* gethostbyname() uses a separate error variable on other systems,
** so the NICOF socket-API also does it this way */
extern int h_errno;
 
 
/* hostent* <- gethostbyname(hostname)
**
** resolve a hostname and return addresses and alias names for the host
**
** possible h_errno values when NULL is returned:
**  HOST_NOT_FOUND
**   -> missing hostname, name not found
**  NO_ADDRESS
**   -> no IPv4 address defined for the (otherwise known) hostname
*/
#define gethostbyname ncs_ghbn
extern struct hostent* gethostbyname(const ncs_char *name);
 
 
/* hostent* <- gethostbyaddr(addr, len, format)
**
** resolve an (IP-)address and return addresses and alias names for the host
**
** possible h_errno values when NULL is returned:
**  EAFNOSUPPORT
**   -> not an IPv4 address ('fmt' is not AF_INET)
**  HOST_NOT_FOUND
**   -> host for address not defined
**  NO_ADDRESS
**   -> no IPv4 address defined for the (otherwise known) hostname
*/
#define gethostbyaddr ncs_ghba
extern struct hostent* gethostbyaddr(const ncs_char *addr, int len, int fmt);
 
#if 0 /* not implemented yet */
/*
** struct servent* <- getservbyname(<servicename>, <protocolname>)
*/
 
struct servent {
    ncs_char   *s_name;     /* official service name */
    ncs_char  **s_aliases;  /* alias list */
    ncs_short   s_port;     /* port number */
    ncs_char   *s_proto;    /* protocol to use */
};
 
extern struct servent* getservbyname(
    const ncs_char *name,
    const ncs_char *proto
    );
#endif
 
 
/*
** (supported) socket operations
*/
 
/* a socket file descriptor is simply an int */
#define SOCKET int
 
/* sockfd <- socket(family, type, protocol)
**
** allocate a new socket, returning the file descriptor of the socket
**
** possible errno values when sockfd < 0 is returned:
**  EAFNOSUPPORT
**   -> address family not supported
**  EPROTONOSUPPORT
**   -> protocol not supported or could not be chosen (if defaulted)
**  EMFILE
**   -> no more file descriptors available (64 sockets already allocated)
*/
extern SOCKET socket(
    ncs_int address_family,
    ncs_int socket_type,
    ncs_int protocol);
 
/* rc <- connect(sockfd, addr, addrlen)
**
** connect the socket with the remote endpoint at 'addr'
**
** possible errno values when rc < 0 is returned:
**  ENOTSOCK
**   -> invalid sockfd (not an allocated socket)
**  EISCONN
**   -> the socket is already connected or is a server (listening) socket
**  EAFNOSUPPORT
**   -> address family of 'addr' not supported
**  EBADF
**   -> a concurrent operation is already underway (non-blocking socket)
**  EWOULDBLOCK
**   -> the socket is non-blocking: the asynchronous operation was started
**  EALREADY
**   -> the socket is non-blocking: the same operation is already started
**      and has not returned so far
**  EINVAL
**   -> invalid address or port given in 'addr'
**  EADDRINUSE
**   -> the previous bind() specified a local address already in use
**  ECONNREFUSED
**   -> connection could not be established to the remote address
*/
extern int connect(
    SOCKET sockfd,
    const struct sockaddr *addr,
    ncs_int addrlen);
 
/* rc <- bind(sockfd, addr, addrlen)
**
** bind the socket to a local address and port
**
** possible errno values when rc < 0 is returned:
**  ENOTSOCK
**   -> invalid sockfd (not an allocated socket)
**  EISCONN
**   -> the socket is already connected or is a server (listening) socket
**  EINPROGRESS
**   -> a concurrent operation is already underway (non-blocking socket)
**  EAFNOSUPPORT
**   -> address family of 'addr' not supported
**  EINVAL
**   -> invalid address or port given in 'addr'
**  EADDRINUSE
**   -> the specified a local address already in use
*/
extern int bind(
    SOCKET sockfd,
    const struct sockaddr *myaddr,
    ncs_int myaddrlen);
 
/* rc <- getsockname(sockfd, &addr, &addrlen)
**
** get the local address and socket of the socket
** (resp. 0.0.0.0:0 if not connected and not bound)
**
** possible errno values when rc < 0 is returned:
**  ENOTSOCK
**   -> invalid sockfd (not an allocated socket)
*/
extern int getsockname(
    SOCKET sockfd,
    const struct sockaddr *addr,
    ncs_int *addrlen);
 
/* rc <- getpeername(sockfd, &addr, &addrlen)
**
** get the remote endpoint address of a connected socket
**
** possible errno values when rc < 0 is returned:
**  ENOTSOCK
**   -> invalid sockfd (not an allocated socket)
**  ENOTCONN
**   -> the socket is not connected
*/
extern int getpeername(
    SOCKET sockfd,
    const struct sockaddr *addr,
    ncs_int *addrlen);
 
/* rc <- listen(sockfd, backlog)
**
** start listening for ingoing connections, i.e. enable the socket
** to accept connections
**
** possible errno values when rc < 0 is returned:
**  ENOTSOCK
**   -> invalid sockfd (not an allocated socket)
**  EOPNOTSUPP
**   -> socket is already connected
**  EINVAL
**   -> socket not bound to a local address
**  EADDRINUSE
**   -> the previous bind() specified a local address already in use
*/
extern int listen(
    SOCKET sockfd,
    ncs_int backlog);
 
/* rc <- accept(sockfd, &addr, &addrlen)
**
** wait for incoming connections and accept the first connection
** for the socket
**
** possible errno values when rc < 0 is returned:
**  ENOTSOCK
**   -> invalid sockfd (not an allocated socket)
**  EOPNOTSUPP
**   -> socket is already connected
**  EINVAL
**   -> socket is not listening for connections
**  EINPROGRESS
**   -> a concurrent operation is already underway (non-blocking socket)
**  EWOULDBLOCK
**   -> the socket is non-blocking: the asynchronous operation was started
**  EALREADY
**   -> the socket is non-blocking: the same operation is already started
**      and has not returned so far
**  ECONNABORTED
**   -> connection has been aborted during accept() processing
*/
extern SOCKET accept(
    SOCKET sockfd,
    const struct sockaddr *addr,
    ncs_int *addrlen);
 
/* count <- send(sockfd, buf, buflen, flags)
**
** transmit a buffer to the remote endpoint the socket is connected to,
** returning the number of bytes transmitted.
**
** If 'sockfd' is a DATAGRAM packet, it must have been bound to a target
** socket with connect(), as unlike sendto(), no specific target for the
** transmission can be specified with send().
**
** NICOF sockets restrictions:
** - the C-library function write() will not work with sockets
** - the transmittable buffer length for a single call is limited to 2048 bytes
** - out-of-band data transmission is not supported
**
** possible errno values when count < 0 is returned:
**  ENOTSOCK
**   -> invalid sockfd (not an allocated socket)
**  EINVAL
**   -> invalid buffer length passed (< 0)
**  ENOTCONN
**   -> socket is not connected to a remote endpoint
**  EINPROGRESS
**   -> a concurrent operation is already underway (non-blocking socket)
**  EWOULDBLOCK
**   -> the socket is non-blocking: the asynchronous operation was started
**  EALREADY
**   -> the socket is non-blocking: the same operation is already started
**      and has not returned so far
**  ECONNABORTED
**   -> connection reset by peer (closed socket or SHUT_RD-shutdown)
*/
extern int send(
    SOCKET sockfd,
    const ncs_char *buf,
    ncs_int buflen,
    ncs_uint flags);
 
/* count <- recv(sockfd, buf, buflen, flags)
**
** receive a buffer from the remote endpoint the socket is connected to,
** returning the number of bytes transmitted
**
** NICOF sockets restrictions:
** - the C-library function write() will not work with sockets
** - the transmittable buffer length for a single call is limited to 2048 bytes
** - out-of-band data transmission is not supported
**
** possible errno values when count < 0 is returned:
**  ENOTSOCK
**   -> invalid sockfd (not an allocated socket)
**  EINVAL
**   -> invalid buffer length passed (< 0)
**  ENOTCONN
**   -> socket is not connected to a remote endpoint
**  EINPROGRESS
**   -> a concurrent operation is already underway (non-blocking socket)
**  EWOULDBLOCK
**   -> the socket is non-blocking: the asynchronous operation was started
**  EALREADY
**   -> the socket is non-blocking: the same operation is already started
**      and has not returned so far
**  ECONNABORTED
**   -> connection reset by peer (closed socket or SHUT_WR-shutdown)
*/
extern int recv(
    SOCKET sockfd,
    ncs_char *buf,
    ncs_int buflen,
    ncs_uint flags);
 
/* rc <- sendto(sockfd, buf, buflen, flags, to, tolen)
**
** send a packet to the specified target socket address through the given
** DATAGRAM  socket, returning the number of bytes transmitted.
** Passing 'to' == NULL or 'tolen' <= 0 is equivalent to a call to send().
**
** possible errno values when count < 0 is returned:
**  ENOTSOCK
**   -> invalid sockfd (not an allocated socket)
**  EINVAL
**   -> invalid buffer length passed (< 0)
**  EAFNOSUPPORT
**   -> address family of 'to' not supported
**  OPNOTSUPP
**   -> operation not supported for this socket.
**  ECONNRESET
**   -> packet could not be transmitted
**  EINPROGRESS
**   -> a concurrent operation is already underway (non-blocking socket)
**  EWOULDBLOCK
**   -> the socket is non-blocking: the asynchronous operation was started
*/
extern int sendto(
    SOCKET sockfd,
    const ncs_char *buf,
    ncs_int buflen,
    ncs_uint flags,
    const struct sockaddr *to,
    ncs_int tolen);
 
/* count <- recvfrom(sockfd, buf, buflen, flags, from, fromlen)
**
** receive a buffer from the DATAGRAM socket, returning the number of
** bytes transmitted and passing the address of the sending socket in the
** output parameters.
**
** possible errno values when count < 0 is returned:
**  ENOTSOCK
**   -> invalid sockfd (not an allocated socket)
**  EINVAL
**   -> invalid buffer length passed (< 0)
**  ECONNRESET
**   -> no packet could be received (socket is being shut down or closed)
**  EINPROGRESS
**   -> a concurrent operation is already underway (non-blocking socket)
**  EWOULDBLOCK
**   -> the socket is non-blocking: the asynchronous operation was started
**  EALREADY
**   -> the socket is non-blocking: the same operation is already started
**      and has not returned so far
*/
extern int recvfrom(
    SOCKET sockfd,
    ncs_char *buf,
    ncs_int buflen,
    ncs_uint flags,
    const struct sockaddr *from,
    ncs_int *fromlen);
 
/* rc <- shutdown(sockfd, how)
**
** stop transmitting in one or both directions through the socket;
** one of the constants SHUT_RD, SHUT_WR or SHUTRDWR should be used
** for 'how' to specify which direction is to be shut down
**
** possible errno values when rc < 0 is returned:
**  ENOTSOCK
**   -> invalid sockfd (not an allocated socket)
**  EINVAL
**   -> invalid value passed for 'how'
**  EOPNOTSUPP
**   -> shutdown not supported for a listering (server) socket
**  ENOTCONN
**   -> socket is not connected to a remote endpoint
*/
 
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2
 
extern int shutdown(SOCKET sockfd, int how);
 
/* rc <- closesocket(sockfd)
** close the given socket
**
** ATTENTION:
** - to avoid a name clash with the file-related close() function of
**   the standard C-API, an explicit socket-related name is used!
** - the C-library function close() will not work with sockets
**
** possible errno values when sockfd < 0 is returned:
**  ENOTSOCK
**   -> invalid sockfd (not an allocated socket)
*/
extern int closesocket(
    SOCKET sockfd);
 
/* rc <- ioctlsocket(sockfd, flag, &value)
**
** modify a property of the socket
**
** NICOF sockets only support to set the "non-blocking" property of a
** socket, using the 'flag' FIONBIO, with:
**   - a value != 0 for placing the socket into non-blocking operation and
**   - the value == 0 for placing the socket into blocking operation.
**
** the non-blocking mode affects the following functions of the NICOF
** socket API:
**  - connect()
**  - accept()
**  - send()
**  - recv()
**
** possible errno values when rc < 0 is returned:
**  ENOTSOCK
**   -> invalid sockfd (not an allocated socket)
**  EINVAL
**   -> invalid flag passed (not FIONBIO)
*/
 
#define FIONBIO 0x70
 
extern int ioctlsocket(SOCKET sockfd, long flag, ncs_uint *value);
 
 
/*
** ****
** **** select() support for simultanously handling more than 1 socket
** ****
*/
 
/* make sure we work with out own size values */
#ifdef FD_SETSIZE
#undef FD_SETSIZE
#endif
#ifdef FD_BYTES
#undef FD_BYTES
#endif
 
/*
** FD_SETSIZE : number of sockets that can be handled
** this value is per Client-VM and also defines the max. count of sockets
** that can be allocated in a VM.
** (it must match the constant MAX_SOCKET_COUNT of the NICOF-Java-service impl.)
*/
#define FD_SETSIZE 64
 
/* size in bytes of the 'fd_set' structure */
#define FD_BYTES ((FD_SETSIZE + 7) / 8)
 
/* the data structure to identify the sockets watched or showing activity */
typedef struct _fd_set {
    ncs_uchar fd_bytes[FD_BYTES];
} fd_set;
 
/* internal used macros, not necessarely for public use */
#define __FDSET1(_pos) ((ncs_char)(0x80 >> (_pos)))
#define __FDSET0(_pos) ((ncs_uchar)~__FDSET1(_pos))
 
/* macro FD_ZERO : reset (empty) a 'fd_set' */
#define FD_ZERO(_set) \
  memset(_set, '\0', FD_BYTES)
 
/* macro FD_ISSET : query if a socket is in a 'fd_set' */
#define FD_ISSET(_fd,_set) \
  ((((ncs_uint)_fd) >= FD_SETSIZE) \
  ? 0 : ((fd_set*)_set)->fd_bytes[(_fd >> 3)] & __FDSET1(_fd & 0x07))
 
/* macro FD_SET : add a socket to a 'fd_set' */
#define FD_SET(_fd,_set) \
  { if (((ncs_uint)_fd) < FD_SETSIZE) { \
  ((fd_set*)_set)->fd_bytes[(_fd >> 3)] |= __FDSET1(_fd & 0x07); } }
 
/* macro FD_CLR : remove a socket from a 'fd_set' */
#define FD_CLR(_fd,_set) \
  { if (((ncs_uint)_fd) < FD_SETSIZE) { \
  ((fd_set*)_set)->fd_bytes[(_fd >> 3)] &= __FDSET0(_fd & 0x07); } }
 
/* structure to specify the timeout of a select() operation */
struct timeval {
    ncs_long tv_sec;  /* seconds */
    ncs_long tv_usec; /* microseconds */
};
 
/* count <- select(...)
**
** standard select() function allowing to watch a number of sockets
** for activity for specific operations, possibly ending to wait after
** a timeout has elapsed
**
** the operation to watch for a socket depends in which of the passed sets
** the socket is contained:
**  -> set 'rd_fds' : recv() and accept()
**  -> set 'wr_fds' : send() and connect()
** except for 'send()', the operations will be initiated if the socket is
** in the corresponding set and the operation is not already underway
** (waiting for a 'send()' to finish is only meaningful if it has already
** been started in non-blocking mode)
**
** all sets can be passed as NULL, meaning the corresponding operations
** need not to be watched for any socket
**
** the first parameter 'num_fds' must be the int-value of the largest
** socket fd in any passed set plus 1 (+1) !
**
** upon return, the passed sets will identify the sockets which have
** shown activity for one of the operations watched
**
** the last parameter defines the timout handling:
**  - passing NULL will let select() wait until at least one socket
**    shows activity
**  - passing a pointer to a {0,0} timeval struct will let select() simply
**    check if an activity occured and return immediately (poll)
**  - passing a pointer to a timeval with at least one non-zero component
**    will let select() wait up to the specified interval with a resolution
**    of 1/100 second (VM/370 resp. CMS restriction) and a minimum of 1/100
**    second
**  (if a timeval is passed, select() will not modify its content)
**
** returns:
**  - the number of sockets that had activity on return
**  - 0 if the timeout occured without any socket activity
**  - < -1 if an error occured
**
** when count < 0 is returned, errno will only indicate NICOFCLT errors,
** as socket-related errors will be reported when the socket-operation is
** invoked afterwards on the sockets having shown activity during select()
*/
extern int select(
    ncs_int num_fds, /* largest sockfd passed in any set, +1 (plus one)!! */
    fd_set *rd_fds,  /* sockets to watch for recv() & accept() operations */
    fd_set *wr_fds,  /* sockets to watch for send() & connect() operations */
    fd_set *ex_fds,  /* not used (out-of-band is unsupported) */
    const struct timeval *timeout /* timeout specification */
    );
 
/* count <- selectX(...)
**
** extended version of select(), having separate sets for input (sockets
** to watch) and output (sockets that have shown activity)
**
** unlike select(), selectX() ensures that the input sets are unchanged
** upon return, relieving from the necessity to rebuild the input sets
** before calling the function again.
**
** besides the separation of input and output sets, selectX() is identical to
** select() (select() is in fact implemented by selectX())
*/
extern int selectX(
    ncs_int num_fds,   /* largest sockfd passed in any input-set, +1 !! */
    fd_set *rd_fds_in, /* input set of sockets to watch for recv()/accept() */
    fd_set *wr_fds_in, /* input set of sockets to watch for send()/connept() */
    fd_set *ex_fds_in, /* not used (out-of-band is unsupported) */
    fd_set *rd_fds_out,/* output set of active sockets in 'rd_fds_in' */
    fd_set *wr_fds_out,/* output set of active sockets in 'wr_fds_in' */
    fd_set *ex_fds_out,/* irrelevant */
    const struct timeval *timeout /* timeout specification */
 );
 
 
/*
** additional service functionality
*/
 
/* return a string representation of 'errcode' (socket and NICOF APIs) */
#define nicofsocket_errmsg ncs_emsg
extern char * nicofsocket_errmsg(int errcode);
 
/* dump internal info about the socket to stdout */
extern void dumpSocket(int sockfd);
 
#endif /* #ifndef __NICOF_SOCKET_DEFS */

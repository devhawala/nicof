 
/*
** socket client tests
*/
 
#include <stdio.h>
#include <errno.h>
#include <string.h>
 
#if defined(__CMS__)
 
  /* CMS resp. VM/370R6 Sixpack 1.2 */
#include "nicofclt.h"
#include "socket.h"
 
#define close(sockfd) closesocket(sockfd)
 
#elif defined(_WIN32)
 
  /* Windows-System */
#include <winsock.h>
 
#define nicofsocket_errmsg(_any) "unimplemented(W32)"
#define __exit(rc) _exit(rc)
#define nicofclt_ascii2ebcdic(in,len,out) memcpy(out, in, len)
 
#else
 
  /* Unix-System */
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
 
#endif
 
#define SRV_LISTEN_ADDR "0.0.0.0"
#define SRV_LISTEN_PORT 7999
 
/* the sockets for closing them on shutdown */
static int zeSocket = -1;
static int zeClientSocket = -1;
 
static void sock_startup() {
 /*
 ** begin OS specific prologue
 */
#if defined(__CMS__)
  nicofclt_init();
#endif
 
#ifdef _WIN32
 {
  WSADATA wsaData;
  if (WSAStartup (MAKEWORD(1, 1), &wsaData) != 0) {
  fprintf (stderr, "WSAStartup(): unable to initialize Winsock\n");
  exit(EXIT_FAILURE);
  }
 }
#endif
 /*
 ** end OS specific prologue
 */
}
 
static void sock_shutdown(int rc) {
 /* close the sockets if opened, ignoring errors */
 if (zeClientSocket >= 0) { close(zeClientSocket); }
 if (zeSocket >= 0) { close(zeSocket); }
 
 /*
 ** begin OS specific epilogue
 */
#if defined(__CMS__)
  nicofclt_deinit();
#endif
 
#ifdef _WIN32
 WSACleanup();
#endif
 /*
 ** end OS specific epilogue
 */
 
 __exit(rc);
}
 
static void dumpAddr(int sockfd, char *n, struct sockaddr_in *zeAddr) {
  char *p = (char*)zeAddr;
  printf("[sockfd: %d] %s\n", sockfd, n);
  printf(" -> sockaddr_in: 0x %02x %02x %02x %02x %02x %02x %02x %02x\n",
    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
  printf("     .sin_family = %d\n", zeAddr->sin_family);
  printf("     .sin_port   = %d\n", zeAddr->sin_port);
  printf("     .sin_addr   = 0x%08X\n", zeAddr->sin_addr.s_addr);
}
 
/* global receive / send buffers */
static char recvData[2048];
static char sendData[2048];
 
int main(int argc, char *argv[]) {
 
  struct sockaddr_in zeAddr;
 
  int recvDataSize = sizeof(recvData); /* in value */
  int recvLen; /* out value */
 
  /* initialize socket subsystem */
  sock_startup();
 
  /* create the socket */
  zeSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (zeSocket < 0) {
    printf(
        "** socket() failed: errno = %d (%s)\n",
        errno, nicofsocket_errmsg(errno));
    sock_shutdown(36);
  }
 
  /* bind the socket */
  zeAddr.sin_family = AF_INET;
  zeAddr.sin_port = htons(SRV_LISTEN_PORT);
  zeAddr.sin_addr.s_addr = inet_addr(SRV_LISTEN_ADDR);
  dumpAddr(-1, "bind-address", &zeAddr);
 
  int retConn = bind(zeSocket, (struct sockaddr*)&zeAddr, sizeof(zeAddr));
  if (retConn < 0) {
    printf("** bind() failed: retval = %d, errno = %d (%s)\n",
      retConn, errno, nicofsocket_errmsg(errno));
    sock_shutdown(40);
  }
 
  /* start listening */
  if (listen(zeSocket, 2) < 0) {
    printf("** listen() failed, errno = %d (%s)\n",
      errno, nicofsocket_errmsg(errno));
    sock_shutdown(42);
  }
 
  /* accept and handle connections until a "terminate" msg comes in */
  int done = 0;
  while (!done) {
    struct sockaddr clientAddr;
    int clientAddrLen = sizeof(clientAddr);
    zeClientSocket = accept(zeSocket, &clientAddr, &clientAddrLen);
    if (zeClientSocket < 0) {
      printf("** accept() failed, clientSock = %d, errno= %d (%s)\n",
        zeClientSocket, errno, nicofsocket_errmsg(errno));
      sock_shutdown(44);
    }
 
    struct sockaddr_in other;
    int olen = sizeof(other);
    struct sockaddr_in me;
    int melen = sizeof(me);
 
    dumpAddr(zeClientSocket, "clientAddr", (struct sockaddr_in*)&clientAddr);
 
    if (getpeername(zeClientSocket, (struct sockaddr*)&other, &olen) < 0) {
      printf("** getpeername(zeClientSocket) -> errno = %d (%s)\n",
        errno, nicofsocket_errmsg(errno));
    } else {
      dumpAddr(zeClientSocket, "getpeername(zeClientSocket)", &other);
    }
 
    if (getsockname(zeClientSocket, (struct sockaddr*)&me, &melen) < 0) {
      printf("** getsockname(zeClientSocket) -> errno = %d (%s)\n",
        errno, nicofsocket_errmsg(errno));
    } else {
      dumpAddr(zeClientSocket, "getsockname(zeClientSocket)", &me);
    }
 
    /* receive data and send back the response, if not a "terminate" msg */
    recvLen = recv(zeClientSocket, recvData, recvDataSize, 0);
    while (recvLen > 0) {
      /* check for the "terminate" message */
      if (recvLen < 14) {
        int strEnd = (recvDataSize > recvLen)
             ? recvLen
             : recvDataSize - 1;
        nicofclt_ascii2ebcdic(recvData, recvLen, sendData);
        sendData[strEnd] = '\0';
        /* printf("... buffer (translated): '%s'\n", sendData); */
        if (strcmp(sendData, "**TERMINATE**") == 0) {
          /* command to shutdown this server */
          done = 1;
          break;
        }
      }
 
      /* normal response: send back same buffer length with
      ** all positions as the first character in the response */
      /*memset(sendData, recvData[0], recvLen);*/
   sendData[0] = recvData[0];           /* set only ...              */
   sendData[recvLen / 2] = recvData[0]; /* ... the positions ...     */
   sendData[recvLen - 1] = recvData[0]; /* ... checked by the client */
      if (send(zeClientSocket, sendData, recvLen, 0) < 0) {
        printf("** send() after recv() -> errno: %d (%s)\n",
           errno, nicofsocket_errmsg(errno));
        recvLen = -1;
      } else {
        recvLen = recv(zeClientSocket, recvData, recvDataSize, 0);
      }
    }
 
    /* close the socket (the client already has closed the other end) */
    if (close(zeClientSocket) < 0) {
       printf("** close() -> errno: %d (%s)\n",
           errno, nicofsocket_errmsg(errno));
       sock_shutdown(46);
    }
    zeClientSocket = -1;
  }
 
  /* done */
  sock_shutdown(0);
}

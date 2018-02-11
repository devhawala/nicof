 
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
 
#elif defined(_WIN32)
 
  /* Windows-System */
#include <winsock.h>
 
#else
 
  /* Unix-System */
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
 
#endif
 
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
 
static void dumpAddr(char *n, struct sockaddr_in *zeAddr) {
  char *p = (char*)zeAddr;
  printf("%s .. zeAddr: 0x %02x %02x %02x %02x %02x %02x %02x %02x\n",
    n, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
  printf("%s     .sin_family = %d\n", n, zeAddr->sin_family);
  printf("%s     .sin_port   = %d\n", n, zeAddr->sin_port);
  printf("%s     .sin_addr   = 0x%08X\n", n, zeAddr->sin_addr.s_addr);
}
 
int main(int argc, char *argv[]) {
  int isOk = 1;
  int rc = 0;
 
  sock_startup(); /* initialize socket subsystem */
 
  int zeSocket;
  struct sockaddr_in zeAddr;
 
  int zeClientSocket = -1;
 
  char recvData[512];
  int recvDataSize = sizeof(recvData); /* in value */
  int recvDataLen; /* out value */
 
  char *sendData = "Hi external client, your message arrived well on VM/370R6";
  int sendDataLen = strlen(sendData);
 
  /* create the socket */
  zeSocket = socket(AF_INET, SOCK_STREAM, 0);
  printf("create socket => socket == %d\n", zeSocket);
  if (zeSocket < 0) {
    printf(
        "** socket() failed: errno = %d (%s)\n",
        errno, nicofsocket_errmsg(errno));
    sock_shutdown(36);
  }
 
  zeAddr.sin_family = AF_INET;
  zeAddr.sin_port = htons(7777);
/*zeAddr.sin_addr.s_addr = inet_addr("127.0.0.1");*/
  zeAddr.sin_addr.s_addr = inet_addr("0.0.0.0");
  dumpAddr("bind-address", &zeAddr);
 
  int retConn = bind(zeSocket, (struct sockaddr*)&zeAddr, sizeof(zeAddr));
  if (retConn < 0) {
    printf("** bind() failed: retval = %d, errno = %d (%s)\n",
      retConn, errno, nicofsocket_errmsg(errno));
    isOk = 0;
    rc = 40;
  }
 
  struct sockaddr_in boundTo;
  int btLen = sizeof(boundTo);
  if (isOk) {
    if (getsockname(zeSocket, (struct sockaddr*)&boundTo, &btLen) < 0) {
      printf("** getsockname(zeSocket) -> errno = %d (%s)\n",
        errno, nicofsocket_errmsg(errno));
    } else {
      dumpAddr("getsockname(zeSocket)", &boundTo);
    }
  }
 
  if (isOk && listen(zeSocket, 2) < 0) {
    printf("** listen() failed, errno = %d (%s)\n",
      errno, nicofsocket_errmsg(errno));
    isOk = 0;
    rc = 42;
  }
 
  if (isOk) {
    struct sockaddr clientAddr;
    int clientAddrLen = sizeof(clientAddr);
    zeClientSocket = accept(zeSocket, &clientAddr, &clientAddrLen);
    if (zeClientSocket < 0) {
      printf("** accept() failed, clientSock = %d, errno= %d (%s)\n",
        zeClientSocket, errno, nicofsocket_errmsg(errno));
      isOk = 0;
      rc = 44;
    } else {
      struct sockaddr_in other;
      int olen = sizeof(other);
      struct sockaddr_in me;
      int melen = sizeof(me);
 
      dumpAddr("clientAddr", (struct sockaddr_in*)&clientAddr);
 
      if (getpeername(zeClientSocket, (struct sockaddr*)&other, &olen) < 0) {
        printf("** getpeername(zeClientSocket) -> errno = %d (%s)\n",
          errno, nicofsocket_errmsg(errno));
      } else {
        dumpAddr("getpeername(zeClientSocket)", &other);
      }
 
      if (getsockname(zeClientSocket, (struct sockaddr*)&me, &melen) < 0) {
        printf("** getsockname(zeClientSocket) -> errno = %d (%s)\n",
          errno, nicofsocket_errmsg(errno));
      } else {
        dumpAddr("getsockname(zeClientSocket)", &me);
      }
    }
  }
 
  if (isOk) {
    recvDataLen = recv(zeClientSocket, recvData, recvDataSize - 1, 0);
    if (recvDataLen < 0) {
      printf("** recv() failed: recvDataLen = %d, errno = %d (%s)\n",
        recvDataLen, errno, nicofsocket_errmsg(errno));
      isOk = 0;
      rc = 46;
    } else {
      recvData[recvDataLen] = '\0';
      nicofclt_ascii2ebcdic(recvData, recvDataLen, recvData);
      printf("received server message:\n%s\n\n", recvData);
    }
  }
 
  if (isOk) {
    char sendBuffer[128];
    int zeLen = strlen(sendData);
    nicofclt_ebcdic2ascii(sendData, zeLen, sendBuffer);
    if (send(zeClientSocket, sendBuffer, zeLen, 0) < 0) {
      printf("** send() failed: errno = %d, (%s)\n",
        errno, nicofsocket_errmsg(errno));
      isOk = 0;
      rc = 48;
    }
  }
 
  if (isOk) {
    if (shutdown(zeClientSocket, SHUT_WR) < 0) {
      printf("** shutdown(SHUT_WR) failed: errno = %d, (%s)\n",
        errno, nicofsocket_errmsg(errno));
      isOk = 0;
      rc = 50;
    } else {
      for(;;) {
        int res = recv(zeClientSocket, recvData, recvDataSize, 0);
        if (res <= 0) { break; }
      }
    }
 
  }
 
  /* close the sockets */
#if defined(__CMS__)
  if (zeClientSocket >= 0) { closesocket(zeClientSocket); }
  if (zeSocket >= 0) { closesocket(zeSocket); }
#else
  if (zeClientSocket >= 0) { close(zeClientSocket); }
  if (zeSocket >= 0) { close(zeSocket); }
#endif
 
  /* done */
  sock_shutdown(0);
}

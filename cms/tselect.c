 
/*
** CMS test program for selectX()
*/
 
#include <stdio.h>
#include <errno.h>
#include <string.h>
 
#include "nicofclt.h"
#include "socket.h"
 
#define SRV_LISTEN_ADDR "0.0.0.0"
#define SRV_LISTEN_PORT 7999
 
/* management for connected clients */
static int maxFd = 0;
static fd_set clientFdSet;   /* active client sockets */
static fd_set recvFdSet;
static fd_set sendFdSet;
static fd_set *clients = &clientFdSet;
static fd_set *recvSet = &recvFdSet;
static fd_set *sendSet = &sendFdSet;
 
static fd_set resRecvFdSet;
static fd_set resSendFdSet;
static fd_set *resRecvSet = &resRecvFdSet;
static fd_set *resSendSet = &resSendFdSet;
 
/* global receive / send buffers */
static char recvData[2048];
static char sendData[2048];
 
static void dumpAddr(int sockfd, char *n, struct sockaddr_in *zeAddr) {
  char *p = (char*)zeAddr;
  printf("[sockfd: %d] %s\n", sockfd, n);
  printf(" -> sockaddr_in: 0x %02x %02x %02x %02x %02x %02x %02x %02x\n",
    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
  printf("     .sin_family = %d\n", zeAddr->sin_family);
  printf("     .sin_port   = %d\n", zeAddr->sin_port);
  printf("     .sin_addr   = 0x%08X\n", zeAddr->sin_addr.s_addr);
}
 
static int terminate(char *what, int code) {
  printf("** %s failed, errno = %d (%s)\n",
      what, errno, nicofsocket_errmsg(errno));
  int i;
  for (i = 0; i < FD_SETSIZE; i++) {
    closesocket(i); /* ignore any errors */
  }
  nicofclt_deinit();
  __exit(code);
}
 
static void dumpSet(char *prefix, int maxFd, fd_set *set) {
  int i;
  int active = 0;
  for (i = 0; i < FD_SETSIZE; i++) {
    if (FD_ISSET(i, set)) { active++; }
  }
 
  printf("%s [maxFd=%d, active=%d]: ", prefix, maxFd, active);
  for (i = 0; i < FD_SETSIZE; i++) {
    if (FD_ISSET(i, set)) { printf(" %d", i); }
  }
  printf("\n");
}
 
int main() {
  /* initialize the infrastructure and our global data*/
  nicofclt_init();
  FD_ZERO(clients);
  FD_ZERO(recvSet);
  FD_ZERO(sendSet);
 
  int rc;
  uint nonblock_true = 1;
  struct sockaddr_in zeAddr;
 
  SOCKET srvSock;
  struct sockaddr_in srvAddr;
 
  struct sockaddr clientAddr;
  int clientAddrLen = sizeof(clientAddr);
 
  int recvDataSize = sizeof(recvData); /* in value for recv() */
  int recvLen;                         /* out value from recv() */
 
  struct timeval tv;
  int inactiveSecs = 0;
 
  int done = 0;
  int fd, i;
 
  /* create server socket and put it on our watch list  */
  srvSock = socket(AF_INET, SOCK_STREAM, 0);
  if (srvSock < 0) { terminate("socket() for srvSock", 32); }
  FD_SET(srvSock, recvSet);
  maxFd = srvSock + 1;
 
  /* bind server socket to our port and start listening */
  zeAddr.sin_family = AF_INET;
  zeAddr.sin_port = htons(SRV_LISTEN_PORT);
  zeAddr.sin_addr.s_addr = inet_addr(SRV_LISTEN_ADDR);
  rc = bind(srvSock, (struct sockaddr*)&zeAddr, sizeof(zeAddr));
  if (rc < 0) { terminate("bind() for srvSock", 34); }
  rc = listen(srvSock, 2);
  if (rc < 0) { terminate("listen() for srvSock", 35); }
 
  /* start accepting connections and respond to packets */
  rc = 0;
  while (rc >= 0 && !done) {
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    rc = selectX(
           maxFd,
           recvSet, sendSet, NULL,
           resRecvSet, resSendSet, NULL,
           &tv);
/*  printf("selectX -> rc = %d\n", rc);*/
    if (rc == 0) {
      inactiveSecs++;
      if ((inactiveSecs % 10) == 0) {
        printf("... more 10 secs of inactivity\n");
      }
    } else if (rc > 0) {
      inactiveSecs = 0;
      for (fd = 0; fd < maxFd; fd++) {
        if (FD_ISSET(fd, resRecvSet)) {
          if (fd == srvSock) {
            /* new connection accepted */
            clientAddrLen = sizeof(clientAddr);
            int newSock = accept(srvSock, &clientAddr, &clientAddrLen);
            if (newSock >= 0) {
              dumpAddr(newSock, "new connection from",
                       (struct sockaddr_in*)&clientAddr);
              FD_SET(newSock, clients);
              FD_SET(newSock, recvSet);
              if (newSock >= maxFd) { maxFd = newSock + 1; }
              dumpSet(".. clients", maxFd, clients);
              dumpSet(".. recvSet", maxFd, recvSet);
            } else {
              printf("** accept() after selectX() -> errno: %d (%s)\n",
                     errno, nicofsocket_errmsg(errno));
            }
          } else {
            /* new data received on socket fd */
            recvLen = recv(fd, recvData, recvDataSize, 0);
            if (recvLen < 0) {
              printf("** recv(fd %d) after selectX() -> errno: %d (%s)\n",
                     fd, errno, nicofsocket_errmsg(errno));
              printf("** => closing client socket %d\n", fd);
              closesocket(fd);
              FD_CLR(fd, clients);
              FD_CLR(fd, recvSet);
              FD_CLR(fd, sendSet);
              dumpSet(".. clients", maxFd, clients);
              dumpSet(".. recvSet", maxFd, recvSet);
            } else if (recvLen == 0) {
              printf("** recv(fd %d) after selectX() -> 0 bytes received!\n",
                     fd);
            } else {
              /*printf("... fd %d: received %d bytes, handling response\n",
                       fd, recvLen); */
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
              memset(sendData, recvData[0], recvLen);
              rc = send(fd, sendData, recvLen, 0);
              if (rc < 0) {
                printf("** send() after selectX()>recv() -> errno: %d (%s)\n",
                       errno, nicofsocket_errmsg(errno));
              }
            }
          }
        }
        /* -- not needed until send() is done non-blocking ...
        if (FD_ISSET(fd, resSendSet)) {
 
        }
        */
      }
    }
  }
 
  printf("## shutting down (rc = %d, done = %d)\n", rc, done);
  dumpSet(".. clients", maxFd, clients);
  dumpSet(".. recvSet", maxFd, recvSet);
  for (i = 0; i < FD_SETSIZE; i++) {
    if (FD_ISSET(i, clients) || FD_ISSET(i, recvSet)) { dumpSocket(i); }
  }
 
  /* deinitialize the infrastructure */
  for (i = 0; i < FD_SETSIZE; i++) {
    closesocket(i); /* ignore any errors */
  }
  nicofclt_deinit();
  return 0;
}

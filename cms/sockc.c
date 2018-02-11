 
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
 
int main(int argc, char *argv[]) {
  int isOk = 1;
 
  sock_startup(); /* initialize socket subsystem */
 
  int zeSocket;
  struct sockaddr_in zeAddr;
 
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
  zeAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
  {
    char *p = (char*)&zeAddr;
    printf(".. zeAddr: 0x %02x %02x %02x %02x %02x %02x %02x %02x\n",
      p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
  }
 
  int retConn = connect(zeSocket, (struct sockaddr*)&zeAddr, sizeof(zeAddr));
  if (retConn < 0) {
    printf("** connect() failed: retval = %d, errno = %d (%s)\n",
      retConn, errno, nicofsocket_errmsg(errno));
    isOk = 0;
  }
 
  if (isOk) {
    char sendBuffer[128];
    char *zeSendText = "Hello Server, this is VM/370R6, waht's up?";
    int zeLen = strlen(zeSendText);
    nicofclt_ebcdic2ascii(zeSendText, zeLen, sendBuffer);
    if (send(zeSocket, sendBuffer, zeLen, 0) < 0) {
      printf("** send() failed: errno = %d, (%s)\n",
        errno, nicofsocket_errmsg(errno));
      isOk = 0;
    }
  }
 
  if (isOk) {
    char recvBuffer[128];
    int recvLen = recv(zeSocket, recvBuffer, sizeof(recvBuffer) - 1, 0);
    if (recvLen < 0) {
      printf("** recv() failed: recvLen = %d, errno = %d (%s)\n",
        recvLen, errno, nicofsocket_errmsg(errno));
      isOk = 0;
    } else {
      recvBuffer[recvLen] = '\0';
      nicofclt_ascii2ebcdic(recvBuffer, recvLen, recvBuffer);
      printf("received server message:\n%s\n\n", recvBuffer);
    }
  }
 
  /* close the socket */
#if defined(__CMS__)
  closesocket(zeSocket);
#else
  close(zeSocket);
#endif
 
  /* done */
  sock_shutdown(0);
}

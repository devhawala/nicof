 
/* Sample UDP server */
 
#include <stdio.h>
#include <errno.h>
 
#if defined(__CMS__)
 
  /* CMS resp. VM/370R6 Sixpack 1.2 */
#include "nicofclt.h"
#include "socket.h"
 
#define bzero(b,l) memset(b, '\0', l)
 
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
#include <netinet/in.h>
 
#endif
 
static int sockfd = -1;
 
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
 /* close the socket if opened, ignoring errors */
 if (sockfd >= 0) { close(sockfd); }
 
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
 
int main(int argc, char**argv)
{
   int sockfd,n;
   struct sockaddr_in servaddr,cliaddr;
   socklen_t len;
   char mesg[1000];
 
   sock_startup();
 
   sockfd=socket(AF_INET,SOCK_DGRAM,0);
   if (sockfd < 0) {
     printf("** socket() failed: errno = %d (%s)\n",
      errno, nicofsocket_errmsg(errno));
   sock_shutdown(30);
   }
 
   bzero(&servaddr,sizeof(servaddr));
   servaddr.sin_family = AF_INET;
   servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
   servaddr.sin_port=htons(32000);
   int rc = bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));
   printf("bind() -> rc = %d\n", rc);
   if (rc < 0) {
     printf("** bind() failed: errno = %d (%s)\n",
      errno, nicofsocket_errmsg(errno));
  sock_shutdown(32);
 }
 
   for (;;)
   {
      len = sizeof(cliaddr);
      n = recvfrom(sockfd,mesg,1000,0,(struct sockaddr *)&cliaddr,&len);
   printf("recvfrom() -> rc = %d, addrlen = %d\n", n, len);
   if (n < 0) {
     printf("** recvfrom() failed: errno = %d (%s)\n",
          errno, nicofsocket_errmsg(errno));
     sock_shutdown(34);
   } else {
     dumpAddr(sockfd, "datagram from:", &cliaddr);
        printf("-------------------------------------------------------\n");
        mesg[n] = 0;
        printf("Received the following:\n");
        printf("%s",mesg);
        printf("-------------------------------------------------------\n");
   }
      n = sendto(sockfd,mesg,n,0,(struct sockaddr *)&cliaddr,sizeof(cliaddr));
   if (n < 0) {
     printf("** sendto() failed: errno = %d (%s)\n",
          errno, nicofsocket_errmsg(errno));
     sock_shutdown(36);
   }
   }
   sock_shutdown(0);
}

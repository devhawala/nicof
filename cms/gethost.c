 
/*
** gethost <hostname> | <ipv4-address> | -ME
** (CMS with NICOF only!)
*/
 
#include <stdio.h>
#include <errno.h>
#include <string.h>
 
/* CMS resp. VM/370R6 Sixpack 1.2 */
#include "nicofclt.h"
#include "socket.h"
 
extern int h_errno;
 
int main(int argc, char *argv[]) {
 
  nicofclt_init();
 
  if (argc < 2) {
    printf("Usage: %s <hostname> | -ME\n", argv[0]);
    return 0;
  }
 
  char *p = argv[1];
  ncs_ulong ipaddr = inet_addr(p);
  struct hostent *h = NULL;
 
  if (h_errno == EOK) {
    h = gethostbyaddr((ncs_char*)&ipaddr, 4, AF_INET);
  } else if (strlen(p) == 3
      && p[0] == '-'
      && (p[1] == 'M' || p[1] == 'm')
      && (p[2] == 'E' || p[2] == 'e')) {
    p = "0.0.0.0";
    h = gethostbyname(p);
  } else {
    h = gethostbyname(p);
  }
 
  if (!h) {
    printf("Name or address '%s' could not be resolved\n", argv[1]);
    printf("h_errno = %d (%s)\n", h_errno, nicofsocket_errmsg(h_errno));
    return 4;
  }
 
  printf("Name: %s\n", h->h_name);
 
  if (h->h_length != 4) {
    printf("No IPv4 addresses found\n");
  } else {
    printf("Addresses:\n");
    unsigned char *address;
 int i = 0;
    while(address = h->h_addr_list[i++]) {
      printf("      %d.%d.%d.%d\n",
     address[0], address[1], address[2], address[3]);
    }
  }
 
  if (h->h_aliases && h->h_aliases[0]) {
    printf("Aliases:\n");
    int a = 0;
    char *alias;
    while(alias = h->h_aliases[a++]) {
      printf("      %s\n", alias);
    }
  }
 
  nicofclt_deinit();
 
 return 0;
}

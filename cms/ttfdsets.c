#include <stdio.h>
#include "socket.h"
 
#define m(...) printf(__VA_ARGS__)
 
int main() {
  fd_set zeSet;
  fd_set *set = &zeSet;
  int i,j,k;
  int *w1 = (int*)&zeSet.fd_bytes[0];
  int *w2 = (int*)&zeSet.fd_bytes[4];
 
  m("-- checking FD_ZERO()\n");
  FD_ZERO(set);
  for (i = 0; i < FD_SETSIZE; i++) {
    if (FD_ISSET(i, set)) {
      m(" ** FD_ISSET(%d) after FD_ZERO()\n", i);
    }
  }
 
  m("-- checking setting single fd\n");
  for (i = 0; i < FD_SETSIZE; i++) {
    FD_ZERO(set);
    FD_SET(i, set);
    for (j = 0; j < FD_SETSIZE; j++) {
      if (j == i) {
        if (!FD_ISSET(j, set)) {
          printf(" ** fd %d is NOT set but should be!\n", i);
        }
      } else {
        if (FD_ISSET(j, set)) {
          printf(" ** fd %d IS set but should not be!\n", i);
        }
      }
    }
  }
 
  m("-- checking setting and unsetting 2 fd\n");
  for (i = 0; i < FD_SETSIZE; i++) {
    FD_ZERO(set);
    FD_SET(i, set);
    for (j = 0; j < FD_SETSIZE; j++) {
      FD_SET(j, set);
 /*   printf(" => [%02d,%02d] ~ 0x%08X%08X\n", i, j, *w1, *w2);*/
      for (k = 0; k < FD_SETSIZE; k++) {
        if (k == i || k == j) {
          if (!FD_ISSET(k, set)) {
            printf(" ** fd %d is NOT set but should be!\n", k);
          }
        } else {
          if (FD_ISSET(k, set)) {
            printf(" ** fd %d IS set but should not be!\n", k);
          }
        }
      }
      if (j != i) { FD_CLR(j, set); }
/*    printf(" => [%02d]    ~ 0x%08X%08X after FD_CLR(%d)\n", i, *w1, *w2, j);*/
      for (k = 0; k < FD_SETSIZE; k++) {
        if (k == i) {
          if (!FD_ISSET(k, set)) {
            printf(" ** fd %d is NOT set but should be (post-FD_CLR)!\n", k);
          }
        } else {
          if (FD_ISSET(k, set)) {
            printf(" ** fd %d IS set but should not be (post-FD_CLR)!\n", k);
          }
        }
      }
    }
  }
}

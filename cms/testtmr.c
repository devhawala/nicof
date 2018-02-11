 
#include <stdio.h>
 
#include "intrapi.h"
 
/* do some CPU intensive computations to consume time */
static void computeWaiting(int count) {
  double value = 9346353.23223;
  while(count > 0) {
    value = sqrt(value);
    count--;
    value *= 3;
  }
}
 
int main() {
    char buf[48];
    diagx00(buf, 48);
    buf[8] = '\0';
    buf[24] = '\0';
    printf("-> System name = '%s' , Userid = '%s'\n", buf, &buf[16]);
    __exit(0);
 
    int rc = CMScommand("CP SET TIMER REAL", CMS_FUNCTION);
 
    printf("-- CP SET TIMER REAL => RC = %d, begin waiting\n", rc);
    _full myecb = 0;
    set_timer(100, &myecb); /* 1 sec */
    wait_ecb(&myecb);
    printf("done waiting...\n");
 
    int i;
    printf("\n-- waiting 10 x 1/10 s\n");
    for (i = 0; i < 10; i++) {
      myecb = 0;
      set_timer(10,&myecb);
      wait_ecb(&myecb);
    }
    printf("done waiting 10 x 1/10 s\n");
 
    printf("\n-- start computeWaiting(100000) && start timer for 10 s\n");
    myecb = 0;
    _full myecb2 = 0;
    _full *ecblist[] = { ECBLIST_ELEM(myecb), ECBLIST_END(myecb2) };
    set_timer(1000, &myecb);
    CMScommand("CP Q TIME", CMS_FUNCTION);
    computeWaiting(100000);
/*  post_ecb(&myecb2);*/
    printf(".. done computeWaiting(), waiting for timeout\n");
    CMScommand("CP Q TIME", CMS_FUNCTION);
    /*wait_ecb(&myecb);*/
    wait_anyecb(ecblist);
    printf(".. done waiting for timeout\n");
    CMScommand("CP Q TIME", CMS_FUNCTION);
 
  /*
    printf("\n-- start computeWaiting(100000) && start timer for 10 s\n");
    myecb = 0;
    set_timer(1000, &myecb);
    CMScommand("CP Q TIME", CMS_FUNCTION);
    computeWaiting(100000);
    printf(".. done computeWaiting(), canceling timer\n");
    CMScommand("CP Q TIME", CMS_FUNCTION);
    reset_timer();
    printf(".. myecb = %d , waiting for timeout (silly idea!)\n");
    wait_ecb(&myecb);
    printf(".. done waiting for timeout\n");
    CMScommand("CP Q TIME", CMS_FUNCTION);
  */
}

 
#include <stdio.h>
 
#include "svc_tblk.h"
 
int main() {
 
  /*
  ** preparations
  */
  intrapi();
  nicofclt_init();
 
  /*
  ** initialize the TestBulks-Service
  */
  if (!testbulks_init()) {
    printf("** unable to initialize SVC_TBLK, aborting\n");
    return 4;
  }
  printf("++ service TestBulks initialized\n");
 
  /*
  ** get a text source stream
  */
  uint linesToGet = 129;
  BULKSTREAM stream = testbulks_getTextSourceStream(linesToGet);
  if (stream == NULL) {
    printf("** unable to access text source stream, aborting\n");
    return 8;
  }
  printf("++ text source stream created\n");
 
  /*
  ** read all lines
  */
  char lineBuffer[81];
  char *line;
  int lineNo = 0;
#if 1
  while(line = ngetline(lineBuffer, 81, stream)) {
    int l = strlen(line);
    if (line[l - 1] == '\n') {
      printf("** ngetline() => NEWLINE at string end !!!!\n");
      line[l - 1] = '\0';
    }
#else
  while(line = ngets(lineBuffer, 81, stream)) {
    int l = strlen(line);
    if (line[l - 1] == '\n') { line[l - 1] = '\0'; }
#endif
 
#if 0
    printf("[%03d]: %s\n", ++lineNo, line);
#else
    lineNo++;
#endif
    if (neof(stream)) { printf("** now at EOF\n"); }
  }
  if (neof(stream)) {
    printf(".. EOF confirmed\n");
  } else {
    printf(".. not at EOF, sorry\n");
  }
  if (linesToGet == lineNo) {
    printf(".. correct number of lines received\n");
  } else {
    printf(".. wrong number of lines read: expected: %d, read: %d\n",
      linesToGet, lineNo);
  }
  nclose(stream);
  printf("\n");
 
  /*
  ** open binary source stream
  */
#define LRECL 73
  int recsToGet = 33;
  stream = testbulks_getBinSourceStream(LRECL, recsToGet);
  if (stream == NULL) {
    printf("** unable to access text source stream, aborting\n");
    return 8;
  }
  printf("++ binary source stream created\n");
 
  char binBuf[LRECL + 1];
  binBuf[LRECL] = '\0';
  int recsFound = 0;
  int bytesRead = nread(binBuf, LRECL, false, stream);
  while(!neof(stream)) {
    if (bytesRead < LRECL) {
      printf("** nread() => bytesRead(%d) < LRECL(%d)\n", bytesRead, LRECL);
      break;
    }
    recsFound++;
#if 0
    printf("[%d]: %s\n", recsFound, binBuf);
#endif
    bytesRead = nread(binBuf, LRECL, false, stream);
  }
  if (recsFound == recsToGet) {
    printf(".. expected number of records(%d) received\n", recsFound);
  } else {
    printf("** recsFound(%d) != recsToGet(%d)\n", recsFound, recsToGet);
  }
  nclose(stream);
  printf("\n");
 
  /*
  ** test bulk text sink
  */
  int linesToPut = 143;
  stream = testbulks_getTextSinkStream(linesToPut);
  if (stream == NULL) {
    printf("** unable to access text sink stream, aborting\n");
    return 12;
  }
  printf("++ text sink stream created\n");
 
  int linesPut = 0;
  while(nputline("--11--22--33--44--55--66--77--88--99--00--", stream) > 0) {
    linesPut++;
  }
  if (linesPut == linesToPut) {
    printf(".. correct number of lines (%d) written\n", linesPut);
  } else {
    printf(".. wrong number of lines written: expected: %d, written: %d\n",
      linesToPut, linesPut);
  }
  nclose(stream);
  printf("\n");
 
  /*
  ** test bulk bin sink
  */
  uint recsToPut = 143;
  uint lrecl = 33;
  stream = testbulks_getBinSinkStream(lrecl, recsToPut);
  if (stream == NULL) {
    printf("** unable to access bin sink stream, aborting\n");
    return 16;
  }
  printf("++ bin sink stream created\n");
 
#define _0 "\x30"
#define _1 "\x31"
 
#define _000 _0 _0 _0
#define _111 _1 _1 _1
 
#define s33_0 _000 _000 _000 _000 _000 _000 _000 _000 _000 _000 _000
#define s33_1 _111 _111 _111 _111 _111 _111 _111 _111 _111 _111 _111
 
  char *records[2] = { s33_0 , s33_1 };
  int zeRec = 0;
 
  int recsPut = 0;
  while(nwrite(records[zeRec++], lrecl, stream) > 0) {
    recsPut++;
    zeRec = zeRec % 2;
  }
  if (recsPut == recsToPut) {
    printf(".. correct number of records (%d) written\n", recsPut);
  } else {
    printf(".. wrong number of records written: expected: %d, written: %d\n",
      recsToPut, recsPut);
  }
  nclose(stream);
  printf("\n");
}

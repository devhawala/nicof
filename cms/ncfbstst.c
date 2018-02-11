#include <stdio.h>
 
#include "nicofclt.h"
#include "ncfbases.h"
#include "ncfio.h"
 
int main(int argc, char **argv) {
 
  /*
  ** preparations
  */
  intrapi();
  nicofclt_init();
 
  char *svcName = "DevNull";
  /*if (argc > 1) { svcName = argv[1]; }*/
 
  /*
  ** resolve a service-id with the API call
  */
  short svcId;
  int rc = ncfbasesvc_resolve(svcName, &svcId);
  printf("ncfbasesvc_resolve('%s') -> rc = %d, svcId = %d\n",
     svcName, rc, svcId);
 
  /*
  ** resolve a service-id with the explicit call (longer)
  */
  svcName = "Echo";
  int lSvcId;
  rc = ncfbasesvc_invoke_sync(
           0, /* svcId, */
           0, /* svcCmd, */
           0, /* inCtlWord, */
           svcName, /* inData, */
           strlen(svcName), /* inDataLen, */
           &lSvcId, /* outCtlWord, */
           NULL, /* outData, */
           NULL, /* outDataLen, */
           INDATA_TEXT /* dataFlags*/
           );
  printf("ncfbasesvc_invoke_sync('%s') -> rc = %d, lSvcId = %d\n",
     svcName, rc, lSvcId);
 
  /*
  ** use the bulk test service
  */
  svcName = "TestBulks";
  rc = ncfbasesvc_resolve(svcName, &svcId);
  printf("ncfbasesvc_resolve('%s') -> rc = %d, svcId = %d\n",
     svcName, rc, svcId);
  if (rc == 0) {
    /* get the text source stream (cmd 1) */
    uint streamId;
    rc = ncfbasesvc_invoke_sync(
             svcId, /* svcId, */
             1, /* svcCmd, */
             129, /* inCtlWord, */ /* number of lines to return */
             NULL, /* inData, */
             0, /* inDataLen, */
             &streamId, /* outCtlWord, */
             NULL, /* outData, */
             NULL, /* outDataLen, */
             DATA_BINARY /* dataFlags*/
             );
    printf("..open text stream => rc = %d, streamId = %d\n", rc, streamId);
    if (rc == NEW_BULK_SOURCE) {
 
      BULKSTREAM stream = ncfbid2s(streamId, true, true);
      char lineBuffer[81];
      char *line;
      int lineNo = 0;
      while(line = ngets(lineBuffer, 81, stream)) {
        int l = strlen(line);
        if (line[l - 1] == '\n') { line[l - 1] = '\0'; }
        printf("[%03d]: %s\n", lineNo++, line);
        if (neof(stream)) { printf("** now at EOF\n"); }
      }
      if (neof(stream)) {
        printf(".. EOF confirmed\n");
      } else {
        printf(".. not at EOF, sorry\n");
      }
      nclose(stream);
 
#if 0
      /* get the first data block */
      char buffer[2049];
      uint readCount;
      uint streamState = 0;
      rc = 0;
      while (rc == 0 && streamState == 0) {
        readCount = 0;
        rc = ncfbasesvc_invoke_sync(
                 0, /* svcId = base services */
                 101, /* svcCmd = BULKSRC_READ */
                 streamId, /* inCtlWord = stream id */
                 NULL, /* inData, irrelevant for source streams */
                 0, /* inDataLen, irrelevant for source streams */
                 &streamState, /* putCtlWord = state of the stream after read */
                 buffer, /* outData = our read buffer */
                 &readCount, /* outDataLen = written space in our read buffer */
                 OUTDATA_TEXT /* dataFlags = do ASCII->EBCDIC translation */
                 );
        buffer[readCount] = '\0';
        printf("..read block => rc = %d, streamState = %d, readCount = %d\n",
               rc, streamState, readCount);
 
        int lineNo = 0;
        int i;
        char line[257];
        memset(line, '\0', sizeof(line));
        char *l = line;
        for (i = 0; i < readCount; i++) {
          unsigned char c = buffer[i];
          if (c >= ' ' && c < 255) {
            *l++ = c;
          } else {
            *l = '\0';
            if (strlen(line) > 0) { printf("line[%d]: %s\n", lineNo, line); }
            l = line;
            lineNo++;
            printf("non-print-char: 0x%02x\n", c);
          }
        }
        *l = '\0';
        if (strlen(line) > 0) { printf("line[%d]: %s\n", lineNo, line); }
      }
#endif
    }
  }
 
 
  /*
  ** close everything
  */
  nicofclt_deinit();
}

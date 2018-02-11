 
#include "svc_tblk.h"
#include "ncfbases.h"
 
static const char *svcName = "TestBulks";
 
static short svcId;
static bool isInitialized = false;
 
bool tstb_001() {
  if (isInitialized) { return true; }
  int rc = ncfbasesvc_resolve(svcName, &svcId);
  if (rc != 0) {
    printf("tstb_001 :: ncfbasesvc_resolve('%s') -> rc = %d, svcId = %d\n",
       svcName, rc, svcId);
    return false;
  }
  isInitialized = true;
  return true;
}
 
BULKSTREAM tstb_002(uint linesToEof) {
  uint streamId;
  int rc = ncfbasesvc_invoke_sync(
             svcId,      /* svcId */
             1,          /* svcCmd     ==> create text source */
             linesToEof, /* inCtlWord, ==> number of lines to return */
             NULL,       /* inData, */
             0,          /* inDataLen, */
             &streamId,  /* outCtlWord, */
             NULL,       /* outData, */
             NULL,       /* outDataLen, */
             DATA_BINARY /* dataFlags*/
             );
  if (rc == NEW_BULK_SOURCE) {
    BULKSTREAM stream = ncfbid2s(streamId, true, true);
    return stream;
  } else {
    printf("tstb_002 :: open text source stream => rc = %d\n", rc);
  }
  return NULL;
}
 
BULKSTREAM tstb_003(byte lrecl, uint recs) {
  int ctlWord = ((recs & 0xFFFFFF) << 8) | lrecl;
  uint streamId;
  int rc = ncfbasesvc_invoke_sync(
             svcId,      /* svcId */
             2,          /* svcCmd     ==> create bin source */
             ctlWord,    /* inCtlWord, ==> records / lrecl */
             NULL,       /* inData, */
             0,          /* inDataLen, */
             &streamId,  /* outCtlWord, */
             NULL,       /* outData, */
             NULL,       /* outDataLen, */
             DATA_BINARY /* dataFlags*/
             );
  if (rc == NEW_BULK_SOURCE) {
    BULKSTREAM stream = ncfbid2s(streamId, true, false);
    return stream;
  } else {
    printf("tstb_003 :: open bin source stream => rc = %d\n", rc);
  }
  return NULL;
}
 
BULKSTREAM tstb_004(uint linesToFull) {
  uint streamId;
  int rc = ncfbasesvc_invoke_sync(
             svcId,      /* svcId */
             3,          /* svcCmd     ==> create text sink */
             linesToFull,/* inCtlWord, ==> number of lines to swallow */
             NULL,       /* inData, */
             0,          /* inDataLen, */
             &streamId,  /* outCtlWord, */
             NULL,       /* outData, */
             NULL,       /* outDataLen, */
             DATA_BINARY /* dataFlags*/
             );
  if (rc == NEW_BULK_SINK) {
    BULKSTREAM stream = ncfbid2s(streamId, false, true);
    return stream;
  } else {
    printf("tstb_004 :: open text sink stream => rc = %d\n", rc);
  }
  return NULL;
}
 
BULKSTREAM tstb_005(uint lrecl, uint recsToAccept) {
  uint streamId;
  uint ctlWord = (recsToAccept << 8) | (lrecl & 0xFF);
  int rc = ncfbasesvc_invoke_sync(
             svcId,      /* svcId */
             4,          /* svcCmd     ==> create bin sink */
             ctlWord,    /* inCtlWord, ==> number of lines to swallow */
             NULL,       /* inData, */
             0,          /* inDataLen, */
             &streamId,  /* outCtlWord, */
             NULL,       /* outData, */
             NULL,       /* outDataLen, */
             DATA_BINARY /* dataFlags*/
             );
  if (rc == NEW_BULK_SINK) {
    BULKSTREAM stream = ncfbid2s(streamId, false, false);
    return stream;
  } else {
    printf("tstb_005 :: open bin sink stream => rc = %d\n", rc);
  }
  return NULL;
}

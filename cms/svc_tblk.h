#ifndef SVC_TBLK_IMPORTED
#define SVC_TBLK_IMPORTED
 
#include "nicofclt.h"
#include "ncfio.h"
 
#define testbulks_init() \
  tstb_001()
extern bool tstb_001();
 
#define testbulks_getTextSourceStream(linesToEof) \
  tstb_002(linesToEof)
extern BULKSTREAM tstb_002(uint linesToEof);
 
#define testbulks_getBinSourceStream(lrecl, recs) \
  tstb_003(lrecl, recs)
extern BULKSTREAM tstb_003(byte lrecl, uint recs);
 
#define testbulks_getTextSinkStream(linesToFull) \
  tstb_004(linesToFull)
extern BULKSTREAM tstb_004(uint linesToFull);
 
#define testbulks_getBinSinkStream(lrecl, recs) \
  tstb_005(lrecl, recs)
extern BULKSTREAM tstb_005(uint lrecl, uint recs);
 
#endif

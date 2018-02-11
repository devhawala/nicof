/*
** IOPROXY.C   - NICOF VM-side (inside) proxy to the external (outside) proxy
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module implements the data shuffling inside proxy in a VM/370 virtual
** machine
**  -> receiving request packets from client VMs through VMCF and forwarding
**     them to the outside proxy
**  -> receiving response packets from the outside proxy and returning them
**     to the client VM which originally sent the corresponding request.
** This proxy waits for the outside proxy to connect to the device 097
** through a DIAL command. The device 097 must have been defined as a GRAF
** device before starting this program.
**
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2012,2014
** Released to the public domain.
*/
 
 
#include <stdio.h>
 
#include "intrapi.h"
 
/* slight optimization: avoid memset() */
#define CLR_VMCPARM(_p)
 
/* deactivated experimental variants for CLR_VMCPARM */
#define _1_CLR_VMCPARM(_p) memset(_p, '\0', sizeof(VMCPARM))
#define _2_CLS_VMCPARM(_p) { \
  long *z = (long*)_p; \
  *z++=0; *z++=0; *z++=0; *z++=0; *z++=0; *z++=0; *z++=0; *z++=0; }
 
/*
** minimal boolean data type, if not already defined
*/
#ifndef true
typedef char bool;
#define true 1
#define false 0
#endif
 
/*
** HAVE_LOG_RING
** if defined, then define LOG(message) to save the last LOG_RING_LEN messages
** in a message ring that can be written to the console with dumpLog()
** (else define dummies)
*/
/*#define dont_HAVE_LOG_RING 1*/
#define HAVE_LOG_RING 1
 
#ifdef HAVE_LOG_RING
 
#define LOG_RING_LEN 64
#define LOG_RING_NEXT(x) { x++; if (x >= LOG_RING_LEN) x = 0; }
 
static char *logRing[LOG_RING_LEN];
static volatile int logRingCurr;
 
static void initLog() {
  memset(logRing, '\0', sizeof(logRing));
  logRingCurr = 0;
}
 
static void dumpLog() {
  int curr = logRingCurr;
  int msgCount = 0;
  printf("-- begin last log entries\n");
  do {
    if (logRing[curr]) {
      printf("%s\n", logRing[curr]);
      msgCount++;
    }
    LOG_RING_NEXT(curr);
  } while(curr != logRingCurr);
  printf("-- end last log entries (count: %d)\n", msgCount);
}
 
#define LOG(m) { logRing[logRingCurr] = m; LOG_RING_NEXT(logRingCurr); }
 
#else
 
#define initLog()
#define dumpLog()
#define LOG(m)
 
#endif
 
/*
** HAVE_DEBUG_PRINTF
** if defined, the define PrintfXX-macros to directly write with printf
** else define dummies
*/
#define dont_HAVE_DEBUG_PRINTF 1
 
#ifdef HAVE_DEBUG_PRINTF
#define Printf0(p1) \
  printf(p1)
#define Printf1(p1,p2) \
  printf(p1,p2)
#define Printf2(p1,p2,p3) \
  printf(p1,p2,p3)
#define Printf3(p1,p2,p3,p4) \
  printf(p1,p2,p3,p4)
#define Printf4(p1,p2,p3,p4,p5) \
  printf(p1,p2,p3,p4,p5)
#define Printf5(p1,p2,p3,p4,p5,p6) \
  printf(p1,p2,p3,p4,p5,p6)
#define Printf16(p1,p2,p3,p4,p5,p6,p7,p8,p9,p10,p11,p12,p13,p14,p15,p16,p17) \
  printf(p1,p2,p3,p4,p5,p6,p7,p8,p9,p10,p11,p12,p13,p14,p15,p16,p17)
#define PrintfCsw2(intro,csw2) \
  printf("%s = 0x%08X ~ %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n", intro, \
    (csw2 & Unit_Attention) ? " Attention" : "", \
    (csw2 & Unit_Modifier) ? " StatusModifier" : "", \
    (csw2 & Unit_ControlUnitEnd) ? " ControlUnitEnd" : "", \
    (csw2 & Unit_Busy) ? " Busy" : "", \
    (csw2 & Unit_ChannelEnd) ? " ChannelEnd" : "", \
    (csw2 & Unit_DeviceEnd) ? " DeviceEnd" : "", \
    (csw2 & Unit_UnitCheck) ? " UnitCheck" : "", \
    (csw2 & Unit_UnitException) ? " UnitException" : "", \
    (csw2 & Channel_ProgCtrlIntr) ? " ProgCtrlIntr" : "", \
    (csw2 & Channel_IncorrectLen) ? " IncorrectLen" : "", \
    (csw2 & Channel_ProgramChk) ? " ProgramCheck" : "", \
    (csw2 & Channel_ProtectChk) ? " ProtectCheck" : "", \
    (csw2 & Channel_ChanDataChk) ? " ChanDataCheck" : "", \
    (csw2 & Channel_ChanCtrlChk) ? " ChanCtrlCheck" : "", \
    (csw2 & Channel_IntfCtrlChk) ? " IntfCtrlCheck" : "", \
    (csw2 & Channel_ChainingChk) ? " ChainingCheck" : "")
 
#else
 
#define Printf0(p1)
#define Printf1(p1,p2)
#define Printf2(p1,p2,p3)
#define Printf3(p1,p2,p3,p4)
#define Printf4(p1,p2,p3,p4,p5)
#define Printf5(p1,p2,p3,p4,p5,p6)
#define Printf16(p1,p2,p3,p4,p5,p6,p7,p8,p9,p10,p11,p12,p13,p14,p15,p16,p17)
#define PrintfCsw2(intro,csw2)
 
#endif
 
#define PRINTF(_LINE) printf("%s\n", _LINE)
 
/* _TRACE_HANDSHAKE_LEN
** if defined, then check the length of handshake token from the outside
** proxy to have max. 3 bytes length and write out message if not
*/
 
#define dont_TRACE_HANDSHAKE_LEN 1
 
#ifdef _TRACE_HANDSHAKE_LEN
#define CHK_HANDSHAKE_LEN \
{ if (recvLen > 6) { \
  printf(" !!!!! in-gone packet handshake longer than necessary: %d bytes\n",\
  recvLen); \
  printf("  start: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", \
    recvBuffer[0], recvBuffer[1], recvBuffer[2], \
    recvBuffer[3], recvBuffer[4], recvBuffer[5]); fflush(stdout); } }
#else
#define CHK_HANDSHAKE_LEN
#endif
 
/*
** the possible states of our state machine
*/
#define s_INITIAL 10
#define s_iWELCOME 11
#define s_iIDLE 20
#define s_IDLE 21
#define s_iTRANSMITPREP 30
#define s_TRANSMITPREP 31
#define s_iTRANSMITTING 32
#define s_TRANSMITTING 33
#define s_iRECEIVING 40
#define s_RECEIVING 41
#define s_iRESET 50
#define s_RESET 51
 
#define s_RECONNECT_CPREAD 60
#define s_iRECONNECT_CPREAD 61
#define s_RECONNECT_DIALED 70
#define s_iRECONNECT_DIALED 71
 
/* the current state in our state machine */
static volatile int pstate = s_INITIAL;
 
/* declaration for the (only) state transition initiated from VMCF handler */
static void enter_iTRANSMITPREP();
 
static void send_Dump();
 
/*
** request packets (data and meta-data) transitting through this proxy
*/
#define MAX_PACKET_LEN 2048    /* max. length of the data part of a packet */
#define MAX_REQUEST_COUNT 128  /* max. number of buffered packets  */
 
static totalReqCount = 0;  /* simple counter for VMCF-packets received so far */
 
/* structure to store a request from reception up to the response sent back */
typedef struct _request {
  int              slot;       /* position in 'requests'/'slots' arrays */
  _full            msgId;      /* VMCF message ID (specific to 'user') */
  char             user[8];    /* name of the VM which sent the request */
  _full            userWord1;  /* first user-word (high-word of VMCMUSE) */
  _full            userWord2;  /* seconrd user-word (low-word of VMCMUSE) */
  _full            inDataLen;  /* length of the data part of the VMCF request */
  char             inData[MAX_PACKET_LEN]; /* buffer for the data part */
} Request, *RequestPtr;
 
/** the request buffers **/
static Request    requests[MAX_REQUEST_COUNT];
static RequestPtr slots[MAX_REQUEST_COUNT];
 
/** the free requests **/
static RequestPtr reqFree[MAX_REQUEST_COUNT]; /* ring of free requests */
static volatile int reqCurrFree;              /* index of next to be used */
static volatile int reqLastFree;              /* index of last freed request */
 
/** ring of requests received but still waiting to be transmitted **/
static RequestPtr   reqQueue[MAX_REQUEST_COUNT]; /* ring of in-gone requests */
static volatile int reqLastIn;                 /* index of last in-gone */
static volatile int reqLastOut;                /* index of last sent */
 
/** ring-index counting **/
#define RING_NEXT(idx) ( (idx >= (MAX_REQUEST_COUNT-1)) ? 0 : idx+1 )
 
/* initialize the request data buffers
*/
static void initRequestBuffers() {
  int i;
 
  memset(requests, '\0', sizeof(requests));
  memset(reqQueue, '\0', sizeof(reqQueue));
  RequestPtr curr;
  for (i = 0, curr = requests; i < MAX_REQUEST_COUNT; i++, curr++) {
    curr->slot = i;
    slots[i] = curr;
    reqFree[i] = curr;
    reqQueue[i] = NULL;
  }
  reqLastIn = 0;
  reqLastOut = 0;
  reqCurrFree = 1;
  reqLastFree = 0;
 
  Printf1("## requests = 0x%08x\n", requests);
  Printf1("## sizeof(requests) = 0x%08x\n", sizeof(requests));
}
 
/* get a free request slot, returning the request buffer or NULL if the free
** pool is exhausted
*/
static RequestPtr getSlot() {
  Printf2("getSlot()-start: reqCurrFree = %d, reqLastFree = %d\n",
    reqCurrFree, reqLastFree);
  if (reqCurrFree == reqLastFree) {
   /* no free slot */
    printf("!! ## getSlot() no free slot: reqCurrFree = %d, reqLastFree = %d\n",
      reqCurrFree, reqLastFree);
    fflush(stdout);
    return NULL;
  }
  RequestPtr req = reqFree[reqCurrFree];
  if (req == NULL) {
   /* no free slot */
    printf("!! ## getSlot() req is NULL: reqCurrFree = %d, reqLastFree = %d\n",
      reqCurrFree, reqLastFree);
    fflush(stdout);
    return NULL;
  }
  reqFree[reqCurrFree] = NULL;
  reqCurrFree = RING_NEXT(reqCurrFree);
 
  return req;
}
 
/* return a processed request buffer to the free pool
*/
static void freeSlot(RequestPtr req) {
  req->msgId = 0;
/*memset(req->inData, '\0', MAX_PACKET_LEN);*/
  int thisFree = RING_NEXT(reqLastFree);
  reqFree[thisFree] = req;
  Printf2("freeSlot()-end: reqCurrFree = %d, reqLastFree = %d\n",
    reqCurrFree, thisFree);
  reqLastFree = thisFree;
}
 
 
/*
** VMCF interfacing, external interrupt handling and request queuing
*/
 
#define HDR_SMSG_LEN 169   /* special length to allow SMSG handling for VMCF */
 
#define EXT_STACKLEN 8192  /* C stack length for external interrupt handler */
 
static char vmcmhdr_data[HDR_SMSG_LEN + 9];  /* the VMCF interrupt data area */
static VMCMHDR_PTR vmcmhdr = NULL;           /* the VMCF interrupt data ptr */
static char vmcparm_data[sizeof(VMCPARM)*2 + 8]; /* the VMCF parm data area */
static VMCPARM_PTR vmcparm = NULL;           /* the normal VMCF parameter ptr */
static VMCPARM_PTR vmcreject = NULL;         /* the reject VMCF parameter ptr */
 
/* the following variables are for the event loop in the main() routine to
** respond to received SMSGs and to prevent the program to terminate, allowing
** this program to wait for interrupts.
*/
static _full evt_ecb = 0;    /* ECB for the event loop in main() */
static bool isDone = false;  /* terminate the event loop and program? */
static bool doRestart = false; /* set restart returncode ? */
static bool doStat = false;  /* dump the current statistics to the console? */
 
/* enable VMCF with SMSG messages
** (ensuring the memory blocks registered with VMCF are double-word aligned)
*/
static initVmcf() {
  vmcparm = (VMCPARM_PTR)(vmcparm_data + 8 - ((_full)vmcparm_data % 8));
  vmcreject = &vmcparm[1];
  vmcmhdr = (VMCMHDR_PTR)(vmcmhdr_data + 8 - ((_full)vmcmhdr_data % 8));
  CLR_VMCPARM(vmcparm);
  vmcparm->v1 = VMCPSMSG;
  vmcparm->vmcpfunc = VMCPAUTH;
  vmcparm->vmcpvada = vmcmhdr;
  vmcparm->vmcplena = HDR_SMSG_LEN;
  int rc = vmcf_request(vmcparm);
  Printf1("vmcf_request(VMCPAUTH) => rc = %d\n", rc);
}
 
/* disable VMCF
*/
static deinitVmcf() {
  if (vmcparm == NULL) { return; } /* not initialized */
  CLR_VMCPARM(vmcparm);
  vmcparm->vmcpfunc = VMCPUAUT;
  int rc = vmcf_request(vmcparm);
  Printf1("vmcf_request(VMCPUAUT) => rc = %d\n", rc);
}
 
/* send a REJECT response to a user / messageId
*/
static void sendVmcfReject(char *userid, _full msgId, _full reasonCode) {
  CLR_VMCPARM(vmcreject);
  vmcreject->vmcpfunc = VMCPRJCT;
  memcpy(vmcreject->vmcpuser.chars, userid, 8);
  vmcreject->vmcpmid = msgId;
  vmcreject->vmcpuse.words.w1 = reasonCode;
  int rc = vmcf_request(vmcreject);
}
 
/* send a REJECT to all requests received
** (after a reconnection of the external proxy to signal that all request
** have become invalid)
*/
static void resetAllRequests() {
  int i;
  Printf0("### resetting all current requests\n");
  for (i = 0; i < MAX_REQUEST_COUNT; i++) {
    RequestPtr slot = slots[i];
    if (slot->msgId != 0) {
      sendVmcfReject(slot->user, slot->msgId, 2);
    }
  }
  initRequestBuffers();
}
 
/* put the current VMCF-requests metadata into 'req' and enqueue 'req' for
** transmission to the outside proxy
*/
static void enqueueRequest(RequestPtr req) {
  /* save the current request data from the interrupt data */
  req->msgId = vmcmhdr->vmcmmid;
  memcpy(req->user, vmcmhdr->vmcmuser.chars, 8);
  req->inDataLen = vmcmhdr->vmcmlena;
  if (req->inDataLen > MAX_PACKET_LEN) { req->inDataLen = MAX_PACKET_LEN; }
  req->userWord1 = vmcmhdr->vmcmuse.words.w1;
  req->userWord2 = vmcmhdr->vmcmuse.words.w2;
 
  /* enqueue */
  Printf2("enqueueRequest: ...before: reqLastOut = %d, reqLastIn = %d\n",
    reqLastOut, reqLastIn);
  int idx = RING_NEXT(reqLastIn);
  reqQueue[idx] = req;
  reqLastIn = idx;
  Printf2("enqueueRequest: ...after : reqLastOut = %d, reqLastIn = %d\n",
    reqLastOut, reqLastIn);
}
 
/* read the data part of the request from VMCF
** (the metadata (userid, msgid, len, userwords) have already been put there
** by 'enqueueRequest()' at VMFC interrupt handling time, this routine is
** intended to be called by the 3270 device interrupt handler)
*/
static int readVmcfRequestIntoSlot(RequestPtr req) {
  Printf2("** readVmcf: msgId = %d, from: '%s'\n",
         vmcmhdr->vmcmmid, vmcmhdr->vmcmuser.chars);
 
  /* receive the request data */
  CLR_VMCPARM(vmcparm); /*memset(vmcparm, '\0', sizeof(VMCPARM));*/
  vmcparm->vmcpfunc = VMCPRECV;
  memcpy(vmcparm->vmcpuser.chars, req->user, 8);
  vmcparm->vmcpmid = req->msgId;
  vmcparm->vmcpvada = req->inData;
  vmcparm->vmcplena = MAX_PACKET_LEN;
/*Printf0("beginning vmcf_request(VMCPRECV)\n");*/
  int rc = vmcf_request(vmcparm);
  if (rc != 0) { printf("vmcf_request(VMCPRECV) => rc = %d\n", rc); }
 
  /* return the VMCF returncode: 0 = OK, others -> failed! */
  return rc;
}
 
/* check if there is (at least) one waiting request to be sent to the outside
** proxy
*/
/* static bool havingRequest() { return (reqLastOut != reqLastIn); } */
#define havingRequest() (reqLastOut != reqLastIn)
 
/* dequeue and return the next request to be sent to the outside proxy
*/
static RequestPtr getNextRequestToSend() {
  Printf2("getNextRequestToSend-start: reqLastOut = %d, reqLastIn = %d\n",
    reqLastOut, reqLastIn);
  if (reqLastOut == reqLastIn) { return NULL; }
  int idx = RING_NEXT(reqLastOut);
  RequestPtr req = reqQueue[idx];
  reqQueue[idx] = NULL;
  reqLastOut = idx;
  Printf2("getNextRequestToSend-end  : reqLastOut = %d, reqLastIn = %d\n",
    reqLastOut, reqLastIn);
  return req;
}
 
static int responseCount = 0; /* counter for VMCF responses sent so far */
 
/* sent the data passed as VMCF reply to the request in the specified slot
*/
static int sendVmcfReplyForSlot(
    RequestPtr req,
    _full userWord1,
    _full userWord2,
    _full responseDataLen,
    char *responseData) {
 
  responseCount++;
 
  /* construct and send the reply */
  CLR_VMCPARM(vmcparm); /*memset(vmcparm, '\0', sizeof(VMCPARM));*/
  vmcparm->vmcpfunc = VMCPREPL;
  memcpy(vmcparm->vmcpuser.chars, req->user, 8);
  vmcparm->vmcpmid = req->msgId;
  vmcparm->vmcpvada = responseData;
  vmcparm->vmcplena = responseDataLen;
  vmcparm->vmcpuse.words.w1 = userWord1;
  vmcparm->vmcpuse.words.w2 = userWord2;
  int rc = vmcf_request(vmcparm);
  if (rc != 0) {
    printf("vmcf_request(VMCPREPL) => rc = %d\n", rc);
    printf("  user: %s  msgid = %d\n", req->user, req->msgId);
  }
 
  /* return the VMCF returncode: 0 = OK, others -> failed! */
  return rc;
}
 
/* the handler routine for external interrupts registered with the INTRAPI
*/
static void handleExt(int *intrParams) {
/*_half *hParams = (_half*)intrParams;
  _half intrCode = hParams[49];*/
 
/*Printf0("################################ handleExt() in C-Code\n");
 
  Printf1("##  intrParams ptr = 0x%08X\n", intrParams);
  Printf2("##  PSW: 0x %08X %08X\n", intrParams[24], intrParams[25]);
  Printf1("##  interrupt code: 0x%04X (hParams[49])\n", intrCode);
  Printf0("##\n");
*/
 
  if (((_half*)intrParams)[49]/*intrCode*/== 0x4001) { /* VMCF */
  /*Printf1("##  -> VMCMFUNC = 0x%04X\n", vmcmhdr->vmcmfunc);
    Printf1("##  -> VMCMLENA = %d\n", vmcmhdr->vmcmlena);
  */
    if (vmcmhdr->vmcmfunc == VMCPSENX) {
      char *msg = &(((char*)vmcmhdr)[sizeof(VMCMHDR)]);
      msg[vmcmhdr->vmcmlena] = '\0';
    /*Printf("##  -> smsg = '%s'\n", msg);*/
      if (strcmp(msg, "END") == 0
          && memcmp(&vmcmhdr->vmcmuse, "MAINT   ", 8) == 0) {
        Printf0("## END message received, ending 'wait_ecb()'\n");
        isDone = true;
        post_ecb(&evt_ecb);
      } else if (strcmp(msg, "STAT") == 0) {
        doStat = true;
        send_Dump();
        post_ecb(&evt_ecb);
      }
    } else if (vmcmhdr->vmcmfunc == VMCPSENR) {
    /*if (memcmp(vmcmhdr->vmcmuser.chars, "CMSUSER ", 8)) {
        char cpcmd[80];
        memset(cpcmd, '\0', sizeof(cpcmd));
        strcpy(cpcmd,
          "MSGNOH CLIENT01 ## VMCF not from CMSUSER, but: 'xxxxxxxx'");
        memcpy(&cpcmd[48], vmcmhdr->vmcmuser.chars, 8);
        CPexecuteCmd(cpcmd);
      }*/
      RequestPtr req = getSlot();
      if (req == NULL) {
        /* no free slot found: reject request with: out-of-transmission-slots */
        sendVmcfReject(vmcmhdr->vmcmuser.chars, vmcmhdr->vmcmmid, 1);
        printf("***** out of slots: getSlot() -> NULL *****\n\n");
        /*post_ecb(&evt_ecb);*/
        return;
      }
      enqueueRequest(req);
      totalReqCount++;
      if (pstate == s_IDLE) { enter_iTRANSMITPREP(); }
      /*post_ecb(&evt_ecb);*/
    }
  }
 
/*Printf0("################################ handleExt() end\n");*/
}
 
 
/*
** SIO interfacing to DEFINEd 3270 device
*/
 
#define INT_STACKLEN 8192  /* C stack length for device interrupt handler */
#define GRAFDEV 0x0097     /* device number of the 3270 for the ext. proxy */
 
static bool usingBinaryTransfer = false; /* not using 7-to-8 encoding ? */
 
static bool inRecv = false; /* are we currently receiving data from GRAFDEV ? */
 
/* memory space where our CCWs reside, followed by the pointers to the
** CCW(-chains) for the handshake tokens inside this space, with the data
** content of the corresponding CCW, as far as it is constant
**
** Remark : the WCC-byte of the first CCW identifies the handshake command to
**          the outside proxy.
**
** Remark : all SBA orders must go to the last (12-bit) buffer position  (i.e.
**          the encoded position must be 7F7F) for the CCW to be recognized
**          as handshake from the VM/370-side proxy)
*/
static _full ccwSpace[48]; /* used to find a 8-byte aligned first CCW */
 
static CCW *ccw_handshake_welcome;
static char *data_handshake_welcome  = "\x40\x11\x7f\x7fHost-Welcome";
 
static CCW *ccw_handshake_welcomeb;
static char *data_handshake_welcomeb = "\x4d\x11\x7f\x7fHost-Welcome-BIN";
 
static CCW *ccw_handshake_willsend;
static char *data_handshake_willsend = "\xc1\x11\x7f\x7fHost-WillSend";
 
static CCW *ccw_handshake_ack;
static char *data_handshake_ack      = "\xc4\x11\x7f\x7fHost-Ack";
 
static CCW *ccw_handshake_dosend;
static char *data_handshake_dosend   = "\xc5\x11\x7f\x7fHost-DoSend";
 
static CCW *ccw_handshake_reset;
static char *data_handshake_reset    = "\x4f\x11\x7f\x7fHost-Reset";
 
static CCW *ccw_handshake_dump;
static char *data_handshake_dump     = "\x4e\x11\x7f\x7fProxy-Dump";
 
static CCW *ccw_reconnect_cpread;
static char *data_reconnect_cpread   = "\xc2\x11\x5b\x5f\x1d\xc1\x11\x5d\x6b"
                                       "\x1d-CP READ            ";
 
static CCW *ccw_reconnect_dialed;
static char *data_reconnect_dialed   = "\xc2\x11  DIALED TO me";
 
static CCW *ccw_xmit_packet_empty;
static CCW *ccw_xmit_packet;
static struct {
  char  wcc;
  char  sba[3];
  char  user[8];
  _full userWord1;
  _full userWord2;
  _half slot;
  } data_xmit_header;
#define XMIT_HEADER_LEN 22
 
/* the CCW and buffer to receive handshake and data from the DIALed ext. proxy*/
static CCW *ccw_recv_data;
static char recvBuffer[2560]; /* 7-to-8 encoded 2048 + overhead bytes */
 
/* some 3270 related constants */
#define WRITE      0x01
#define ERASEWRITE 0x05
#define SENSE      0x04
#define READMODIF  0x06
 
#define AID_ENTER ((char)0x7D)
#define AID_CLEAR ((char)0x6D)
#define AID_F1    ((char)0xF1)
#define AID_F2    ((char)0xF2)
#define AID_F3    ((char)0xF3)
#define AID_F4    ((char)0xF4)
#define AID_F5    ((char)0xF5)
#define AID_F9    ((char)0xF9)
 
/* get length of the string and replace §-chars by null-chars
*/
static int getPatchedLen(char *s) {
  int len = 0;
  while(*s) {
    len++;
    if (*s == '§') { *s = '\0'; }
    s++;
  }
  return len;
}
 
/* setup the CCWs and initialize the corresponding CCW-pointers
*/
static void init_ccws() {
  memset(ccwSpace, '\0', sizeof(ccwSpace));
  /* CCWs must be on a double-word boundary */
  CCW *firstCCW = (CCW*)&ccwSpace[0];
  if (((_full)firstCCW % 8) != 0) { firstCCW = (CCW*)&ccwSpace[1]; }
 
  ccw_handshake_welcome = firstCCW;
  CCW_Init(
    ccw_handshake_welcome[0],
    WRITE,
    data_handshake_welcome,
    CCWFlag_SILI,
    getPatchedLen(data_handshake_welcome));
 
  ccw_handshake_welcomeb = &ccw_handshake_welcome[1];
  CCW_Init(
    ccw_handshake_welcomeb[0],
    WRITE,
    data_handshake_welcomeb,
    CCWFlag_SILI,
    getPatchedLen(data_handshake_welcomeb));
 
  ccw_handshake_willsend = &ccw_handshake_welcomeb[1];
  CCW_Init(
    ccw_handshake_willsend[0],
    WRITE,
    data_handshake_willsend,
    CCWFlag_SILI,
    getPatchedLen(data_handshake_willsend));
 
  ccw_handshake_ack = &ccw_handshake_willsend[1];
  CCW_Init(
    ccw_handshake_ack[0],
    WRITE,
    data_handshake_ack,
    CCWFlag_SILI,
    getPatchedLen(data_handshake_ack));
 
  ccw_handshake_dosend = &ccw_handshake_ack[1];
  CCW_Init(
    ccw_handshake_dosend[0],
    WRITE,
    data_handshake_dosend,
    CCWFlag_SILI,
    getPatchedLen(data_handshake_dosend));
 
  ccw_handshake_reset = &ccw_handshake_dosend[1];
  CCW_Init(
    ccw_handshake_reset[0],
    WRITE,
    data_handshake_reset,
    CCWFlag_SILI,
    getPatchedLen(data_handshake_reset));
 
  ccw_handshake_dump = &ccw_handshake_reset[1];
  CCW_Init(
    ccw_handshake_dump[0],
    WRITE,
    data_handshake_dump,
    CCWFlag_SILI,
    getPatchedLen(data_handshake_dump));
 
  data_xmit_header.wcc = 0x00;
  data_xmit_header.sba[0] = 0x11;
  data_xmit_header.sba[1] = 0x7f;
  data_xmit_header.sba[2] = 0x7f;
 
  ccw_xmit_packet_empty = &ccw_handshake_dump[1];
  CCW_Init(
    ccw_xmit_packet_empty[0],
    ERASEWRITE,
    &data_xmit_header.wcc,
    CCWFlag_SILI,
    XMIT_HEADER_LEN);
 
  ccw_reconnect_cpread = &ccw_xmit_packet_empty[1];
  CCW_Init(
    ccw_reconnect_cpread[0],
    WRITE,
    data_reconnect_cpread,
    CCWFlag_SILI,
    getPatchedLen(data_reconnect_cpread));
 
  ccw_reconnect_dialed = &ccw_reconnect_cpread[1];
  CCW_Init(
    ccw_reconnect_dialed[0],
    WRITE,
    data_reconnect_dialed,
    CCWFlag_SILI,
    getPatchedLen(data_reconnect_dialed));
 
  ccw_xmit_packet = &ccw_reconnect_dialed[1];
  CCW_Init(
    ccw_xmit_packet[0],
    ERASEWRITE,
    &data_xmit_header.wcc,
    CCWFlag_CD | CCWFlag_SILI,
    XMIT_HEADER_LEN);
  CCW_Init(
    ccw_xmit_packet[1],
    ERASEWRITE,
    NULL, /* data pointer: will be filled when transmitting */
    CCWFlag_SILI,
    0);   /* data length : will be filled when transmitting */
 
  ccw_recv_data = &ccw_xmit_packet[2];
  CCW_Init(
    ccw_recv_data[0],
    READMODIF,
    recvBuffer,
    CCWFlag_SILI,
    sizeof(recvBuffer));
}
 
static char *lastSIO = "none"; /* the 'name' of the last CCW executed */
 
/* do a SIO for the given CCW(-chain)
*/
#if 0
static void doSIO(_full device, CCW *ccw, char *ccwName) {
  lastSIO = ccwName;
  _full rc = SIO(device, ccw);
  if (rc != 0) {
  /*Printf3("SIO(%03x, %s) => rc = %d, retrying...\n", device, ccwName, rc);*/
    rc = SIO(device, ccw);
    if (rc != 0) {
      Printf3("retry-SIO(%03x, %s) => rc = %d\n", device, ccwName, rc);
    }
  }
}
#else
#define doSIO(device, ccw, ccwName) {\
  int rc; lastSIO = ccwName; \
  if ((rc=SIO(device, ccw)) != 0 && (rc=SIO(device, ccw)) != 0) \
    Printf3("retry-SIO(%03x, %s) => rc = %d\n", device, ccwName, rc); \
}
#endif
 
/* send the CCW to let the outside proxy dump its state to the log
*/
static void send_Dump() {
  LOG("... send_Dump()");
  doSIO(GRAFDEV, ccw_handshake_dump, "ccw_handshake_dump");
}
 
/* transition to iWELCOME state and send the corresponding CCW
*/
/*static void enter_iWELCOME() {*/
#define enter_iWELCOME() { \
  LOG(" -> s_iWELCOME ==> ccw_handshake_welcome"); \
  pstate = s_iWELCOME; \
  doSIO(GRAFDEV, ccw_handshake_welcome, "ccw_handshake_welcome"); \
}
 
/* transition to iWELCOME state with binary and send the corresponding CCW
*/
/*static void enter_iWELCOMEBIN() {*/
#define enter_iWELCOMEBIN() { \
  LOG(" -> s_iWELCOMEBIN ==> ccw_handshake_welcomeb"); \
  pstate = s_iWELCOME; \
  doSIO(GRAFDEV, ccw_handshake_welcomeb, "ccw_handshake_welcomeb"); \
}
 
/* transition to iTRANSMITPREP state and send the corresponding CCW
*/
static void enter_iTRANSMITPREP() {
  LOG(" -> s_iTRANSMITPREP ==> ccw_handshake_willsend");
  pstate = s_iTRANSMITPREP;
  doSIO(GRAFDEV, ccw_handshake_willsend, "ccw_handshake_willsend");
}
 
/* transition to iRECEIVING state and send the corresponding CCW
*/
/*static void enter_iRECEIVING() {*/
#define enter_iRECEIVING() { \
  LOG(" -> s_iRECEIVING ==> ccw_handshake_dosend"); \
  pstate = s_iRECEIVING; \
  doSIO(GRAFDEV, ccw_handshake_dosend, "ccw_handshake_dosend"); \
}
 
/* transition to iIDLE state and send the corresponding CCW
*/
/*static void enter_iIDLE() {*/
#define enter_iIDLE() { \
  LOG(" -> s_iIDLE ==> ccw_handshake_ack"); \
  pstate = s_iIDLE; \
  doSIO(GRAFDEV, ccw_handshake_ack, "ccw_handshake_ack"); \
}
 
/* transition to iRESET state and send the corresponding CCW
*/
/*static void enter_iRESET() {*/
#define enter_iRESET() { \
  LOG(" -> s_iRESET ==> ccw_handshake_reset"); \
  pstate = s_iRESET; \
  doSIO(GRAFDEV, ccw_handshake_reset, "ccw_handshake_reset"); \
}
 
/* transition to iRECONNECT_CPREAD, sending "CP READ" state CCW
*/
static void enter_iRECONNECT_CPREAD() {
  LOG(" -> s_iRECONNECT_CPREAD ==> ccw_reconnect_cpread");
  pstate = s_iRECONNECT_CPREAD;
  doSIO(GRAFDEV, ccw_reconnect_cpread, "ccw_reconnect_cpread");
}
 
/* transition to INITIAL state, sending the "DIALED TO me" message
*/
static void enter_iRECONNECT_DIALED() {
  LOG(" -> s_iRECONNECT_DIALED = s_INITIAL ==> ccw_reconnect_dialed");
  pstate = s_INITIAL;
  doSIO(GRAFDEV, ccw_reconnect_dialed, "ccw_reconnect_dialed");
}
 
/* transition to iTRANSMITTING state and send the corresponding CCW with the
** data from the VMCF-request.
**
** WARNING: as the external interrupt handler (for VMCF) may be interrupted by
**          an internal interrupt (here for dev 097), reading the data content
**          of an already notified VMCF-request must be done inside the internal
**          interrupt handler (where this routine is called).
**          If a VMCF request was completely read in the external interrupt
**          handler (metadata and the data block), the VMCF RECEIVE call may
**          be active while an internal interrupt from device 097 is received,
**          which may result is a VMCF REPLY call, so 2 VMCF calls may be
**          active concurrently, which (sporadically) brings VMCF to transfer
**          data packets to the wrong recipient or buffer.
**          By doing the RECEIVEs from the internal interrupt handler, these
**          VMCF calls are automatically synchronized with the REPLYs, as only
**          single device interrupt is handled at a time.
*/
static void enter_iTRANSMITTING(RequestPtr req) {
  pstate = s_iTRANSMITTING;
 
  int rc = readVmcfRequestIntoSlot(req);
  if (rc != 0) {
    printf("enter_iTRANSMITTING: unable to receive VMCF packet (rc = %d)\n",
        rc);
    LOG("enter_iTRANSMITTING: unable to receive VMCF packet");
   }
 
  memcpy(data_xmit_header.user, req->user, 8);
  data_xmit_header.slot = (_half)req->slot;
  data_xmit_header.userWord1 = req->userWord1;
  data_xmit_header.userWord2 = req->userWord2;
 
  char *ccwName;
  CCW *ccw;
  if (req->inDataLen > 0) {
    LOG(" -> s_iTRANSMITTING ==> ccw_xmit_packet");
    ccw = ccw_xmit_packet;
    CCW_SetLen(ccw_xmit_packet[1], req->inDataLen);
    CCW_SetAddr(ccw_xmit_packet[1], req->inData);
    ccwName = "ccw_xmit_packet";
  } else {
    LOG(" -> s_iTRANSMITTING ==> ccw_xmit_packet_empty");
    ccw = ccw_xmit_packet_empty;
    ccwName = "ccw_xmit_packet_empty";
  }
 
  doSIO(GRAFDEV, ccw, ccwName);
}
 
/* initiate the data transfer from ext. proxy to this program after an ATTENTION
** interrupt from the DIALed 3270 device
*/
static void beginReceivePacket() {
  LOG(" ATTENTION ==> ccw_recv_data");
  inRecv = true;
/*memset(recvBuffer, '\0', sizeof(recvBuffer));*/
  CCW_SetLen(ccw_recv_data[0], sizeof(recvBuffer));
  doSIO(GRAFDEV, ccw_recv_data, "ccw_recv_data");
}
 
/* handle the data transferred from the outside proxy into our buffer,
** interpreting the AID as the handshake command from the outside proxy, doing
** the appropriate state transition and possibly sending the response data to
** the VM which sent the request to which the outside proxy responded
*/
static void endReceivePacket(_full csw2) {
  int restLen = csw2 & 0x0000FFFF;
  int recvLen = sizeof(recvBuffer) - restLen;
  bool keepReceivingAfterData = false;
 
  Printf2("     => endReceivePacket: aid = 0x%02x, recvLen = %d\n",
    recvBuffer[0], recvLen);
 
  inRecv = false;
 
  if (recvLen < 1) {           /* not even an AID code ? */
    LOG("*** endReceivePacket(): recvLen == 0 !!!!");
    printf("*** endReceivePacket(): recvLen == 0 !!!!\n");
    return;
  }
 
  if (recvBuffer[0] == AID_F5) {
    /* handshake E: want-send */
    LOG(" <<< handshake-E: want-send");
    CHK_HANDSHAKE_LEN;
    if (pstate == s_IDLE) {
      enter_iRECEIVING();
    } else if (pstate == s_TRANSMITPREP) {
      /* collision with our willsend-handshake, we have priority! */
      enter_iTRANSMITPREP();
    } else {
      enter_iRESET();
    }
    return;
  } else if (recvBuffer[0] == AID_F2 || recvBuffer[0] == AID_F9) {
    /* handshake E: welcome */
    usingBinaryTransfer = (recvBuffer[0] == AID_F9);
    if (usingBinaryTransfer) {
      LOG(" <<< handshake-E: welcome (for binary transfer)");
    } else {
      LOG(" <<< handshake-E: welcome (for 7-of-8 encoded transfer)");
    }
    CHK_HANDSHAKE_LEN;
    if (pstate == s_INITIAL) {
      if (usingBinaryTransfer) {
        enter_iWELCOMEBIN();
      } else {
        enter_iWELCOME();
      }
    } else {
      LOG("*** endReceivePacket(): unexpected welcome handshake, resyncing");
      enter_iRESET();
    }
    return;
  } else if (recvBuffer[0] == AID_F1) {
    /* handshake E: ack */
    LOG(" <<< handshake-E: ack");
    CHK_HANDSHAKE_LEN;
    if (pstate == s_TRANSMITPREP) {
      enter_iTRANSMITTING(getNextRequestToSend());
    } else if (pstate == s_TRANSMITTING || pstate == s_RESET) {
      if (havingRequest()) {
        enter_iTRANSMITPREP();
      } else if (recvLen > 3 && recvBuffer[3] == AID_F5) {
        /* 'want-send' immediately after 'ack' confirming our data packet */
        enter_iRECEIVING();
      } else {
        LOG(" -> s_iIDLE");
        pstate = s_IDLE;
      }
    }
    return;
  } else if (recvBuffer[0] == AID_F3) {
    /* handshake E: ack+want-send */
    LOG(" <<< handshake-E: ack + want-send");
    CHK_HANDSHAKE_LEN;
    if (pstate == s_TRANSMITTING || pstate == s_RESET) {
      enter_iRECEIVING();
    } else {
      enter_iRESET();
    }
    return;
  } else if (recvBuffer[0] == AID_CLEAR) {
    /* a different external proxy wants to connect */
    enter_iRECONNECT_CPREAD();
    return;
  } else if (recvBuffer[0] == AID_ENTER && pstate == s_iRECONNECT_CPREAD) {
    if (  recvBuffer[6]  == 'D'
       && recvBuffer[7]  == 'I'
       && recvBuffer[8]  == 'A'
       && recvBuffer[9]  == 'L'
       && recvBuffer[10] == ' ') {
      /* DIAL-command => reconnection of an external proxy */
 
      /*
      ** first the weird thing: we are a running inner proxy and a new(!) outer
      ** proxy has successfully connected. This means the old external proxy
      ** died and was replaced, so all pending requests are now invalid, as the
      ** old states in then previous proxy are gone with it, so we must drop
      ** all pending requests and reject them to signal the client of the
      ** loss..
      */
      resetAllRequests();
 
      /* now welcome the new proxy */
      enter_iRECONNECT_DIALED();
    } else {
      /* some other input ... */
      enter_iRECONNECT_CPREAD();
    }
    return;
  } else if (recvLen < 21) {
    printf("*** endReceivePacket(): response too short: %d\n", recvLen);
    /* not enough data for the packet response header */
    enter_iRESET();
    return;
  } else if (recvBuffer[0] == AID_F4) {
    /* handshake E: Data-Paket + want-send */
    if (pstate != s_RECEIVING && pstate != s_iRECEIVING) {
      PRINTF(
        " <<< handshake-E: DATA + want-send but not in state s_RECEIVING !!");
      enter_iRESET();
      return;
    }
    LOG(" <<< handshake-E: DATA + want-send");
    keepReceivingAfterData = true;
  } else if (recvBuffer[0] == AID_ENTER) {
    /* handshake E: Data-Packet */
    if (pstate != s_RECEIVING && pstate != s_iRECEIVING) {
      PRINTF(
        " <<< handshake-E: DATA  ## but not in state s_RECEIVING !!");
      enter_iRESET();
      return;
    }
    LOG(" <<< handshake-E: DATA");
    keepReceivingAfterData = false;
  } else {
    /* unknown handshake token / unexpected attention interrupt ... ?? */
    LOG(" <<< handshake-E: unexpected AID");
    printf("*** endReceivePacket(): unexpected AID 0x%02X\n", recvBuffer[0]);
    enter_iRESET();
    return;
  }
 
  /* here, we seem to have a response to a waiting slot */
  if (!usingBinaryTransfer) {
    /* first decode the data block from the 7-to-8 encoding */
    int dlen = recvLen - 11; /* aid + cursor-pos + vm-name are not encoded */
    if ((dlen % 8) != 0) {
      printf(
        "** 7-to-8 encoding problem recvLen = %d -> dlen = %d (not *8!)\n",
        recvLen, dlen);
    }
    int mask = 0x40;
    _byte *block = &recvBuffer[11]; /* skip the aid, cursor-ps and vm-name */
    _byte *dest = block;
    _byte blockModif = block[7];
    recvLen = 11 + (((dlen + 7) / 8) * 7);
    while(dlen > 0) {
      if (blockModif & mask) {
        *dest++ = *block++ | 0x80;
      } else {
        *dest++ = *block++;
      }
      mask >>= 1;
      if (!mask) {
        dlen -= 8;
        block++;
        blockModif = block[7];
        mask = 0x40;
      }
    }
  }
 
    /* process the data received */
  unsigned short recvDataLen = recvLen - 21;
/*  char  username[8];*/
  short slot;
  _full userWord1;
  _full userWord2;
  unsigned short xmitDataLen;
  char *src = &recvBuffer[11]; /*&recvBuffer[3];*/
                               /*memcpy(username, src, 8);*/
                               /*src += 8;*/
  slot = *((short*)src);       /*memcpy((char*)&slot, src, 2);*/
  src += 2;
  userWord1 = *((_full*)src);  /*memcpy((char*)&userWord1, src, 4);*/
  src += 4;
  userWord2 = *((_full*)src);  /*memcpy((char*)&userWord2, src, 4);*/
  src += 4;
  xmitDataLen = *((unsigned short*)src);/*memcpy((char*)&xmitDataLen,src,2);*/
  src += 2;
  if (slot < 0 || slot >= MAX_REQUEST_COUNT) {
    /* invalid slot */
    LOG("  !! invalid slot received from the outside proxy !!");
    enter_iRESET();
    return;
  }
  RequestPtr req = slots[slot];
  if (req == NULL || req->slot != slot) {
    /* unplausible slot number => slot not in use => ignore ! ! ! */
    if (keepReceivingAfterData) {
      enter_iRECEIVING();
    } else {
      enter_iIDLE();
    }
    return;
  }
  if (recvDataLen < xmitDataLen) {
    LOG("  !! recvDataLen < xmitDataLen received the outside proxy !!");
    printf("** recvDataLen = %d < xmitDataLen = %d\n",
      recvDataLen, xmitDataLen);
    printf("   csw2 = 0x%08x\n", csw2);
    printf("   slot: %d\n", slot);
    printf("   userWord1: 0x%08x\n", userWord1);
    printf("   userWord2: 0x%08x\n", userWord2);
    printf("   data: 0x%02x%02x%02x%02x%02x%02x%02x%02x...\n",
      src[0],src[1],src[2],src[3],src[4],src[5],src[6],src[7]);
    xmitDataLen = recvDataLen;
  }
  int rc = sendVmcfReplyForSlot(
              req,
              userWord1,
              userWord2,
              xmitDataLen,
              src);
  freeSlot(req);
  if (keepReceivingAfterData) {
    enter_iRECEIVING();
  } else {
    enter_iIDLE();
  }
}
 
static _full lastCsw2 = 0; /* the last channel status word handled */
 
/* the internal interrupt handler registered for the GRAF-device, which is a
** 3270 device DEFINEd for the outside proxy to DIAL to.
*/
static _full devintHandler(
    _full deviceAddress,
    _full oldPsw1,
    _full oldPsw2,
    _full csw1,
    _full csw2) {
 
/*Printf0("\n### dev097Handler:\n");
  Printf1("###   Device : %03x\n", deviceAddress);
  Printf2("###   CSW    : 0x = %08X %08X\n", csw1, csw2);
*/
  PrintfCsw2("\nint97", csw2);
  Printf1("    inRecv = %s\n", (inRecv) ? "true" : "false");
/*Printf2("###   OLD-PSW: 0x = %08X %08X\n", oldPsw1, oldPsw2);*/
 
  if (deviceAddress == GRAFDEV) {
    lastCsw2 = csw2;
    if (csw2 & Unit_Attention) {
    /*Printf0("... int-097 (Unit_Attention) -> beginReceivePacket()\n");*/
      beginReceivePacket();
    }
    if (csw2 & Unit_DeviceEnd) {
    /*Printf0("... int-097 (Unit_DeviceEnd)\n");*/
      if (inRecv) {
        endReceivePacket(csw2);
      } else if (pstate == s_iTRANSMITPREP) {
        LOG(" int97 -> s_TRANSMITPREP");
        pstate = s_TRANSMITPREP;
      } else if (pstate == s_iRECEIVING) {
        LOG(" int97 -> s_RECEIVING");
        pstate = s_RECEIVING;
      } else if (pstate == s_iTRANSMITTING) {
        LOG(" int97 -> s_TRANSMITTING");
        pstate = s_TRANSMITTING;
      } else if (pstate == s_iIDLE && havingRequest()) {
        LOG(" int97 -> s_TRANSMITPREP");
        enter_iTRANSMITPREP();
      } else if (pstate == s_iIDLE) {
        LOG(" int97 -> s_IDLE");
        pstate = s_IDLE;
      } else if (pstate == s_iRESET) {
        LOG(" int97 -> s_RESET");
        pstate = s_RESET;
      } else if (pstate == s_iWELCOME) {
        enter_iIDLE();
      }
    } else if (csw2 != Unit_Attention) {
      printf  ("\nint97 skipped csw2 ~ %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n\n",
        (csw2 & Unit_Attention) ? " Attention" : "",
        (csw2 & Unit_Modifier) ? " StatusModifier" : "",
        (csw2 & Unit_ControlUnitEnd) ? " ControlUnitEnd" : "",
        (csw2 & Unit_Busy) ? " Busy" : "",
        (csw2 & Unit_ChannelEnd) ? " ChannelEnd" : "",
        (csw2 & Unit_DeviceEnd) ? " DeviceEnd" : "",
        (csw2 & Unit_UnitCheck) ? " UnitCheck" : "",
        (csw2 & Unit_UnitException) ? " UnitException" : "",
        (csw2 & Channel_ProgCtrlIntr) ? " ProgCtrlIntr" : "",
        (csw2 & Channel_IncorrectLen) ? " IncorrectLen" : "",
        (csw2 & Channel_ProgramChk) ? " ProgramCheck" : "",
        (csw2 & Channel_ProtectChk) ? " ProtectCheck" : "",
        (csw2 & Channel_ChanDataChk) ? " ChanDataCheck" : "",
        (csw2 & Channel_ChanCtrlChk) ? " ChanCtrlCheck" : "",
        (csw2 & Channel_IntfCtrlChk) ? " IntfCtrlCheck" : "",
        (csw2 & Channel_ChainingChk) ? " ChainingCheck" : "");
      printf  ("   csw1 = 0x%08X csw2 = 0x%08X lastSIO: %s\n",
        csw1, csw2, lastSIO);
    }
/*  post_ecb(&evt_ecb);*/
  }
 
  return 0;
}
 
#define _STACK_CHECK 0
#define INT_STACKFILL ((char)0x99)
#define EXT_STACKFILL ((char)0x66)
 
/* entry point of the program:
** - initialize
** - register interrupt handlers for the 097 device and VMCF
** - start listening to VMCF requests
** - wait in a loop for our ECB to be posted by VMCF and process possible SMSG
**   commands (currently only: dump the statistics&log-ring) until
**   the proxy is requested by user MAINT to end itself
** - stop listering to VMCF
** - deregister interrupt handlers
** - write final statistics and leave program
*/
int main() {
 
  /* initialize data structures */
  initLog();
  init_ccws();
  initRequestBuffers();
 
  /* initialize interrupt handling */
  intrapi();
 
  void *intStack = (void*)malloc(INT_STACKLEN);
  set_devint_handler(&devintHandler, intStack, INT_STACKLEN);
 
  _byte enableFailed;
  enable_devint_handling(GRAFDEV, enableFailed);
  if (enableFailed) {
    printf("** error to enable interrupt handling for device %03X\n", GRAFDEV);
    return 4;
  }
 
  void *extStack = (void*)malloc(EXT_STACKLEN);
  enable_ext(&handleExt, extStack, EXT_STACKLEN);
 
#if _STACK_CHECK
  char *_stk = (char*)extStack;
  char *_stk_end = (char*)&_stk[EXT_STACKLEN];
  while(_stk < _stk_end) { *_stk++ = EXT_STACKFILL; }
 
  _stk = (char*)intStack;
  _stk_end = (char*)&_stk[INT_STACKLEN];
  while(_stk < _stk_end) { *_stk++ = INT_STACKFILL; }
#endif
 
  /* start listering to VMCF requests */
  initVmcf();
 
  /* wait for the END command and process other commands (only STAT for now) */
  wait_ecb(&evt_ecb);
  while(!isDone) {
    if (doStat) {
      doStat = false;
      printf("\nCurrent request status ::\n");
      printf("  reqs free :   reqCurrFree = %d, reqLastFree = %d\n",
        reqCurrFree, reqLastFree);
      printf("  reqs queue:   reqLastOut = %d, reqLastIn = %d\n",
        reqLastOut, reqLastIn);
      printf("Current transmission status ::\n");
      char *nstate = "UNKNOWN";
      if (pstate == s_INITIAL) { nstate = "INITIAL"; } else
      if (pstate == s_iWELCOME) { nstate = "WELCOME"; } else
      if (pstate == s_iIDLE) { nstate = "iIDLE"; } else
      if (pstate == s_IDLE) { nstate = "IDLE"; } else
      if (pstate == s_iTRANSMITPREP) { nstate = "iTRANSMITPREP"; } else
      if (pstate == s_TRANSMITPREP) { nstate = "TRANSMITPREP"; } else
      if (pstate == s_iTRANSMITTING) { nstate = "iTRANSMITTING"; } else
      if (pstate == s_TRANSMITTING) { nstate = "TRANSMITTING"; } else
      if (pstate == s_iRECEIVING) { nstate = "iRECEIVING"; } else
      if (pstate == s_RECEIVING) { nstate = "RECEIVING"; } else
      if (pstate == s_iRESET) { nstate = "iRESET"; } else
      if (pstate == s_RESET) { nstate = "RESET"; }
      printf("  pstate .........: %s\n", nstate);
      printf("  inRecv .........: %s\n", (inRecv) ? "true" : "false");
      printf("  binary transfer : %s\n",
             (usingBinaryTransfer) ? "true" : "false");
      printf("## Slot-Usage:\n");
      int slotIdx;
      for (slotIdx = 0; slotIdx < MAX_REQUEST_COUNT; slotIdx ++) {
        RequestPtr r = slots[slotIdx];
        if (r != NULL && r->msgId != 0) {
          printf("Slot[%d]: 0x%08x -- msgId = %d , uw1 = %d, uw2 = %d\n",
            slotIdx, r, r->msgId, r->userWord1, r->userWord2);
        }
      }
      printf("## End Slot-Usage\n");
      dumpLog();
    }
    evt_ecb = 0;
    wait_ecb(&evt_ecb);
  }
 
  /* stop listering to VMCF requests */
  deinitVmcf();
 
  /* disable´interrupt handling */
  disable_ext();
  free(extStack);
 
  short disableFailed;
  disable_devint_handling(GRAFDEV, disableFailed);
  if (disableFailed) {
    printf("** warning: disable interrupt handling failed for dev %03X\n",
           GRAFDEV);
  }
  free(intStack);
 
#if _STACK_CHECK
  _stk = (char*)extStack;
  _stk_end = (char*)&_stk[EXT_STACKLEN - 1];
  while(_stk_end >= _stk && *_stk_end == EXT_STACKFILL) { _stk_end--; }
  printf("## external stack usage: %d bytes\n", (_stk_end - _stk));
 
  _stk = (char*)intStack;
  _stk_end = (char*)&_stk[INT_STACKLEN - 1];
  while(_stk_end >= _stk && *_stk_end == INT_STACKFILL) { _stk_end--; }
  printf("## internal stack usage: %d bytes\n", (_stk_end - _stk));
#endif
 
  /* tell we're done and leave */
  printf ("##\n");
  printf ("### total successful requests processed: %d\n", totalReqCount);
  if (doRestart) {
    printf("##\n");
    printf("## restarting => returncode 4117\n");
    return 4117;
  }
  return 0;
}

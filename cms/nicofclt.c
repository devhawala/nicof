/*
** NICOFCLT.C  - NICOF client interface implementation
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module implements the communication from a client VM to a proxy VM
** running the NICOF through VMCF.
**
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2012, 2014
** Released to the public domain.
*/
 
 
#include <stdio.h>
 
#include "nicofclt.h"
 
#define HDR_SMSG_LEN 169
 
#define EXT_STACKLEN 8192
 
#define EXT_STACKCHECK 0
#define EXT_STACKFILL ((char)0x66)
 
static char vmcmhdr_data[HDR_SMSG_LEN + 1];  /* the VMCF-interrupt data area */
static VMCMHDR_PTR vmcmhdr = NULL;           /* the VMCF-interrupt data ptr */
static char vmcparm_data[sizeof(VMCPARM)+8]; /* the VMCF-parameter data area */
static VMCPARM_PTR vmcparm = NULL;           /* the VMCF-parameter ptr */
static SmsgHandler smsgHandler = NULL;       /* handler for SENDX-messages */
 
static _full rcv_ecb = 0; /* the ECB for waiting for VMCF responses */
 
#define MAX_PACKET_LEN 2048 /* must be in synch with the proxy equivalent */
 
typedef struct _client_request {
  struct _client_request *next;     /* next in queue / list */
  request_handle          me;       /* must be the _client_request ptr ! */
  char                    svcVm[8]; /* the VM-userid serviceing the request */
  uint                    msgId;    /* unique (in 32 bit range) */
  int                     recvRc;   /* rc when receiving the response async */
  uint                    filterTag;/* optional filter */
  uint                    userWord1;
  uint                    userWord2;
  uint                    dataLen;
  char                    data[MAX_PACKET_LEN];
  } NICOFCLT_REQ, *NICOFCLT_REQ_PTR;
 
static uint lastMsgId = 10;
 
static uint responseFilter = 0;
 
#define MID_FREE 2 /* in free list */
#define MID_NEW  4 /* owned by client but not sent to service-vm */
#define MID_RCVD 6 /* returned by service-vm, waiting for client */
#define MID_RTRN 8 /* owned by client after being received */
#define MID_PEND 10 /* if msgId > PEND: sent to service-vm, waiting */
 
static NICOFCLT_REQ_PTR requests = NULL; /* all requests */
 
#define RC_OK              0
#define VMCF        -1000000
#define SETREQDATA  -1001000
#define SENDREQ     -1002000
#define WAITRESP    -1003000
#define GETRESPDATA -1004000
#define FREEREQ     -1005000
#define RECVREQ     -1006000
#define RC(code,what) (what - code)
 
static void waitForVmcfResponse() {
  wait_ecb(&rcv_ecb);
  rcv_ecb = 0;
}
 
#define INIT_REQ_COUNT 4
 
static void *extStack;
 
static void handleExt(int *intrParams);
 
static NICOFCLT_REQ_PTR allocRequest() {
  NICOFCLT_REQ_PTR curr = (NICOFCLT_REQ_PTR)malloc(sizeof(NICOFCLT_REQ));
  curr->next = requests;
  curr->msgId = MID_FREE;
  curr->dataLen = 0;
  curr->me =(request_handle)curr;
  curr->next = requests;
  requests = curr;
  return curr;
}
 
static NICOFCLT_REQ_PTR getRequest() {
  NICOFCLT_REQ_PTR curr = requests;
  while (curr && curr->msgId != MID_FREE) { curr = curr->next; }
  if (!curr) { return allocRequest(); }
  curr->dataLen = 0;
  return curr;
}
 
 
/*
** int nicofclt_init()
** int nicofclt_initForSMSGs(smsg_handler)
*/
int ncf_001(SmsgHandler handler) {
  if (vmcparm != NULL) { return RC(0, VMCF); } /* already initialized */
 
  /* initialize interrupt handling from C */
  intrapi();
 
  /* save the smsg-handler */
  smsgHandler = handler;
 
  /* allocate some data buffers */
  int i;
  for (i = 0; i < INIT_REQ_COUNT; i++) {
    allocRequest();
  }
 
  /* enable external interrupt handling */
  extStack = (void*)malloc(EXT_STACKLEN);
  enable_ext(&handleExt, extStack, EXT_STACKLEN);
#if EXT_STACKCHECK
  char *_stk = (char*)extStack;
  char *_stk_end = (char*)&_stk[EXT_STACKLEN];
  while(_stk < _stk_end) { *_stk++ = EXT_STACKFILL; }
#endif
 
  /* enable VMCF for this VM */
  vmcparm = (VMCPARM_PTR)(vmcparm_data + 8 - ((_full)vmcparm_data % 8));
  vmcmhdr = (VMCMHDR_PTR)(vmcmhdr_data + 8 - ((_full)vmcmhdr_data % 8));
  memset(vmcparm, '\0', sizeof(VMCPARM));
  vmcparm->v1 = (smsgHandler) ? VMCPSMSG : 0;
  vmcparm->vmcpfunc = VMCPAUTH;
  vmcparm->vmcpvada = vmcmhdr;
  vmcparm->vmcplena = HDR_SMSG_LEN;
  int rc = vmcf_request(vmcparm);
/*printf("vmcf_request(VMCPAUTH) => rc = %d\n", rc);*/
  if (rc == 0) { return RC_OK; }
  return RC(rc, VMCF);
}
 
 
/*
** void nicofclt_deinit()
*/
void ncf_002() {
  if (vmcparm == NULL) { return; } /* not initialized */
 
#if EXT_STACKCHECK
  char *_stk = (char*)extStack;
  char *_stk_end = (char*)&_stk[EXT_STACKLEN - 1];
  while(_stk_end >= _stk && *_stk_end == EXT_STACKFILL) { _stk_end--; }
  printf("## external stack usage: %d bytes\n", (_stk_end - _stk));
#endif
 
  /* disable VMCF for this VM */
  memset(vmcparm, '\0', sizeof(VMCPARM));
  vmcparm->vmcpfunc = VMCPUAUT;
  int rc = vmcf_request(vmcparm);
/*printf("vmcf_request(VMCPUAUT) => rc = %d\n", rc);*/
 
  /* disable external interrupts */
  disable_ext();
 
  /* free all request buffers we know about */
  while (requests) {
    NICOFCLT_REQ_PTR curr = requests;
    requests = curr->next;
    curr->msgId = 0;
    curr->me = 0;
    free(curr);
  }
}
 
 
/*
** handle <- nicofclt_createRequest(userWord1, userWord2)
*/
request_handle ncf_010(uint userWord1, uint userWord2) {
  NICOFCLT_REQ_PTR curr = getRequest();
  curr->userWord1 = userWord1;
  curr->userWord2 = userWord2;
  curr->msgId = MID_NEW;
  return curr->me;
}
 
 
/*
** rc <- nicofclt_setRequestData(handle, length, data)
** rc <- nicofclt_setRequestDataXlate(handle, length, data, xtab)
*/
int ncf_011(request_handle h, uint length, const char *data,
            const unsigned char *xtab) {
  NICOFCLT_REQ_PTR req = (NICOFCLT_REQ_PTR)h;
  if (req->me != h) { return RC(1, SETREQDATA); }
  if (req->msgId != MID_NEW) { return RC(2, SETREQDATA); }
  if (length > MAX_PACKET_LEN) { length = MAX_PACKET_LEN; }
  if (length > 0) {
    if (xtab) {
      int i;
      unsigned char *s = (unsigned char*)data;
      unsigned char *d = (unsigned char*)req->data;
      for (i = 0; i < length; i++) {
        *d++ = xtab[*s++];
      }
    } else {
      memcpy(req->data, data, length);
 }
  }
  req->dataLen = length;
  return RC_OK;
}
 
/*
** rc <- nicofclt_setRequestDataX(handle, length1, data1, length2, data2)
** rc <- nicofclt_setRequestDataXlateX(handle, l1, d1, xtab1, l2, d2, xtab2)
*/
int ncf_012(request_handle h,
    uint length1, const char *data1, const unsigned char *xtab1,
    uint length2, const char *data2, const unsigned char *xtab2) {
  NICOFCLT_REQ_PTR req = (NICOFCLT_REQ_PTR)h;
  if (req->me != h) { return RC(1, SETREQDATA); }
  if (req->msgId != MID_NEW) { return RC(2, SETREQDATA); }
 
  if (length1 > MAX_PACKET_LEN) { length1 = MAX_PACKET_LEN; }
  if ((length1 + length2) > MAX_PACKET_LEN) {
    length2 = MAX_PACKET_LEN - length1;
  }
 
  unsigned char *d = (unsigned char*)req->data;
  if (length1 > 0) {
    if (xtab1) {
      int i;
      unsigned char *s = (unsigned char*)data1;
      for (i = 0; i < length1; i++) {
        *d++ = xtab1[*s++];
      }
    } else {
      memcpy(req->data, data1, length1);
 }
  }
  if (length2 > 0) {
    if (xtab2) {
      int i;
      unsigned char *s = (unsigned char*)data2;
      for (i = 0; i < length2; i++) {
        *d++ = xtab2[*s++];
      }
    } else {
      memcpy(&req->data[length1], data2, length2);
 }
  }
  req->dataLen = length1 + length2;
 
  return RC_OK;
}
 
static char const *DEFAULT_SVC_VM = "NICOFPXY";
 
/*
** rc <- nicofclt_sendRequest(handle)
** rc <- nicofclt_sendRequestAndWait(handle)
*/
int ncf_020(request_handle h, bool waitForResponse) {
  return ncf_021(h, waitForResponse, NULL);
}
 
/*
** rc <- nicofclt_sendRequestTo(handle, vm_name)
** rc <- nicofclt_sendRequestToAndWait(handle, vm_name)
*/
int ncf_021(request_handle h, bool waitForResponse, char const *vm) {
  NICOFCLT_REQ_PTR req = (NICOFCLT_REQ_PTR)h;
  if (req->me != h) { return RC(1, SENDREQ); }
  if (req->msgId != MID_NEW) { return RC(2, SENDREQ); }
  req->msgId = ++lastMsgId;
 
  char const *toVm = (vm) ? vm : DEFAULT_SVC_VM;
 
  /* prepare VMCF request */
  memset(vmcparm, '\0', sizeof(VMCPARM));
  vmcparm->vmcpfunc = VMCPSENR;
  SET_USER_FOR_CP(vmcparm->vmcpuser.chars, toVm);
  vmcparm->vmcpvada = req->data;
  vmcparm->vmcplena = req->dataLen;
  vmcparm->vmcpvadb = req->data;
  vmcparm->vmcplenb = MAX_PACKET_LEN;
  vmcparm->vmcpuse.words.w1 = req->userWord1;
  vmcparm->vmcpuse.words.w2 = req->userWord2;
  vmcparm->vmcpmid = req->msgId;
 
/*printf("-- sending request w/ msgId %d\n", req->msgId);*/
 
  /* do VMCF transmission */
  int rc = vmcf_request(vmcparm);
/*if (rc != 0) { printf("vmcf_request(VMCPSENR) => rc = %d\n", rc); }*/
  if (rc != 0) {
    /* if not sent: remove from pending queue, set state back to 'new' */
    req->msgId = MID_NEW;
    return RC(rc, VMCF); /* not sent, with encoded VMCF return code */
  }
 
  if (waitForResponse) { return nicofclt_waitForResponse(h); }
  return RC_OK;
}
 
/*
** rc <- nicofclt_waitForResponse(handle)
*/
int ncf_030(request_handle h) {
  NICOFCLT_REQ_PTR req = (NICOFCLT_REQ_PTR)h;
  if (req->me != h) { return RC(1, WAITRESP); }
  if (req->msgId == MID_RTRN) { return RC_OK; } /* already returned to client */
  if (req->msgId < MID_RCVD) { return RC(2, WAITRESP); } /* not sent */
 
/*printf("-- waiting for msgId: %d\n", req->msgId);*/
  while (req->msgId != MID_RCVD) {
    waitForVmcfResponse();
  }
/*printf("-- received msgId   : %d\n", req->msgId);*/
 
  req->msgId = MID_RTRN;
 
  return req->recvRc;
}
 
/*
** rc <- nicofclt_waitForAnyAvailable(&handle)
*/
int ncf_031(request_handle *handlePtr) {
  NICOFCLT_REQ_PTR reqPend = requests;
  while (reqPend &&
         !(reqPend->msgId == MID_RCVD || reqPend->msgId >= MID_PEND)) {
    reqPend = reqPend->next;
  }
  if (reqPend == NULL) {
    *handlePtr = NULL_REQUEST;
    return RC(3, WAITRESP); /* no pending request */
  }
 
  *handlePtr = NULL_REQUEST;
 
  while(true) {
    NICOFCLT_REQ_PTR req = requests;
    if (responseFilter) {
      while (req) {
        if (req->msgId == MID_RCVD && req->filterTag == responseFilter) {
    break;
  }
        req = req->next;
      }
    }
    if (req) {
     *handlePtr = req->me;
     return 0;
    }
    waitForVmcfResponse();
  }
}
 
/*
** {0,1} <- nicofclt_hasResponse(handle)
*/
int ncf_032(request_handle h) {
  NICOFCLT_REQ_PTR req = (NICOFCLT_REQ_PTR)h;
  if (req->me != h) { return 0; }
  if (req->msgId == MID_RCVD) { return 1; }
  return 0;
}
 
/*
** {0,1} <- nicofclt_isReceived(handle)
*/
int ncf_033(request_handle h) {
  NICOFCLT_REQ_PTR req = (NICOFCLT_REQ_PTR)h;
  if (req->me != h) { return 0; }
  if (req->msgId == MID_RTRN) { return 1; }
  return 0;
}
 
/*
** string <- nicofclt_getStateString(handle)
*/
static char stateBuffer[16];
char* ncf_034(request_handle h) {
  NICOFCLT_REQ_PTR req = (NICOFCLT_REQ_PTR)h;
  if (req->me != h) { return "invalid"; }
  if (req->msgId == MID_FREE) { return "FREE"; }
  if (req->msgId == MID_NEW) { return "NEW"; }
  if (req->msgId == MID_RCVD) { return "RCVD"; }
  if (req->msgId == MID_RTRN) { return "RTRN"; }
  if (req->msgId == MID_PEND) { return "PEND(?)"; }
  sprintf(stateBuffer, "PEND[%d]", req->msgId);
  return stateBuffer;
}
 
/*
** {} <- nicofclt_setFilterTag(handle, filterTag);
*/
void ncf_035(request_handle h, uint filterTag) {
  NICOFCLT_REQ_PTR req = (NICOFCLT_REQ_PTR)h;
  if (req->me != h) { return; }
  req->filterTag = filterTag;
}
 
/*
** tag <- nicofclt_getFilterTag(handle);
*/
uint ncf_036(request_handle h) {
  NICOFCLT_REQ_PTR req = (NICOFCLT_REQ_PTR)h;
  if (req->me != h) { return 0; }
  return req->filterTag;
}
 
/*
** rc <- nicofclt_waitForAnyAvailableX(&handle, filterTag, timeout)
** (timeout in 1/100 seconds => unit = 10ms)
*/
int ncf_037(request_handle *handlePtr, uint filterTag, uint timeout) {
  int rc;
 
  NICOFCLT_REQ_PTR reqPend = requests;
  while (reqPend &&
         !(reqPend->msgId == MID_RCVD || reqPend->msgId >= MID_PEND)) {
    reqPend = reqPend->next;
  }
  if (reqPend == NULL) {
    *handlePtr = NULL_REQUEST;
    return RC(3, WAITRESP); /* no pending request */
  }
 
  responseFilter = filterTag;
 
  if (timeout == NO_TIMEOUT) {
    rc = nicofclt_waitForAnyAvailable(handlePtr);
  } else {
    _full timer_ecb = 0;
    _full *ecblist[] = { ECBLIST_ELEM(rcv_ecb), ECBLIST_END(timer_ecb) };
 
    *handlePtr = NULL_REQUEST;
    rc = WAITANY_TIMEDOUT;
/*  printf("---- ncf_037(): setting timer to %d 1/100s\n", timeout);*/
    set_timer(timeout, &timer_ecb);
 
    while(timer_ecb == 0 && rc == WAITANY_TIMEDOUT) {
      NICOFCLT_REQ_PTR req = requests;
      while (req) {
        if (req->msgId == MID_RCVD
      && (!responseFilter || req->filterTag == responseFilter)) {
          rc =  0;
          *handlePtr = req->me;
          req = NULL;
        } else {
          req = req->next;
        }
      }
      if (rc == WAITANY_TIMEDOUT) {
/*      printf("---- ncf_037(): wait_anyecb(ecblist)\n");*/
        wait_anyecb(ecblist);
        rcv_ecb = 0;
      }
    }
  }
 
  /* reset filter and timer */
  responseFilter = 0;
  reset_timer();
 
  /* done */
  return rc;
}
 
/*
** rc <- nicofclt_getResponseUserWords(handle, *userWord1, *userWord2)
*/
int ncf_040(request_handle h, uint *userWord1, uint *userWord2) {
  NICOFCLT_REQ_PTR req = (NICOFCLT_REQ_PTR)h;
  if (req->me != h) { return RC(1, GETRESPDATA); }
  if (req->msgId == MID_RCVD) { ncf_030(h); } /* wait for response ! */
  if (req->msgId != MID_RTRN) { return RC(2, GETRESPDATA); } /* not returned */
 
  *userWord1 = req->userWord1;
  *userWord2 = req->userWord2;
 
  return RC_OK;
}
 
/*
** rc <- nicofclt_getResponseData(handle, buflen, buffer, *datalen)
** rc <- nicofclt_getResponseDataXlate(handle, buflen, buffer, *datalen, xtab)
*/
int ncf_041(request_handle h, uint bufferLen, char *buffer, uint *dataLen,
            const unsigned char *xtab, uint from) {
  NICOFCLT_REQ_PTR req = (NICOFCLT_REQ_PTR)h;
  if (req->me != h) { return RC(1, GETRESPDATA); }
  if (req->msgId == MID_RCVD) { ncf_030(h); } /* wait for response ! */
  if (req->msgId != MID_RTRN) { return RC(2, GETRESPDATA); } /* not returned */
 
  uint copyLen = req->dataLen;
  if (from >= copyLen) {
    *dataLen = 0;
    return 0;
  }
  copyLen -= from;
  if (copyLen > bufferLen) { copyLen = bufferLen; }
  if (copyLen > 0) {
    if (xtab) {
      int i;
      unsigned char *s = (unsigned char*)&req->data[from];
      unsigned char *d = (unsigned char*)buffer;
      for (i = 0; i < copyLen; i++) {
        *d++ = xtab[*s++];
      }
    } else {
      memcpy(buffer, &req->data[from], copyLen);
    }
  }
  *dataLen = copyLen;
 
  return 0;
}
 
/*
** rc <- nicofclt_getResponseDataLength(handle, *datalen)
*/
int ncf_042(request_handle h, uint *dataLen) {
  NICOFCLT_REQ_PTR req = (NICOFCLT_REQ_PTR)h;
  if (req->me != h) { return RC(1, GETRESPDATA); }
  if (req->msgId == MID_RCVD) { ncf_030(h); } /* wait for response ! */
  if (req->msgId != MID_RTRN) { return RC(2, GETRESPDATA); } /* not returned */
 
  uint dLen = req->dataLen;
  if (dLen > MAX_PACKET_LEN) { dLen = MAX_PACKET_LEN; }
  *dataLen = dLen;
 
  return 0;
}
 
/*
** rc <- nicofclt_getResponseDataByte(handle, idx, *byte)
*/
int ncf_043(request_handle h, uint idx, char *b) {
  *b = (char)0x00;
  NICOFCLT_REQ_PTR req = (NICOFCLT_REQ_PTR)h;
  if (req->me != h) { return RC(1, GETRESPDATA); }
  if (req->msgId == MID_RCVD) { ncf_030(h); } /* wait for response ! */
  if (req->msgId != MID_RTRN) { return RC(2, GETRESPDATA); } /* not returned */
 
  if (idx >= MAX_PACKET_LEN) { return RC(3, GETRESPDATA); }
  if (idx >= req->dataLen) { return RC(3, GETRESPDATA); }
  *b = req->data[idx];
 
  return 0;
}
 
/*
** rc <- nicofclt_freeRequest(handle)
*/
int ncf_050(request_handle h) {
  NICOFCLT_REQ_PTR req = (NICOFCLT_REQ_PTR)h;
  if (req->me != h) { return RC(1, FREEREQ); }
  if (req->msgId >= MID_PEND) {
    return RC(2, FREEREQ); /* sent but not received: cannot be free-ed ! */
  }
 
  req->msgId = MID_FREE;
 
  return 0;
}
 
static void handleExt(int *intrParams) {
  _half *hParams = (_half*)intrParams;
  _half intrCode = hParams[49];
 
  if (intrCode == 0x4001) { /* VMCF */
    if (vmcmhdr->vmcmfunc == VMCPSENR) { /* result to a SEND/RECV */
      _full mId = vmcmhdr->vmcmmid;
      NICOFCLT_REQ_PTR req = requests;
      while(req) {
        if (req->msgId == mId) { break; }
        req = req->next;
      }
      if (!req) { return; } /* request not found ... a response to what...? */
      if (vmcmhdr->v1 & VMCMRJCT) { /* request rejected */
        uint reason = vmcmhdr->vmcmuse.words.w1;
        if (reason == 1 || reason == 2) {
          req->recvRc = RC(reason, RECVREQ);
        } else {
          req->recvRc = RC(3, RECVREQ);
        }
      } else if (vmcmhdr->v1 & VMCMRESP) {        /* positive response */
        req->recvRc = RC_OK;
        req->dataLen = MAX_PACKET_LEN - vmcmhdr->vmcmlenb;
        req->userWord1 = vmcmhdr->vmcmuse.words.w1;
        req->userWord2 = vmcmhdr->vmcmuse.words.w2;
      } else {                             /* unknown result state */
        req->recvRc = RC(4, RECVREQ);
      }
      req->msgId = MID_RCVD;
      if (!responseFilter || req->filterTag == responseFilter) {
        post_ecb(&rcv_ecb);
      }
    } else if (vmcmhdr->vmcmfunc == VMCPSENX) { /* incoming SMSG */
      if (!smsgHandler) { return; }
      char *msg = &(((char*)vmcmhdr)[sizeof(VMCMHDR)]);
      msg[vmcmhdr->vmcmlena] = '\0';
      smsgHandler(vmcmhdr->vmcmuse, msg);
    } else {
      printf("** irrelevant VMCF-interrupt: vmcmfunc = %d, V1 = 0x%02X\n",
        vmcmhdr->vmcmfunc, vmcmhdr->v1);
      printf("**   vmcmlena  = %d\n", vmcmhdr->vmcmlena);
      printf("**   vmcmlenb  = %d\n", vmcmhdr->vmcmlenb);
      printf("**   userword1 = 0x%08X\n", vmcmhdr->vmcmuse.words.w1);
      printf("**   userword2 = 0x%08X\n", vmcmhdr->vmcmuse.words.w2);
    }
  }
}
 
#define Errmsg(CODE,MSG) case CODE : return MSG; break;
 
static char errmsgbuffer[32];
 
char* ncf_000(int rc) {
  switch(rc) {
 
    Errmsg(0, "OK, no error")
 
    Errmsg(RC(0, VMCF), "NICOFCLT already initialized")
    Errmsg(RC(1, VMCF), "VMCF(1) - invalid virtual buffer address or length")
    Errmsg(RC(2, VMCF), "VMCF(2) - invalid subfunction code")
    Errmsg(RC(3, VMCF), "VMCF(3) - protocol violation")
    Errmsg(RC(4, VMCF), "VMCF(4) - source virtual machine not authorized")
    Errmsg(RC(5, VMCF), "VMCF(5) - target virtual machine not available")
    Errmsg(RC(6, VMCF), "VMCF(6) - protection exception")
    Errmsg(RC(7, VMCF), "VMCF(7) - SENDX data too large")
    Errmsg(RC(8, VMCF), "VMCF(8) - duplicate message")
    Errmsg(RC(9, VMCF), "VMCF(9) - target VM in quiesce mode")
    Errmsg(RC(10,VMCF), "VMCF(10) - message limit exceeded")
    Errmsg(RC(11,VMCF), "VMCF(11) - REPLY canceled")
    Errmsg(RC(12,VMCF), "VMCF(12) - message id not found")
    Errmsg(RC(13,VMCF), "VMCF(13) - synchronization error")
    Errmsg(RC(14,VMCF), "VMCF(14) - CANCEL too late")
    Errmsg(RC(15,VMCF), "VMCF(15) - paging I/O error")
    Errmsg(RC(16,VMCF), "VMCF(16) - incorrect length")
    Errmsg(RC(17,VMCF), "VMCF(17) - destructive overlap")
    Errmsg(RC(18,VMCF), "VMCF(18) - user not authorized for priority messages")
    Errmsg(RC(19,VMCF), "VMCF(19) - data transfer error")
    Errmsg(RC(20,VMCF), "VMCF(20) - CANCEL busy")
 
    Errmsg(RC(1, SETREQDATA), "invalid request handle [setrequestdata(1)]")
    Errmsg(RC(2, SETREQDATA),
           "request not new (already sent) [setrequestdata(2)]");
 
    Errmsg(RC(1, SENDREQ), "invalid request handle [sendrequest(1)]")
    Errmsg(RC(2, SENDREQ), "request not new (already sent) [sendrequest(2)]")
 
    Errmsg(RC(1, RECVREQ), "request rejected (out of transmission slots)")
    Errmsg(RC(2, RECVREQ), "request rejected (connection to ext. proxy lost)")
    Errmsg(RC(3, RECVREQ), "request rejected (unknown reason)")
    Errmsg(RC(4, RECVREQ), "response state unknown")
 
    Errmsg(RC(1, WAITRESP), "invalid request handle [waitforresponse(1)]")
    Errmsg(RC(2, WAITRESP), "request not sent [waitforresponse(2)]")
    Errmsg(RC(3, WAITRESP), "no request pending [waitforanyresponse(1)]")
 
    Errmsg(RC(1, GETRESPDATA), "invalid request handle [getresponsedata(1)]")
    Errmsg(RC(2, GETRESPDATA), "response not available [getresponsedata(2)]")
    Errmsg(RC(3, GETRESPDATA), "invalid buffer index [getresponsedata(2)]")
 
    Errmsg(RC(1, FREEREQ), "invalid request handle [freerequest(1)]")
    Errmsg(RC(2, FREEREQ), "response still not received [freerequest(2)]")
 
    Errmsg(WAITANY_TIMEDOUT, "(no error) wait for response timed out");
 
    default:
      sprintf(errmsgbuffer, "unknown NICOFCLT rc: %d", rc);
      return errmsgbuffer;
    }
}
 
/* EBCDIC-bracket <-> ISO-Latin-1 translation tables (simplified) */
static const unsigned char e2a_tab[256] = {
/* 00 .. 0f */
0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x0D,0x20,0x20,
/* 10 .. 1f */
0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
/* 20 .. 2f */
0x20,0x20,0x20,0x20,0x20,0x0A,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
/* 30 .. 3f */
0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
/* 40 .. 4f */
0x20,0xA0,0xE2,0xE4,0xE0,0xE1,0xE3,0xE5,0xE7,0xF1,0xA2,0x2E,0x3C,0x28,0x2B,0x7C,
/* 50 .. 5f */
0x26,0xE9,0xEA,0xEB,0xE8,0xED,0xEE,0xEF,0xEC,0xDF,0x21,0x24,0x2A,0x29,0x3B,0xAC,
/* 60 .. 6f */
0x2D,0x2F,0xC2,0xC4,0xC0,0xC1,0xC3,0xC5,0xC7,0xD1,0xA6,0x2C,0x25,0x5F,0x3E,0x3F,
/* 70 .. 7f */
0xF8,0xC9,0xCA,0xCB,0xC8,0xCD,0xCE,0xCF,0xCC,0x60,0x3A,0x23,0x40,0x27,0x3D,0x22,
/* 80 .. 8f */
0xD8,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0xAB,0xBB,0xF0,0xFD,0xFE,0xB1,
/* 90 .. 9f */
0xB0,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x72,0xAA,0xBA,0xE6,0xB8,0xC6,0xA4,
/* a0 .. af */
0xB5,0x7E,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0xA1,0xBF,0xD0,0x5B,0xDE,0xAE,
/* b0 .. bf */
0x5E,0xA3,0xA5,0xB7,0xA9,0xA7,0xB6,0xBC,0xBD,0xBE,0xDD,0xA8,0xAF,0x5D,0xB4,0xD7,
/* c0 .. cf */
0x7B,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0xAD,0xF4,0xF6,0xF2,0xF3,0xF5,
/* d0 .. df */
0x7D,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x50,0x51,0x52,0xB9,0xFB,0xFC,0xF9,0xFA,0xFF,
/* e0 .. ef */
0x5C,0xF7,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0xB2,0xD4,0xD6,0xD2,0xD3,0xD5,
/* f0 .. ff */
0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0xB3,0xDB,0xDC,0xD9,0xDA,0x20
};
const unsigned char *e2a = e2a_tab;
 
static const unsigned char a2e_tab[256] = {
/* 00 .. 0f */
0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x25,0x40,0x40,0x0D,0x40,0x40,
/* 10 .. 1f */
0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
/* 20 .. 2f */
0x40,0x5A,0x7F,0x7B,0x5B,0x6C,0x50,0x7D,0x4D,0x5D,0x5C,0x4E,0x6B,0x60,0x4B,0x61,
/* 30 .. 3f */
0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0x7A,0x5E,0x4C,0x7E,0x6E,0x6F,
/* 40 .. 4f */
0x7C,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,
/* 50 .. 5f */
0xD7,0xD8,0xD9,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xAD,0xE0,0xBD,0xB0,0x6D,
/* 60 .. 6f */
0x79,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x91,0x92,0x93,0x94,0x95,0x96,
/* 70 .. 7f */
0x97,0x98,0x99,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xC0,0x4F,0xD0,0xA1,0x40,
/* 80 .. 8f */
0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
/* 90 .. 9f */
0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
/* a0 .. af */
0x41,0xAA,0x4A,0xB1,0x9F,0xB2,0x6A,0xB5,0xBB,0xB4,0x9A,0x8A,0x5F,0xCA,0xAF,0xBC,
/* b0 .. bf */
0x90,0x8F,0xEA,0xFA,0xBE,0xA0,0xB6,0xB3,0x9D,0xDA,0x9B,0x8B,0xB7,0xB8,0xB9,0xAB,
/* c0 .. cf */
0x64,0x65,0x62,0x66,0x63,0x67,0x9E,0x68,0x74,0x71,0x72,0x73,0x78,0x75,0x76,0x77,
/* d0 .. df */
0xAC,0x69,0xED,0xEE,0xEB,0xEF,0xEC,0xBF,0x80,0xFD,0xFE,0xFB,0xFC,0xBA,0xAE,0x59,
/* e0 .. ef */
0x44,0x45,0x42,0x46,0x43,0x47,0x9C,0x48,0x54,0x51,0x52,0x53,0x58,0x55,0x56,0x57,
/* f0 .. ff */
0x8C,0x49,0xCD,0xCE,0xCB,0xCF,0xCC,0xE1,0x70,0xDD,0xDE,0xDB,0xDC,0x8D,0x8E,0xDF
};
const unsigned char *a2e = a2e_tab;
 
/*
** () <- nicofclt_ebcdic2ascii(source, length, target);
*/
void ncf_090(const char *src, int length, char *trg) {
  while(length-- > 0) {
    *trg++ = (char)e2a[(unsigned char)*src++];
  }
}
 
 
/*
** () <- nicofclt_ascii2ebcdic(source, length, target);
*/
void ncf_091(const char *src, int length, char *trg) {
  while(length-- > 0) {
    *trg++ = (char)a2e[(unsigned char)*src++];
  }
}

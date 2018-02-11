/*
** INTRAPI.H   - low-level access to assembler facilities from C
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** However: this definitions and the assembler implementation should also
**          be useful for other C programs.
**
** This module defines the C procedure headers for a low-level API to
** (CMS resp. OS) assembler macros and other assembler items allowing to:
**
**  - register a handling routine for external interrupts
**  - enable/disable receiving external interrupts
**  - use VMCF for communication with another virtual machine
**
**  - wait for and post ECBs
**  - set timer for an interval and post an ECB on timeout
**
**  - register a handling routine for interrupts from one or more devices
**  - create/modify CCWs and perform SIOs for a device
**
**  - invoke selected DIAG functions (X'00' and X'08')
**
** All routines named __intrXX defined in the header file are implemented
** in the accompanying assembler module INTRAPI, where these routines are
** named @@INTRxx.
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
 
 
#ifndef __INTRAPI_INCLUDED
#define __INTRAPI_INCLUDED
 
 
/*
** ***** API basics
*/
 
/* basic data types to simplify things */
typedef unsigned char      _byte;
typedef unsigned short     _half;
typedef unsigned int       _full;
typedef unsigned long long _dblw;
typedef void*              _addr;
 
 
/* initialize interrupts handling machinery
   (this routine is to be called first before any other operation)
*/
extern void intrapi();
 
/*
** ***** selected DIAG functions
** (see IBM Virtual Machine Facility/370: System Programmer's Guide)
*/
 
 
/* DIAG-x00 : Extended-Identification Code
   () <- diagx00(outbuf, buflen)
*/
#define diagx00(outbuf, buflen) __intr00(outbuf, buflen)
extern void __intr00(char *outbuf, int buflen);
 
 
/* DIAG-x08 : execute CP command
   () <- diagx08(cpcmd, cpcmdlen)
*/
#define diagx08(cpcmd, cpcmdlen) __intrff(cpcmd, cpcmdlen)
#define CPexecuteCmd(cmd) __intrff((cmd), strlen(cmd))
extern void __intrff(char *cpcmd, int cpcmdlen);
 
 
/*
** ***** Interrupt handling
*/
 
/* enable handling of external interrupts with the given handler routine,
   which will be invoked from assembler code with the given stack area.
*/
typedef void (*ExtHandler)(int *intrParams);
extern void __intr01(ExtHandler handler, int *cstack, int cstacklen);
#define enable_ext(handler,cstack,cstacklen) \
  __intr01(handler,cstack,cstacklen)
 
 
/* disable handling if external interrupts.
*/
extern void __intr02();
#define disable_ext() \
  __intr02()
 
 
/* enable handling of internal interrupts with the given handler routine,
   which will be invoked from assembler code with the given stack area.
*/
typedef _full (*IntHandler)(
    _full deviceAddress,
    _full oldPsw1,
    _full oldPsw2,
    _full csw1,
    _full csw2);
 
/* set handling routine for device i/o interrupts ("internal interrupts")
*/
extern void __intr40(IntHandler handler, int *cstack, int cstacklen);
#define set_devint_handler(handler,cstack,cstacklen) \
  __intr40(handler,cstack,cstacklen)
 
/* (internal only) set assembler-int-handler at 'hndintdef[4]' and do SVC202
   returns 'failed': == 0 => no, != 0 => yes
*/
extern int __intr41(_full *hndintdef);
 
/* enable interrupt handling for devid 'dev'
** -> DEV: (input) device to start handling interrupts for
**                 (_half) 0x0000 .. 0x07ff
** -> FAILED: (output) outcome, with 0 = success, 1 = fail
*/
#define enable_devint_handling(DEV, FAILED) { \
  _half dev = ((DEV) & 0xfff); \
  _full hndintdef[7]; \
  sprintf((char*)hndintdef, "HNDINT  SET D%03X", dev); \
  hndintdef[4] = 0; \
  hndintdef[5] = (dev << 16) | 0xC1C3; /* 0x0ddd 'A' 'C' */ \
  hndintdef[6] = 0xFFFFFFFF; \
  (FAILED) = (__intr41(hndintdef)) ? (_byte)1 : (_byte)0; \
}
 
/* disable handling of interrupts for device 'dev'
** -> DEV: (input) device to end handling interrupts for
**                 (_half) 0x0000 .. 0x07ff
** -> FAILED: (output) outcome, with 0 = success, 1 = fail
*/
#define disable_devint_handling(DEV, FAILED) { \
  _full hndintdef[7]; \
  sprintf((char*)hndintdef, "HNDINT  CLR C%03X", ((DEV) & 0xfff));\
  hndintdef[4] = 0; \
  hndintdef[5] = 0; \
  hndintdef[6] = 0xFFFFFFFF; \
  (FAILED) = (__intr41(hndintdef)) ? (_byte)1 : (_byte)0; \
}
 
 
/*
** ***** ECB posting and waiting
*/
 
/* post the given ECB, triggering continuation of the waiting main 'thread'.
*/
extern void __intr10(_full *ecb);
#define post_ecb(ecb) \
  __intr10(ecb)
 
 
/* wait for the given ECB to be posted, this ECB should be reset for the wait
   to happen.
*/
extern void __intr11(_full *ecb);
#define wait_ecb(ecb) \
  __intr11(ecb)
 
/* wait for one the ECBs in the list to be posted.
   The parameter is a list of ECB-pointers, the last entry in the list must
   have the high order bit set (e.g. ECBLIST_END), all ECBs in the list
   should be reset for the wait to happen.
*/
extern void __intr12(_full **ecblist);
#define wait_anyecb(ecblist) \
  __intr12(ecblist)
 
#define ECBLIST_ELEM(ecb) (_full*)(((_full)&ecb) & 0x7FFFFFFF)
#define ECBLIST_END(ecb)  (_full*)(((_full)&ecb) | 0x80000000)
 
/*
** ***** timer facility
*/
 
/* set and arm timer to post the given ECB after 'interval' 1/100 seconds
*/
extern void __intr50(_full interval, _full *ecb);
#define set_timer(interval,ecb) \
  __intr50(interval, ecb)
 
/* reset timer
*/
extern void __intr51();
#define reset_timer() \
  __intr51()
 
 
/*
** ***** VMCF interfacing
*/
 
typedef union {
  _dblw dblword;
  struct { _full w1; _full w2; } words;
  char  chars[8];
} DBLWORD;
 
#define SET_USER_FOR_CP(dest,from) \
{ \
  int len = strlen(from); if (len > 8) { len = 8; } \
  memset(dest, ' ', 8); \
  memcpy(dest, from, len); \
}
 
typedef struct __vmcmhdr {
  _byte   v1;
  _byte   v2;
  _half   vmcmfunc;
  _full   vmcmmid;
  DBLWORD vmcmuser;
  _addr   vmcmvada;
  _full   vmcmlena;
  _addr   vmcmvadb;
  _full   vmcmlenb;
  DBLWORD vmcmuse;
  _byte   messageData[0]; /* SMSG message buffer: if used, the VMCMHDR must
                             be allocated large enough to hold the messages */
} VMCMHDR, *VMCMHDR_PTR;
 
typedef struct __vmcparm {
  _byte   v1;
  _byte   v2;
  _half   vmcpfunc;
  _full   vmcpmid;
  DBLWORD vmcpuser;
  _addr   vmcpvada;
  _full   vmcplena;
  _addr   vmcpvadb;
  _full   vmcplenb;
  DBLWORD vmcpuse;
} VMCPARM, *VMCPARM_PTR;
 
/* flags for VMCMHDR.v1 */
#define VMCMRESP ((_byte)0x80)   /* final interrupt (transmission complete) */
#define VMCMRJCT ((_byte)0x40)   /* rejected by sink VM */
#define VMCMPRTY ((_byte)0x20)   /* priority message */
 
/* flags for VMCPARM.v1 */
#define VMCPAUTS ((_byte)0x80)   /* authorize specific (-> vmcpuser) */
#define VMCPPRTY ((_byte)0x40)   /* priority request / authorize priority */
#define VMCPSMSG ((_byte)0x20)   /* authorize SMSG messages */
 
/* constants for VMCPARM.vmcpfunc and VMCMHDR.vmcmfunc */
#define VMCPAUTH ((_half)0x0000)   /* authorize    */
#define VMCPUAUT ((_half)0x0001)   /* un-authorize */
#define VMCPSEND ((_half)0x0002)   /* send         */
#define VMCPSENR ((_half)0x0003)   /* send/receive */
#define VMCPSENX ((_half)0x0004)   /* sendx        */
#define VMCPRECV ((_half)0x0005)   /* receive      */
#define VMCPCANC ((_half)0x0006)   /* cancel       */
#define VMCPREPL ((_half)0x0007)   /* reply        */
#define VMCPQUIE ((_half)0x0008)   /* quiesce      */
#define VMCPRESM ((_half)0x0009)   /* resume       */
#define VMCPIDEN ((_half)0x000A)   /* identify     */
#define VMCPRJCT ((_half)0x000B)   /* reject       */
 
extern int __intr20(VMCPARM_PTR param);
#define vmcf_request(param) \
  __intr20(param)
 
 
/***
**** *****  SIO related support
***/
 
/*** Channel Command Word (CCW) related definitions ***/
 
/* CCW base type
*/
typedef unsigned long long CCW;
 
/* initialize a CCW with command, address, flags and length data
*/
#define CCW_Init(_CCW,_CMD,_ADDR,_FLAGS,_LEN) \
{ \
  _full *__ccw__ = (_full*)&_CCW; \
  *__ccw__ = ((_CMD) << 24) | ((_full)(_ADDR) & 0x00FFFFFF); \
  __ccw__++; \
  *__ccw__ = ((_FLAGS) << 24) | ((_full)(_LEN) & 0x0000FFFF); \
}
 
/* set the address part of a CCW
*/
#define CCW_SetAddr(_CCW,_ADDR) \
{ \
  _full *__ccw__ = (_full*)&_CCW; \
  *__ccw__ &= 0xFF000000; \
  *__ccw__ |= ((_full)(_ADDR) & 0x00FFFFFF); \
}
 
/* set the length part of a CCW
*/
#define CCW_SetLen(_CCW,_LEN) \
{ \
  _full *__ccw__ = (_full*)&_CCW; __ccw__++; \
  *__ccw__ &= 0xFC000000; \
  *__ccw__ |= ((_full)(_LEN) & 0x0000FFFF); \
}
 
/* printf a CCW
*/
#define CCW_Printf(_PREFIX,_CCW) \
{ \
  _full *__ccw__ = (_full*)&_CCW; \
  printf("%s 0x%08X.%08X @ 0x%08X\n", \
         _PREFIX, __ccw__[0], __ccw__[1], __ccw__); \
}
 
/* CCW flags */
#define CCWFlag_CD   ((_byte)0x80)  /* chain data */
#define CCWFlag_CC   ((_byte)0x40)  /* chain command */
#define CCWFlag_SILI ((_byte)0x20)  /* suppress incorrent length indication */
#define CCWFlag_SKIP ((_byte)0x10)  /* skip data transfer to main storage */
#define CCWFlag_PCI  ((_byte)0x08)  /* program controlled interruption */
#define CCWFlag_IDA  ((_byte)0x04)  /* indirect addressing */
 
/*** Channel Status Word (CSW) related definitions ***/
 
/* flags for 'Unit status' in CSW2 */
#define Unit_Attention      0x80000000
#define Unit_Modifier       0x40000000
#define Unit_ControlUnitEnd 0x20000000
#define Unit_Busy           0x10000000
#define Unit_ChannelEnd     0x08000000
#define Unit_DeviceEnd      0x04000000
#define Unit_UnitCheck      0x02000000
#define Unit_UnitException  0x01000000
 
/* flags for 'Channel status' in CSW2 */
#define Channel_ProgCtrlIntr 0x00800000
#define Channel_IncorrectLen 0x00400000
#define Channel_ProgramChk   0x00200000
#define Channel_ProtectChk   0x00100000
#define Channel_ChanDataChk  0x00080000
#define Channel_ChanCtrlChk  0x00040000
#define Channel_IntfCtrlChk  0x00020000
#define Channel_ChainingChk  0x00010000
 
/* do a SIO for a device with a ccw-chain
*/
extern _full __intr30(_full deviceAddress, CCW *ccwChain);
#define SIO(deviceAddress,ccwChain) \
  __intr30(deviceAddress,ccwChain)
 
#endif

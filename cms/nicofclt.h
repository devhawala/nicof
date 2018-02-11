/*
** NICOFCLT.H  - NICOF client interface definition
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module defines the communication interface between a client VM
** and a proxy VM running the NICOF through VMCF.
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
 
 
#ifndef __NICOFCLT_Included
#define __NICOFCLT_Included
 
#include "intrapi.h"
 
#ifndef true
typedef char bool;
#define true 1
#define false 0
#endif
 
#ifndef NULL
#define NULL 0
#endif
 
/* constants */
#define WAITANY_TIMEDOUT 0xFFFFFFFF
#define NO_FILTER 0
#define NO_TIMEOUT 0xFFFFFFFF
 
/* general datatypes */
typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char byte;
 
/* a 'request_handle' represents a single request as opaque object */
typedef unsigned int request_handle;
#define NULL_REQUEST 0
 
/* handler for SMSG messages received by the client VM */
typedef void (*SmsgHandler)(DBLWORD vmcmuse, char *smsg);
 
 
/*
** rc <- nicofclt_init()
** rc <- nicofclt_initForSMSGs(smsg_handler)
**
** initialize the NICOF client api
**  -> either for "simple" data transfers
**  -> or additiinally with the possibility to receive SMSGs, which each will
**     call the passed handler
*/
#define nicofclt_init() ncf_001(NULL)
#define nicofclt_initForSMSGs(h) ncf_001(h)
extern int ncf_001(SmsgHandler handler);
 
 
/*
** () <- nicofclt_deinit()
++
** de-initialize the NICOF client api, releasing all ressources and
** disallowing all VMCF communication
*/
#define nicofclt_deinit() ncf_002()
extern void ncf_002();
 
 
/*
** handle <- nicofclt_createRequest(userWord1, userWord2)
**
** allocate a request object with the passed userWords but an empty data packet
*/
#define nicofclt_createRequest(w1, w2) ncf_010(w1, w2)
extern request_handle ncf_010(uint userWord1, uint userWord2);
 
 
/*
** rc <- nicofclt_setRequestData(handle, length, data)
** rc <- nicofclt_setRequestDataXlate(handle, length, data, xtab)
**
** set the content for the request's data packet
*/
#define nicofclt_setRequestData(h, l, d) ncf_011(h, l, d, NULL)
#define nicofclt_setRequestDataXlate(h, l, d, x) ncf_011(h, l, d, x)
extern int ncf_011(request_handle h, uint length, const char *data,
                   const unsigned char *xtab);
 
/*
** rc <- nicofclt_setRequestDataX(handle, length1, data1, length2, data2)
** rc <- nicofclt_setRequestDataXlateX(handle, l1, d1, xtab1, l2, d2, xtab2)
*/
#define nicofclt_setRequestDataX(h, l1, d1, l2, d2) \
  ncf_012(h, l1, d1, NULL, l2, d2, NULL)
#define nicofclt_setRequestDataXlateX(h, l1, d1, x1, l2, d2, x2) \
  ncf_012(h, l1, d1, x1, l2, d2, x2)
extern int ncf_012(request_handle h,
    uint length1, const char *data1, const unsigned char *xtab1,
    uint length2, const char *data2, const unsigned char *xtab2);
 
/*
** rc <- nicofclt_sendRequest(handle)
** rc <- nicofclt_sendRequestAndWait(handle)
**
** send the request to the default proxy-vm and possibly wait for the response
*/
#define nicofclt_sendRequest(h) ncf_020(h,0)
#define nicofclt_sendRequestAndWait(h) ncf_020(h,1)
extern int ncf_020(request_handle h, bool waitForResponse);
 
 
/*
** rc <- nicofclt_sendRequestTo(handle, vm_name)
** rc <- nicofclt_sendRequestToAndWait(handle, vm_name)
**
** send the request to the specified proxy-vm and possibly wait for the
** response
*/
#define nicofclt_sendRequestTo(h, vm) ncf_021(h,0,vm)
#define nicofclt_sendRequestToAndWait(h, vm) ncf_021(h,1,vm)
extern int ncf_021(request_handle h, bool waitForResponse, char const *vm);
 
 
/*
** rc <- nicofclt_waitForResponse(handle)
**
** wait for the response for the given request to arrive
*/
#define nicofclt_waitForResponse(h) ncf_030(h)
extern int ncf_030(request_handle h);
 
 
/*
** rc <- nicofclt_waitForAnyAvailable(&handle)
**
** wait for the next response to arrive and pass back the request to which the
** response is available; the response must still be received with
** nicofclt_waitForResponse(h).
*/
#define nicofclt_waitForAnyAvailable(hPtr) ncf_031(hPtr)
extern int ncf_031(request_handle *handlePtr);
 
 
/*
** {} <- nicofclt_setFilterTag(handle, filterTag);
**
** set the filter tag for the request handle h
*/
#define nicofclt_setFilterTag(h, f) ncf_035(h, f)
extern void ncf_035(request_handle h, uint filterTag);
 
/*
** tag <- nicofclt_getFilterTag(handle);
**
** get the filter tag of the request handle h
*/
#define nicofclt_getFilterTag(h) ncf_036(h)
extern uint ncf_036(request_handle h);
 
/*
** rc <- nicofclt_waitForAnyAvailableX(&handlePtr, filterTag, timeout)
**
** Generalized wait for available responses with filter and/or timeout.
** -> filterTag: if != NO_FILTER: wait for requests with matching
**               filterTag (see nicofclt_setFilterTag)
** -> timeout:   if != NO_TIMEOUT, this is the max. time to wait in
**               1/100 seconds (=> unit = 10ms)
**
** Returnvalue:
**   WAITANY_TIMEDOUT => no response arrived in the 'timeout' interval
**                       => *handlePtr is NULL
**   any other value  => the rc for the response received
**                       => *handlePtr is the request for the response available
**                           (the response must still be received with
**                           nicofclt_waitForResponse(h)).
*/
#define nicofclt_waitForAnyAvailableX(hPtr, f, t) ncf_037(hPtr, f, t)
int ncf_037(request_handle *handlePtr, uint filterTag, uint timeout);
 
/*
** {0,1} <- nicofclt_isAvailable(handle)
**
** query if the response for the request is available.
** 1 is returned exactly if the handle is valid, the request has been
** transmitted, the response arrived and has not been received with
** a "wait" function.
** In all other cases, 0 is returned.
*/
#define nicofclt_isAvailable(handle) ncf_032(handle)
extern int ncf_032(request_handle handle);
 
/*
** {0,1} <- nicofclt_isReceived(handle)
**
** query if the response for the request has been received.
** 1 is returned exactly if the handle is valid, the request has been
** transmitted, the response arrived and has already been received with
** a "wait" function.
** In all other cases, 0 is returned.
*/
#define nicofclt_isReceived(handle) ncf_033(handle)
extern int ncf_033(request_handle handle);
 
/*
** string <- nicofclt_getStateString(handle)
**
** return the text represenation of the internal state (FREE, NEW,...)
** of the request handle
*/
#define nicofclt_getStateString(handle) ncf_034(handle)
extern char* ncf_034(request_handle handle);
 
 
/*
** rc <- nicofclt_getResponseUserWords(handle, *userWord1, *userWord2)
**
** fetch the user-words from the response to the given request
*/
#define nicofclt_getResponseUserWords(h, uw1, uw2) ncf_040(h, uw1, uw2)
extern int ncf_040(request_handle h, uint *userWord1, uint *userWord2);
 
 
/*
** rc <- nicofclt_getResponseData(handle, buflen, buffer, *datalen)
** rc <- nicofclt_getResponseDataXlate(handle, buflen, buffer, *datalen, xtab)
**
** copy the response packet data the buffer and fetch the data length from the
** response for the given request
*/
#define nicofclt_getResponseData(h, bl, b, dl) \
  ncf_041(h, bl, b, dl, NULL, 0)
#define nicofclt_getResponseDataXlate(h, bl, b, dl, x) \
  ncf_041(h, bl, b, dl, x, 0)
#define nicofclt_getResponseDataFrom(h, bl, b, dl, from) \
  ncf_041(h, bl, b, dl, NULL, from)
#define nicofclt_getResponseDataXlateFrom(h, bl, b, dl, x, from) \
  ncf_041(h, bl, b, dl, x, from)
extern int ncf_041(
   request_handle h, uint bufferLen, char *buffer, uint *dataLen,
   const unsigned char *xtab, uint from);
 
/*
** rc <- nicofclt_getResponseDataLength(handle, *datalen)
**
** fetch the length of the response packet data
*/
#define nicofclt_getResponseDataLength(h, dlen) ncf_042(h, dlen)
extern int ncf_042(request_handle h, uint *dataLen);
 
 
/*
** rc <- nicofclt_getResponseDataByte(handle, idx, *byte)
**
** fetch a single byte at the given position from the response data packet
** for the given request
*/
#define nicofclt_getResponseDataByte(h, idx, b) ncf_043(h, idx, b)
extern int ncf_043(request_handle h, uint idx, char *b);
 
 
/*
** rc <- nicofclt_freeRequest(handle)
**
** free the given request and release the ressources bound to it
*/
#define nicofclt_freeRequest(h) ncf_050(h)
extern int ncf_050(request_handle h);
 
/*
** msg <- nicofclt_errmsg(errorcode)
*/
#define nicofclt_errmsg(code) ncf_000(code)
extern char* ncf_000(int code);
 
/*
** ***** ASCII <-> EBCDIC translation
*/
extern const unsigned char *a2e; /* ASCII => EBCDIC table */
extern const unsigned char *e2a; /* EBCDIC => ASCII table */
 
/*
** () <- nicofclt_ebcdic2ascii(source, length, target);
*/
#define nicofclt_ebcdic2ascii(src,length,trg) \
  ncf_090(src,length,trg)
extern void ncf_090(const char *src, int length, char *trg);
 
/*
** () <- nicofclt_ascii2ebcdic(source, length, target);
*/
#define nicofclt_ascii2ebcdic(src,length,trg) \
  ncf_091(src,length,trg)
extern void ncf_091(const char *src, int length, char *trg);
 
#endif

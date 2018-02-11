/*
** NCFBASES.H  - NICOF level-one base services definition
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module defines the base services allowing to communicate with the
** level one dispatcher on the java proxy side.
**
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2012
** Released to the public domain.
*/
 
 
#ifndef _NCFBASES_INCLUDED
#define _NCFBASES_INCLUDED
 
#include "nicofclt.h"
#include "ncfio.h"
 
#define SERVICENAME_MAXLEN 64
 
#define INVALID_ASYNC_HANDLE 0xFFFFFFFF
 
#define ERR_INVALID_SERVICE -1024
#define ERR_SVC_INVALIDRESULT -1025
#define ERR_SVC_EXCEPTION -1026
#define ERR_BASESVC_INVCMD -2048
 
#define NEW_BULK_SOURCE -32
#define ERR_BULK_SOURCE_INVALID -33
 
#define NEW_BULK_SINK -64
#define ERR_BULK_SINK_INVALID -65
 
/*
** rc <- ncfbasesvc_resolve(serviceName, (out) svcId)
**
** resolve the service name to its ID on the external process
*/
#define ncfbasesvc_resolve(name,id) \
  ncfb_001(name,id)
extern int ncfb_001(const char *serviceName, short *serviceId);
 
#define INDATA_TEXT  0x01
#define OUTDATA_TEXT 0x02
#define DATA_BINARY  0x00
 
/*
** RC <- ncfbasesvc_invoke_sync(
**         svcId,
**         svcCmd,
**         inCtlWord,
**         inData,
**         inDataLen,
**         outCtlWord,
**         outData,
**         outDataLen,
**         dataFlags);
*/
#define ncfbasesvc_invoke_sync(svcId,svcCmd,inCtlWord,inData,inDataLen, \
                               outCtlWord,outData,outDataLen,dataFlags) \
  ncfb_020(svcId,svcCmd,inCtlWord,inData,inDataLen, \
           outCtlWord,outData,outDataLen,dataFlags)
extern int ncfb_020(
              short   svcId,
              short   svcCmd,
              int     inCtlWord,
              void   *inData,
              uint    inDataLen,
              int    *outCtlWord,
              void   *outData, /* *must* have space for 2048 bytes ! */
              uint   *outDataLen,
              byte    dataFlags);
 
/*
** rc <- nfcbasesvc_invoke_begin(
**         handle,
**         svcId,
**         svcCmd,
**         inCtlWord,
**         inData,
**         inDataLen,
**         dataFlags);
*/
#define ncfbasesvc_invoke_begin(hndl,svcId,svcCmd,inCtlWord,inData,inDataLen, \
                                dataFlags) \
  ncfb_021(hndl,svcId,svcCmd,inCtlWord,inData,inDataLen,dataFlags)
extern int ncfb_021(
              request_handle *hndl,
              short   svcId,
              short   svcCmd,
              int     inCtlWord,
              void   *inData,
              uint    inDataLen,
              byte    dataFlags);
 
/*
** RC <- nfcbasesvc_invoke_end(
**         h,
**         outCtlWord,
**         outData,
**         outDataLen,
**         dataFlags);
*/
#define ncfbasesvc_invoke_end(h,outCtlWord,outData,outDataLen,dataFlags) \
  ncfb_022(h,outCtlWord,outData,outDataLen,dataFlags)
extern int ncfb_022(
              request_handle h,
              int    *outCtlWord,
              void   *outData, /* *must* have space for 2048 bytes ! */
              uint   *outDataLen,
              byte    dataFlags);
 
extern BULKSTREAM ncfbid2s(int streamId, bool isSourceStream, bool isText);
 
/*
** get the error message text for the passed return/error code
*/
#define ncfbasesvc_errmsg(rc) ncfb_000(rc)
extern char* ncfb_000(int rc);
#endif

/*
** SVCRNHFS.C  - NICOF Raw Host File System (level one) custom service
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module implements the operations for the Raw Host File Service provided
** by the corresponding custom service on the java side.
** This service allows to:
**  - fetch the name of the current directory on the host
**  - change the current directory on the host
**  - list the current directory, optionally with a file pattern
**  - retrieve or store a file in the current directory on the host,
**    either in binary mode or in text mode (with ascii<->ebcdic translation)
**
** The Java class registered in the external Java proxy to implement the
** service must be registered under the name: RawHostFileSvc
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
 
 
#include "svcrnhfs.h"
#include "ncfbases.h"
 
static const char *svcName = "RawHostFileSvc";
 
static short svcId;
static bool isInitialized = false;
 
static int lastRc = 0;
 
/* Initialize the service: connect to the external proxy and query the service
** id, verifying thaat the Host File System is available and operational.
*/
bool rnhfs011() {
  if (isInitialized) { return true; }
  lastRc = ncfbasesvc_resolve(svcName, &svcId);
  if (lastRc != 0) {
  /*printf("nhfsr011 :: ncfbasesvc_resolve('%s') -> rc = %d, svcId = %d\n",
       svcName, lastRc, svcId);*/
    return false;
  }
  isInitialized = true;
  return true;
}
 
 
/* get the current directory
** (returns 'false' if failed -> rawhostfs_lastXXX())
*/
bool rhnfs012(char *buffer, int bufferLength) {
  int outDataLen = 0;
  if (bufferLength > 2047) { bufferLength = 2047; }
  lastRc = ncfbasesvc_invoke_sync(
             svcId,        /* svcId */
             1,            /* svcCmd     ==> print working directory */
             bufferLength, /* inCtlWord  ==> client buffer length */
             NULL,         /* inData */
             NULL,         /* inDataLen */
             NULL,         /* outCtlWord */
             buffer,       /* outData    ==> path name buffer */
             &outDataLen,  /* outDataLen ==> length of path name */
             OUTDATA_TEXT  /* dataFlags */
             );
  if (lastRc == 0) {
    buffer[outDataLen] = '\0';
    return true;
  } else {
    buffer[0] = '\0';
    return false;
  }
}
 
 
/* change the current directory
** (returns 'false' if failed -> rawhostfs_lastXXX())
*/
bool rhnfs013(char *dirName) {
  lastRc = ncfbasesvc_invoke_sync(
             svcId,           /* svcId */
             2,               /* svcCmd     ==> change working directory */
             0,               /* inCtlWord */
             dirName,         /* inData     ==> new diretory name */
             strlen(dirName), /* inDataLen  ==> length of directory name */
             NULL,            /* outCtlWord */
             NULL,            /* outData */
             NULL,            /* outDataLen */
             INDATA_TEXT      /* dataFlags */
             );
  return (lastRc == 0);
}
 
 
/* list the content of the current directory
** (returns 'false' if failed -> rawhostfs_lastXXX())
*/
BULKSTREAM rhnfs014(char *pattern) {
  uint streamId;
  int patLen = (pattern) ? strlen(pattern) : 0;
  lastRc = ncfbasesvc_invoke_sync(
             svcId,      /* svcId */
             3,          /* svcCmd     ==> list current directory */
             0,          /* inCtlWord */
             pattern,    /* inData     ==> pattern */
             patLen,     /* inDataLen  ==> pattern length */
             &streamId,  /* outCtlWord ==> streamId for directory list */
             NULL,       /* outData */
             NULL,       /* outDataLen */
             INDATA_TEXT /* dataFlags */
             );
  if (lastRc == NEW_BULK_SOURCE) {
    BULKSTREAM stream = ncfbid2s(streamId, true, true);
    return stream;
  } else {
  /*printf("rhnfs014 :: open file source stream => rc = %d\n", lastRc);*/
  }
  return NULL;
}
 
 
/* Get the source stream to read a file in the current user's area.
** (returns 'null' if failed -> rawhostfs_lastXXX())
*/
BULKSTREAM rhnfs015(char *fn, bool isText) {
  uint streamId;
  lastRc = ncfbasesvc_invoke_sync(
             svcId,      /* svcId */
             4,          /* svcCmd     ==> read file */
             0,          /* inCtlWord */
             fn,         /* inData     ==> filename */
             strlen(fn), /* inDataLen  ==> length of filename */
             &streamId,  /* outCtlWord */
             NULL,       /* outData */
             NULL,       /* outDataLen */
             INDATA_TEXT /* dataFlags */
             );
  if (lastRc == NEW_BULK_SOURCE) {
    BULKSTREAM stream = ncfbid2s(streamId, true, isText);
    return stream;
  } else {
  /*printf("rhnfs015 :: open file source stream => rc = %d\n", lastRc);*/
  }
  return NULL;
}
 
 
/* Get a sink stream to write a file in the current user's area.
** (returns 'null' if failed -> rawhostfs_lastXXX())
*/
BULKSTREAM rhnfs016(char *fn, bool overwrite, bool isText) {
  uint streamId;
  int ctlData = (overwrite) ? 1 : 0;
  lastRc = ncfbasesvc_invoke_sync(
             svcId,      /* svcId */
             5,          /* svcCmd     ==> write file */
             ctlData,    /* inCtlWord  ==> overwrite */
             fn,         /* inData     ==> filename */
             strlen(fn), /* inDataLen  ==> length of filename */
             &streamId,  /* outCtlWord */
             NULL,       /* outData */
             NULL,       /* outDataLen */
             INDATA_TEXT /* dataFlags */
             );
  if (lastRc == NEW_BULK_SINK) {
    BULKSTREAM stream = ncfbid2s(streamId, false, isText);
    return stream;
  } else {
  /*printf("rhnfs016 :: open file sink stream => rc = %d\n", lastRc);*/
  }
  return NULL;
}
 
 
/* Return the error code of the last operation.
**
** Each remote operation executed sets the value retrieved by this function,
** with 0 meaning "operation was successful".
** Failures are indicated by one one the ERR_* constants in this header file
** or the codes defined in NCFBASES.H resp. NICOFCLT.H
*/
int rnhfs001() {
  return lastRc;
}
 
 
/* Get the message text for the error code of the last operation.
**
** This is equivalent to: hostfs_errmsg(hostfs_lastErrcode))
*/
char* rnhfs002() {
  return rawhostfs_errmsg(lastRc);
}
 
 
/* Get the message text for the given error code.
**
** The function handles the ERR_*- error code constants defined in this
** header file as well as the codes from the base services (NCFBASES.H) or
** the NICOF client (NICOFCLT.H).
*/
char* rnhfs000(int rc) {
  switch(rc) {
    case ERR_NOT_USABLE:
      return "host file service misconfigured and not usable";
    case ERR_INVALID_COMMAND:
      return "invalid command for host file service";
    case ERR_NO_FILENAME:
      return "no filename given";
    case ERR_CWD_FAILED:
      return "change working directory failed";
    case ERR_FILENAME_NOT_FOUND:
      return "file not found in current directory";
    case ERR_FILENAME_IS_DIR:
      return "the name specified is an existing directory";
    case ERR_FILE_NOT_READABLE:
      return "the file is not readable";
    case ERR_FILE_NOT_WRITABLE:
      return "thie file is not writable";
    case ERR_DIR_IS_READONLY:
      return "directory is read only";
    case ERR_FILE_ACCESS_ERROR:
      return "error accessing file";
    case ERR_FILE_EXISTS:
      return "file already exists";
    default:
      return ncfbasesvc_errmsg(lastRc);
  }
}
 

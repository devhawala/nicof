/*
** SVC_NHFS.C  - NICOF Host File System (level one) custom service
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module implements the operations for the Host File Service provided by
** the corresponding custom service on the java side. This service allows to:
**  - list directories
**  - create directories
**  - read text or binary files
**  - write (create) text or binary files
** Accessing files and directories is restricted to a user specific directory
** (the current user's area) in the base directory for the whole service.
** Furthermore only files and  directories named in a CMS compatible way
** (max. 8 character tokens for  filename, filetype oder directory name,
** 2 name components in the file and the like) can be access/created/listed.
**
** The Java class registered in the external Java proxy to implement the
** service must be registered under the name: HostFileSvc
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
 
 
#include "svc_nhfs.h"
#include "ncfbases.h"
 
static const char *svcName = "HostFileSvc";
 
static short svcId;
static bool isInitialized = false;
 
static int lastRc = 0;
 
bool nhfs_001() {
  if (isInitialized) { return true; }
  lastRc = ncfbasesvc_resolve(svcName, &svcId);
  if (lastRc != 0) {
  /*printf("nhfs_001 :: ncfbasesvc_resolve('%s') -> rc = %d, svcId = %d\n",
       svcName, lastRc, svcId);*/
    return false;
  }
  isInitialized = true;
  return true;
}
 
static int encodePath(char **pathElems, int pathElemsCount, char *buffer) {
  char *b = buffer;
  int i;
  for (i = 0; i < pathElemsCount && i < NHFS_MAX_PATH_DEPTH; i++) {
    char *s = pathElems[i];
    int len = strlen(s);
    if (len > 8) { len = 8; }
    memcpy(b, s, len);
    b += len;
    *b++ = ' ';
  }
  if (b != buffer) { b--; }
  *b = '\0';
  return (b - buffer);
}
 
BULKSTREAM nhfs_002(char **pathElems, int pathElemsCount) {
  char buffer[NHFS_MAX_PATH_DEPTH * 9];
  int bufLen = encodePath(pathElems, pathElemsCount, buffer);
 
  uint streamId;
  lastRc = ncfbasesvc_invoke_sync(
             svcId,      /* svcId */
             1,          /* svcCmd     ==> list files */
             0,          /* inCtlWord */
             buffer,     /* inData     ==> path components */
             bufLen,     /* inDataLen  ==> length of path components */
             &streamId,  /* outCtlWord */
             NULL,       /* outData */
             NULL,       /* outDataLen */
             INDATA_TEXT /* dataFlags */
             );
  if (lastRc == NEW_BULK_SOURCE) {
    BULKSTREAM stream = ncfbid2s(streamId, true, true);
    return stream;
  } else {
  /*printf("tstb_002 :: open dir list source stream => rc = %d\n", lastRc);*/
  }
  return NULL;
}
 
BULKSTREAM nhfs_003(
    char *fn,
    char *ft,
    char **pathElems,
    int pathElemsCount,
    bool isText) {
  char buffer[18 + (NHFS_MAX_PATH_DEPTH * 9)];
  sprintf(buffer, "%s %s%c", fn, ft, (pathElemsCount > 0) ? ' ' : '\0');
  int fnftLen = strlen(buffer);
  int pathLen = encodePath(pathElems, pathElemsCount, &buffer[fnftLen]);
  int bufLen = fnftLen + pathLen;
 
  uint streamId;
  lastRc = ncfbasesvc_invoke_sync(
             svcId,      /* svcId */
             2,          /* svcCmd     ==> read file */
             0,          /* inCtlWord */
             buffer,     /* inData     ==> fn, ft & path components */
             bufLen,     /* inDataLen  ==> length of path components */
             &streamId,  /* outCtlWord */
             NULL,       /* outData */
             NULL,       /* outDataLen */
             INDATA_TEXT /* dataFlags */
             );
  if (lastRc == NEW_BULK_SOURCE) {
    BULKSTREAM stream = ncfbid2s(streamId, true, isText);
    return stream;
  } else {
  /*printf("tstb_003 :: open file source stream => rc = %d\n", lastRc);*/
  }
  return NULL;
}
 
BULKSTREAM nhfs_004(
    char *fn,
    char *ft,
    bool overwrite,
    char **pathElems,
    int pathElemsCount,
    bool isText) {
  char buffer[18 + (NHFS_MAX_PATH_DEPTH * 9)];
  sprintf(buffer, "%s %s%c", fn, ft, (pathElemsCount > 0) ? ' ' : '\0');
  int fnftLen = strlen(buffer);
  int pathLen = encodePath(pathElems, pathElemsCount, &buffer[fnftLen]);
  int bufLen = fnftLen + pathLen;
 
  int ctlWord = (overwrite) ? 1 : 0;
 
  uint streamId;
  lastRc = ncfbasesvc_invoke_sync(
             svcId,      /* svcId */
             3,          /* svcCmd     ==> write file */
             ctlWord,    /* inCtlWord  ==> overwrite if exists? */
             buffer,     /* inData     ==> fn, ft & path components */
             bufLen,     /* inDataLen  ==> length of path components */
             &streamId,  /* outCtlWord */
             NULL,       /* outData */
             NULL,       /* outDataLen */
             INDATA_TEXT /* dataFlags */
             );
  if (lastRc == NEW_BULK_SINK) {
    BULKSTREAM stream = ncfbid2s(streamId, false, isText);
    return stream;
  } else {
  /*printf("tstb_004 :: open file sink stream => rc = %d\n", lastRc);*/
  }
  return NULL;
}
 
int nhfs_005(
    char *dirName,
    char **pathElems,
    int pathElemsCount) {
  char buffer[9 + (NHFS_MAX_PATH_DEPTH * 9)];
  sprintf(buffer, "%s%c", dirName, (pathElemsCount > 0) ? ' ' : '\0');
  int dirnameLen = strlen(buffer);
  int pathLen = encodePath(pathElems, pathElemsCount, &buffer[dirnameLen]);
  int bufLen = dirnameLen + pathLen;
 
  lastRc = ncfbasesvc_invoke_sync(
             svcId,      /* svcId */
             4,          /* svcCmd     ==> create directory */
             0,          /* inCtlWord */
             buffer,     /* inData     ==> dirName & path components */
             bufLen,     /* inDataLen  ==> length of path components */
             NULL,       /* outCtlWord */
             NULL,       /* outData */
             NULL,       /* outDataLen */
             INDATA_TEXT /* dataFlags */
             );
  return lastRc;
}
 
char* nhfs_000(int rc) {
  switch(rc) {
    case ERR_NOT_USABLE          :
      return "host file service misconfigured and not usable";
    case ERR_INVALID_COMMAND     :
      return "invalid command for host file service";
    case ERR_INV_NAME_TOKEN      :
      return "invalid character in name token";
    case ERR_MISSING_FNFT_TOKENS :
      return "missing filename or filetype token";
    case ERR_DIRPATH_NOT_PRESENT :
      return "specified directory path not present";
    case ERR_FILE_NOT_FOUND      :
      return "file not found";
    case ERR_FILE_READ_ERROR     :
      return "file read error";
    case ERR_FILE_EXISTS         :
      return "file already exists";
    case ERR_FILE_NOT_CREATED    :
      return "file could not be created";
    case ERR_DIR_ALREADY_EXISTS  :
      return "directory already exists";
    case ERR_DIR_NOT_CREATED     :
      return "directory could not be created";
    default:
      return ncfbasesvc_errmsg(lastRc);
  }
}
 
int nhfs_006() {
  return lastRc;
}
 
char* nhfs_007() {
  return hostfs_errmsg(lastRc);
}

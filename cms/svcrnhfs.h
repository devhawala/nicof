/*
** SVCRNHFS.H  - NICOF Raw Host File System (level one) custom service
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module defines the operations for the Raw Host File Service provided by
** the corresponding custom service on the java side.
** This service allows to:
**  - fetch the name of the current directory on the host
**  - change the current directory on the host
**  - list the current directory, optionally with a file pattern
**  - retrieve or store a file in the current directory on the host,
**    either in binary mode or in text mode (with ascii<->ebcdic translation)
**
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2014
** Released to the public domain.
*/
 
#ifndef SVC_RHNFS_IMPORTED
#define SVC_RHNFS_IMPORTED
 
#include "nicofclt.h"
#include "ncfio.h"
 
/* Initialize the service: connect to the external proxy and query the service
** id, verifying thaat the Host File System is available and operational.
*/
#define rawhostfs_init() \
  rnhfs011()
extern bool rnhfs011();
 
/* get the current directory
** (returns 'false' if failed -> rawhostfs_lastXXX())
*/
#define rawhostfs_getWD(buffer, bufferLength) \
  rhnfs012(buffer, bufferLength)
extern bool rhnfs012(char *buffer, int bufferLength);
 
 
/* change the current directory
** (returns 'false' if failed -> rawhostfs_lastXXX())
*/
#define rawhostfs_changeWD(dirName) \
  rhnfs013(dirName)
extern bool rhnfs013(char *dirName);
 
 
/* list the content of the current directory
** (returns 'null' if failed -> rawhostfs_lastXXX())
*/
#define rawhostfs_list(p) \
  rhnfs014(p)
extern BULKSTREAM rhnfs014(char *pattern);
 
 
/* Get the source stream to read a file in the current user's area.
** (returns 'null' if failed -> rawhostfs_lastXXX())
*/
#define rawhostfs_getfile(fileName, isText) \
  rhnfs015(fileName, isText)
extern BULKSTREAM rhnfs015(char *fileName, bool isText);
 
 
/* Get a sink stream to write a file in the current user's area.
** (returns 'null' if failed -> rawhostfs_lastXXX())
*/
#define rawhostfs_putfile(fileName, overwrite, isText) \
  rhnfs016(fileName, overwrite, isText)
extern BULKSTREAM rhnfs016(char *fileName, bool overwrite, bool isText);
 
 
/* error codes of the Raw NICOF Host File System
*/
#define ERR_NOT_USABLE         5050
#define ERR_INVALID_COMMAND    5051
#define ERR_NO_FILENAME        5052
#define ERR_CWD_FAILED         5060
#define ERR_FILENAME_NOT_FOUND 5070
#define ERR_FILENAME_IS_DIR    5071
#define ERR_FILE_NOT_READABLE  5072
#define ERR_FILE_NOT_WRITABLE  5073
#define ERR_DIR_IS_READONLY    5074
#define ERR_FILE_ACCESS_ERROR  5075
#define ERR_FILE_EXISTS        5076
 
 
/* Return the error code of the last operation.
**
** Each remote operation executed sets the value retrieved by this function,
** with 0 meaning "operation was successful".
** Failures are indicated by one one the ERR_* constants in this header file
** or the codes defined in NCFBASES.H resp. NICOFCLT.H
*/
#define rawhostfs_lastErrcode() rnhfs001()
extern int rnhfs001();
 
 
/* Get the message text for the error code of the last operation.
**
** This is equivalent to: hostfs_errmsg(hostfs_lastErrcode))
*/
#define rawhostfs_lastErrmsg() rnhfs002()
extern char* rnhfs002();
 
 
/* Get the message text for the given error code.
**
** The function handles the ERR_*- error code constants defined in this
** header file as well as the codes from the base services (NCFBASES.H) or
** the NICOF client (NICOFCLT.H).
*/
#define rawhostfs_errmsg(rc) rnhfs000(rc)
extern char* rnhfs000(int rc);
 
#endif /* SVC_RNHFS_IMPORTED */

/*
** SVC_NHFS.H  - NICOF Host File System (level one) custom service
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module defines the operations for the Host File Service provided by
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
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2012
** Released to the public domain.
*/
 
#ifndef SVC_HNFS_IMPORTED
#define SVC_HNFS_IMPORTED
 
#include "nicofclt.h"
#include "ncfio.h"
 
#define NHFS_MAX_PATH_DEPTH 10
 
/* Initialize the service: connect to the external proxy and query the service
** id, verifying thaat the Host File System is available and operational.
*/
#define hostfs_init() \
  nhfs_001()
extern bool nhfs_001();
 
 
/* List a directory in the current user's area.
**
** The directory is identified by the 'count' first entries in 'pathElems',
** with each element is a subdirectory in the chain. Each subdirecory name
** must conform to the CMS naming convention for a filename (1-8 characters,
** alphanumeric as well as + - _
*/
#define hostfs_list(pathElems,count) \
  nhfs_002(pathElems,count)
extern BULKSTREAM nhfs_002(char **pathElems, int count);
 
 
/* Get the source stream to read a file in the current user's area.
**
** The file is identified by the 'fn' and 'ft' parameters and is named 'fn.ft'.
** The directory where it is to be looked for is given by 'pathElems'/'count'
** (see hostfs_list()).
** If 'isText' is true, then the file will be handled as text file and a text
** source stream is returned (to be used with ngets, ngetline, ngetstr). If it
** is false, a binary source stream is returned (to be used with nread).
**
** If the file cannot be accessed, NULL is returned and the failure reason
** can be rerieved with nerror resp. nerrmsg.
*/
#define hostfs_getfile(fn,ft,pathElems,count,isText) \
  nhfs_003(fn,ft,pathElems,count,isText)
extern BULKSTREAM nhfs_003(
    char *fn,
    char *ft,
    char **pathElems,
    int count,
    bool isText);
 
 
/* Get a sink stream to write a file in the current user's area.
**
** The file is identified by the 'fn' and 'ft' parameters and is named 'fn.ft'.
** The directory where it is to be written for is given by 'pathElems'/'count'
** (see hostfs_list()).
** If 'isText' is true, then the file will be handled as text file and a text
** sink stream is returned (to be used with nputs, nputline, nputstr). If it
** is false, a binary sink stream is returned (to be used with nwrite).
**
** If the file exists, the file will not be replaced unless 'overwrite' is
** passed with the value true.
**
** If the file cannot be created, NULL is returned and the failure reason
** can be rerieved with hostfs_lastErrorcode resp. hostfs_lastErrormsg.
*/
#define hostfs_putfile(fn,ft,overwrite,pathElems,count,isText) \
  nhfs_004(fn,ft,overwrite,pathElems,count,isText)
extern BULKSTREAM nhfs_004(
    char *fn,
    char *ft,
    bool overwrite,
    char **pathElems,
    int count,
    bool isText);
 
 
/* Create a subdirectory in the current user's area.
**
** The new subdirectory is identified by the 'dirName' parameter.
** The directory where it is to be created is given by 'pathElems'/'count'
** (see hostfs_list()).
**
** Returns 0 if the directory was created or the error code for the operation.
*/
#define hostfs_mkdir(dirName,pathElems,count) \
  nhfs_005(dirName,pathElems,count)
int nhfs_005(
    char *dirName,
    char **pathElems,
    int count);
 
#define ERR_NOT_USABLE          4050
#define ERR_INVALID_COMMAND     4051
#define ERR_INV_NAME_TOKEN      4052
#define ERR_MISSING_FNFT_TOKENS 4060
#define ERR_DIRPATH_NOT_PRESENT 4061
#define ERR_FILE_NOT_FOUND      4062
#define ERR_FILE_READ_ERROR     4063
#define ERR_FILE_EXISTS         4070
#define ERR_FILE_NOT_CREATED    4071
#define ERR_DIR_ALREADY_EXISTS  4072
#define ERR_DIR_NOT_CREATED     4073
 
/* Return the error code of the last operation.
**
** Each remote operation executed sets the value retrieved by this function,
** with 0 meaning "operation was successful".
** Failures are indicated by one one the ERR_* constants in this header file
** or the codes defined in NCFBASES.H resp. NICOFCLT.H
*/
#define hostfs_lastErrcode() nhfs_006()
extern int nhfs_006();
 
 
/* Get the message text for the error code of the last operation.
**
** This is equivalent to: hostfs_errmsg(hostfs_lastErrcode))
*/
#define hostfs_lastErrmsg() nhfs_007()
extern char* nhfs_007();
 
 
/* Get the message text for the given error code.
**
** The function handles the ERR_*- error code constants defined in this
** header file as well as the codes from the base services (NCFBASES.H) or
** the NICOF client (NICOFCLT.H).
*/
#define hostfs_errmsg(rc) nhfs_000(rc)
extern char* nhfs_000(int rc);
 
#endif

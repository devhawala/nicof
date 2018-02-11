/*
** RNHFS.C     - Command line interface to the NICOF Raw Host File System
**               custom service
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module provides the following commands (assuming the name RNHFS MODULE):
**
**  RNHFS PWD
**    -> print the current directory on the console
**
**  RNHFS CD hostdirname
**    -> change the current directory to 'hostdirname'
**
**  RNHFS LIST [ pattern ]
**    -> list all files in the current directory if 'pattern' is omitted
**       resp. with 'pattern' (possibly with absolute or relative path to
**       the current directory)
**
**  RNHFS TYPE hostfile
**    -> type the specified file on the console
**
**  RNHFS PUT fn ft [ fm ] hostfile [ ( [ REPLACE ] ]
**    -> copy the CMS file 'fn ft fm' to the file 'hostfile' in
**       the current directory.
**       The file content will be converted form EBCDIC to ASCII.
**       If the file exists, REPLACE must be given to overwrite it.
**
**  RNHFS PUTBIN fn ft [ fm ] hostfile [ ( [ REPLACE ] ]
**    -> copy the CMS file 'fn ft fm' to the file 'hostfile' in
**       the current directory.
**       The file content is transferred in binary mode, no conversion is made.
**       If the file exists, REPLACE must be given to overwrite it.
**
**  RNHFS GET hostfile fn ft [ fm ] [ ( [ REPLACE ] [ RECFM x ] [ LRECL x ] ]
**    -> copy the file 'hostfile' from the host to the file 'fn ft fm' in
**       CMS with the given RECFM and LRECL (with defaults: RECFM V LRECL 80).
**       The file content will be converted form ASCII to EBCDIC.
**       If the file exists, REPLACE must be given to overwrite it.
**
**  RNHFS GETBIN hostfile fn ft [ fm ] [ ( [ REPLACE ] [ RECFM x ] [ LRECL x ] ]
**    -> copy the file 'hostfile' from the host to the file 'fn ft fm' in
**       CMS with the given RECFM and LRECL (with defaults: RECFM V LRECL 80).
**       The file content is transferred in binary mode, no conversion is made.
**       If the file exists, REPLACE must be given to overwrite it.
**
** When transferring files:
**  - the CMS file LRECL is limited to 255 characters
**  - the CMS file is always specified first
**  - the host file is always given as last (non-option) parameter and
**    must be a single token (no blanks permitted)
**  - the omission of 'fm' is recognized if only 3 non-option parameters
**    are found when enumerating the parameter tokens; minidisk A is then
**    assumed when PUTting files to the host resp. A1 when GETting files
**    from the host
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
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
#include "nhfscomn.h"
#include "svcrnhfs.h"
 
static char hostElemName[2048];
 
/*
** main routine
*/
 
static int interpretOptions(int argc, char *argv[], bool paramsEndOnly) {
  int i;
  bool inOptions = false;
  int lastParam = argc;
  char *p;
  for (i = 2; i < argc; i++) {
    p = argv[i];
    if (strequiv(p, "(")) {
      if (paramsEndOnly) { return i; }
      if (!inOptions) { lastParam = i; }
      inOptions = true;
    } else if (!inOptions) {
      continue;
    } else if (strequiv(p, "recfm")) {
      i++;
      recfm = 'X';
      if (i < argc) {
        p = argv[i];
        if (strequiv(p, "V")) {
          recfm = 'V';
        } else if (strequiv(p, "F")) {
          recfm = 'F';
        }
      }
      if (recfm == 'X') {
        printf("Command option incomplete (missing/invalid RECFM value)\n");
        return -1;
      }
    } else if (strequiv(p, "lrecl")) {
      i++;
      if (i < argc) { p = argv[i]; }
      lrecl = atoi(p);
      if (lrecl < 1 || lrecl > 255) {
        printf("Command option incomplete (missing/invalid LRECL value)\n");
        return -1;
      }
    } else if (strequiv(p, "append")) {
      doAppend = true;
    } else if (strequiv(p, "replace")) {
      doReplace = true;
    } else {
      printf("Invalid option '%s'\n", p);
      return -1;
    }
  }
/*if (!paramsEndOnly) {
    printf("** options: recfm %c   lrecl %d   append %s   replace %s\n",
      recfm, lrecl, (doAppend) ? "yes" : "no", (doReplace) ? "yes" : "no");
  }*/
  return lastParam;
}
 
#define DONE(rc) { nicofclt_deinit(); return rc; }
 
int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("\nUsage:\n");
    printf("   %s PWD\n", argv[0]);
    printf("   %s CD hostdirname\n", argv[0]);
    printf("   %s LIST [ filepattern ]\n", argv[0]);
    printf("   %s TYPE hostfilename\n", argv[0]);
    printf("   %s PUT fn ft [ fm ]  hostfilename [ ( options ]\n", argv[0]);
    printf("   %s PUTBIN fn ft [ fm ] hostfilename [ ( options ]\n", argv[0]);
    printf("   %s GET hostfilename fn ft [ fm ] [ ( options ]\n", argv[0]);
    printf("   %s GETBIN hostfilename fn ft [ fm ] [ ( options ]\n", argv[0]);
    printf("\n");
    printf("Options:\n");
    printf("  REPLACE           (for: PUT, PUTBIN, GET, GETBIN)\n");
    printf("  LRECL len         (for: GET, GETBIN; with len in 1..255)\n");
    printf("  RECFM x           (for: GET, GETBIN; with x in V or F)\n");
    printf("\n");
    return 0;
  }
 
  /*
  ** init communications APIs
  */
  intrapi();
  nicofclt_init();
 
  /*
  ** initialize the HostFS-Service
  */
  if (!rawhostfs_init()) {
    printf("** unable to initialize SVC_NHFS, aborting\n");
    printf("** reason: %s\n", rawhostfs_lastErrmsg());
    DONE(4);
  }
 
  /*
  ** interpret the first arg as command
  */
  if (strequiv(argv[1], "pwd")) {
 
    /*
    ** PWD
    */
 
    if (rawhostfs_getWD(hostElemName, sizeof(hostElemName))) {
      printf("Current working directory:\n %s\n\n", hostElemName);
    } else {
      printf("** error querying current host directory\n");
      printf("** reason: %s\n", rawhostfs_lastErrmsg());
      DONE(12);
    }
 
  } else if (strequiv(argv[1], "cd")) {
 
    /*
    ** CD hostdirname
    */
 
    argc = interpretOptions(argc, argv, false);
    if (argc < 3) {
      printf("** missing <directory>-argument for subcommand CD\n");
      DONE(4);
    }
    bool wasOk = rawhostfs_changeWD(argv[2]);
    if (!wasOk) {
      printf("** unable to change to directory: %s\n", argv[2]);
      printf("** reason: %s\n", rawhostfs_lastErrmsg());
    }
 
  } else if (strequiv(argv[1], "list")) {
 
    /*
    ** LIST [ pattern ]
    */
 
    char *pattern = NULL;
    argc = interpretOptions(argc, argv, false);
    if (argc > 2) { pattern = argv[2]; }
 
    BULKSTREAM stream = rawhostfs_list(pattern);
    if (stream == NULL) {
      printf("** error listing host directory\n");
      printf("** reason: %s\n", rawhostfs_lastErrmsg());
      DONE(24);
    }
 
    char lineBuffer[81];
    char *line;
    while(line = ngetline(lineBuffer, 81, stream)) {
        printf("%s\n", line);
    }
    nclose(stream);
 
  } else if (strequiv(argv[1], "type")) {
 
    /*
    ** TYPE hostfilename
    */
 
    argc = interpretOptions(argc, argv, true);
    if (argc < 0) { DONE(4); }
    if (argc < 3) {
      printf("** missing argument <hostfilename> for subcommand TYPE\n");
      DONE(4);
    }
    BULKSTREAM stream = rawhostfs_getfile(argv[2], true);
    if (stream == NULL) {
      printf("** error accessing host file\n");
      printf("** reason: %s\n", rawhostfs_lastErrmsg());
      DONE(24);
    }
    char lineBuffer[81];
    char *line;
    while(line = ngetline(lineBuffer, 81, stream)) {
      printf("%s\n", line);
    }
    nclose(stream);
 
  } else if (strequiv(argv[1], "put")) {
 
    /*
    ** PUT fn ft [ fm ] hostfilename [ ( options ]
    */
 
    argc = interpretOptions(argc, argv, false);
    if (argc < 0) { DONE(4); }
    if (argc < 5) {
      printf("** missing arguments for subcommand PUT\n");
      DONE(4);
    }
 
    char *fm = "A";
    char *hostfilename = argv[4];
    if (argc > 5) {
      fm = argv[4];
      hostfilename = argv[5];
    }
 
    int rc = openFile(argv[2], argv[3], fm, true);
    if (rc != 0) { DONE(rc); }
    BULKSTREAM stream = rawhostfs_putfile(
                          hostfilename,
                          doReplace,
                          true);
    if (stream == NULL) {
      printf("** error accessing host file\n");
      printf("** reason: %s\n", rawhostfs_lastErrmsg());
      closeFile();
      DONE(24);
    }
    bool eof;
    int len;
    len = readRecord(&eof);
    while (!eof) {
      if (!nputline(io_buffer, stream)) {
        printf("** error writing to host file, transfer aborted\n");
        printf("** reason: %s\n", rawhostfs_lastErrmsg());
        closeFile();
        nclose(stream);
        DONE(24);
      }
      len = readRecord(&eof);
    }
    closeFile();
    nclose(stream);
 
  } else if (strequiv(argv[1], "putbin")) {
 
    /*
    ** PUTBIN fn ft [ fm ] hostfilename [ ( options ]
    */
 
    argc = interpretOptions(argc, argv, false);
    if (argc < 0) { DONE(4); }
    if (argc < 5) {
      printf("** missing arguments for subcommand PUTBIN\n");
      DONE(4);
    }
 
    char *fm = "A";
    char *hostfilename = argv[4];
    if (argc > 5) {
      fm = argv[4];
      hostfilename = argv[5];
    }
 
    int rc = openFile(argv[2], argv[3], fm, true);
    if (rc != 0) { DONE(rc); }
    BULKSTREAM stream = rawhostfs_putfile(
                          hostfilename,
                          doReplace,
                          false);
    if (stream == NULL) {
      printf("** error accessing host file\n");
      printf("** reason: %s\n", rawhostfs_lastErrmsg());
      closeFile();
      DONE(24);
    }
    bool eof;
    int len;
    len = readRecord(&eof);
    while (!eof && len >= 0) {
      if (recfm == 'F' && len != lrecl) {
        printf("*** recfm = 'F', lrecl = %d BUT len = %d\n", lrecl, len);
      }
      uint count = nwrite(io_buffer, len, stream);
      if (count != lrecl && nerror(stream) != NERR_NOERROR) {
        printf("** error writing to host file, transfer aborted\n");
        printf("** reason: %s\n", rawhostfs_lastErrmsg());
        closeFile();
        nclose(stream);
        DONE(24);
      }
      len = readRecord(&eof);
    }
    closeFile();
    nclose(stream);
 
  } else if (strequiv(argv[1], "get")) {
 
    /*
    ** GET hostfilename fn ft [ fm ] [ ( options ]
    */
 
    argc = interpretOptions(argc, argv, false);
    if (argc < 0) { DONE(4); }
    if (argc < 5) {
      printf("** missing arguments for subcommand GET\n");
      DONE(4);
    }
    if (argc > 6) {
      printf("** too many arguments for subcommandn GET\n");
      DONE(4);
    }
 
    char *hostfilename = argv[2];
    char *fn = argv[3];
    char *ft = argv[4];
    char *fm = (argc > 5) ? argv[5] : "A";
 
    if (f_exists(fn, ft, fm) && !doReplace) {
      printf("** CMS file already exists, transfer aborted\n");
      DONE(24);
    }
    BULKSTREAM stream = rawhostfs_getfile(
                          hostfilename,
                          true);
    if (stream == NULL) {
      printf("** unable to access host file, aborting\n");
      printf("** reason: %s\n", rawhostfs_lastErrmsg());
      DONE(24);
    }
    int rc = openFile(fn, ft, fm, false);
    if (rc != 0) {
      nclose(stream);
      DONE(rc);
    }
    char *line;
    while(line = ngetline(io_buffer, lrecl + 1, stream)) {
      int len = strlen(line);
      if (writeRecord(len)) {
        closeFile();
        nclose(stream);
        DONE(24);
      }
    }
    if (!neof(stream)) {
      printf("Error reading from host file, nerror = %d\n", nerror(stream));
      closeFile();
      nclose(stream);
      DONE(24);
    }
    closeFile();
    nclose(stream);
 
  } else if (strequiv(argv[1], "getbin")) {
 
    /*
    ** GETBIN hostfilename fn ft [ fm ] [ ( options ]
    */
 
    argc = interpretOptions(argc, argv, false);
    if (argc < 0) { DONE(4); }
    if (argc < 5) {
      printf("** missing arguments for subcommand GETBIN\n");
      DONE(4);
    }
    if (argc > 6) {
      printf("** too many arguments for subcommandn GETBIN\n");
      DONE(4);
    }
 
    char *hostfilename = argv[2];
    char *fn = argv[3];
    char *ft = argv[4];
    char *fm = (argc > 5) ? argv[5] : "A";
 
    if (f_exists(fn, ft, fm) && !doReplace) {
      printf("** CMS file already exists, transfer aborted\n");
      DONE(24);
    }
    BULKSTREAM stream = rawhostfs_getfile(
                          hostfilename,
                          false);
    if (stream == NULL) {
      printf("** unable to access host file, aborting\n");
      printf("** reason: %s\n", rawhostfs_lastErrmsg());
      DONE(24);
    }
    int rc = openFile(fn, ft, fm, false);
    if (rc != 0) {
      nclose(stream);
      DONE(rc);
    }
    int recLen = nread(io_buffer, lrecl, false, stream);
    while(!neof(stream) && nerror(stream) == NERR_NOERROR) {
      if (writeRecord(recLen)) {
        closeFile();
        nclose(stream);
        DONE(24);
      }
      recLen = nread(io_buffer, lrecl, false, stream);
    }
    if (!neof(stream)) {
      printf("Error reading from host file, nerror = %d\n", nerror(stream));
      closeFile();
      nclose(stream);
      DONE(24);
    }
    closeFile();
    nclose(stream);
 
  } else {
    printf("** unknown subcommand '%s', aborting\n", argv[1]);
    DONE(4);
  }
 
  DONE(0);
}

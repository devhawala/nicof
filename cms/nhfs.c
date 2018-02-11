/*
** NHFS.C      - Simple command line interface to the NICOF Host File System
**               custom service
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module provides the following commands (assuming the name NHFS MODULE):
**
**   NHFS LIST [ dir1 [ dir2 [...] ] ]
**     -> list the files in the user's area resp. the subdirectory path given
**
**   NHFS TYPE fn ft [ dir1 [ dir2 [...] ] ]
**     -> type the file fn.ft in the user's area or in the subdirectory given)
**
**   NHFS PUT fn ft [ dir1 [ ...] ] [ ( [ REPLACE ] ]
**     -> copy the CMS file 'fn ft A to the file fn.ft in the user's area (resp.
**        the subdirectory).
**        The file content will be converted form EBCDIC to ASCII.
**        If the file exists, REPLACE must be given to overwrite it.
**
**   NHFS PUTBIN fn ft [ dir1 [ ...] ] [ ( [ REPLACE ] ]
**     -> copy the CMS file 'fn ft A to the file fn.ft in the user's area (resp.
**        the subdirectory).
**        The file content is transferred in binary mode, no conversion is made.
**        If the file exists, REPLACE must be given to overwrite it.
**
**   NHFS GET fn ft [ dir1 [ ...] ] [ ( [ REPLACE ] [ RECFM x ] [ LRECL x ] ]
**     -> copy the file fn.ft in the user's area (subdirectory) to the file
**        'fn ft A' in CMS with the given RECFM and LRECL
**        (with defaults: RECFM V LRECL 80).
**        The file content will be converted form ASCII to EBCDIC.
**        If the file exists, REPLACE must be given to overwrite it.
**
**   NHFS GETBIN fn ft [ dir1 [ ...] ] [ ( [ REPLACE ] [ RECFM x ] [ LRECL x ] ]
**     -> copy the file fn.ft in the user's area (subdirectory) to the file
**        'fn ft A' in CMS with the given RECFM and LRECL
**        (with defaults: RECFM V LRECL 80).
**        The file content is transferred in binary mode, no conversion is made.
**        If the file exists, REPLACE must be given to overwrite it.
**
**   NHFS MKDIR dirname [ dir1 [ dir2 [...] ] ]
**     -> create a new subdirectory 'dirname' in the user's area resp. in the
**        subdirectory given.
**
** When transferring files, the CMS file LRECL is limited to 255 characters.
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
 
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
#include "nhfscomn.h"
#include "svc_nhfs.h"
 
/*
** MAIN CODE
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
 
int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("\nUsage:\n");
    printf("   %s list [ dir1 [ dir2 ... ] ]\n", argv[0]);
    printf("   %s type fn ft [ dir1 [ dir2 ... ] ]\n", argv[0]);
    printf("   %s mkdir dirname [ dir1 [ dir2 ... ] ]\n", argv[0]);
    printf("   %s put fn ft [ dir1 [ dir2 ... ] ] [ ( options ]\n", argv[0]);
    printf("   %s putbin fn ft [ dir1 [ dir2 ... ] ] [ ( options ]\n", argv[0]);
    printf("   %s get fn ft [ dir1 [ dir2 ... ] ] [ ( options ]\n", argv[0]);
    printf("   %s getbin fn ft [ dir1 [ dir2 ... ] ] [ ( options ]\n", argv[0]);
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
  if (!hostfs_init()) {
    printf("** unable to initialize SVC_NHFS, aborting\n");
    printf("** reason: %s\n", hostfs_lastErrmsg());
    DONE(4);
  }
 
  /*
  ** interpret the first arg as command
  */
  if (strequiv(argv[1], "list")) {
    argc = interpretOptions(argc, argv, true);
    BULKSTREAM stream = hostfs_list(&argv[2], argc - 2);
    if (stream == NULL) {
      printf("** error listing host directory\n");
      printf("** reason: %s\n", hostfs_lastErrmsg());
      DONE(24);
    }
 
    char lineBuffer[81];
    char *line;
    while(line = ngetline(lineBuffer, 81, stream)) {
      if (*line == 'D') {
        printf("%-9s <directory>\n", &line[1]);
      } else {
        printf("%s\n", &line[1]);
      }
    }
    nclose(stream);
  } else if (strequiv(argv[1], "type")) {
    argc = interpretOptions(argc, argv, true);
    if (argc < 0) { DONE(4); }
    if (argc < 4) {
      printf("** missing arguments <fn ft> for subcommand type\n");
      DONE(4);
    }
    BULKSTREAM stream = hostfs_getfile(
                          argv[2], argv[3],
                          &argv[4], argc - 4,
                          true);
    if (stream == NULL) {
      printf("** error accessing host file\n");
      printf("** reason: %s\n", hostfs_lastErrmsg());
      DONE(24);
    }
    char lineBuffer[81];
    char *line;
    while(line = ngetline(lineBuffer, 81, stream)) {
      printf("%s\n", line);
    }
    nclose(stream);
  } else if (strequiv(argv[1], "mkdir")) {
    argc = interpretOptions(argc, argv, true);
    if (argc < 0) { DONE(4); }
    if (argc < 3) {
      printf("** missing arguments <dirname> for subcommand mkdir\n");
      DONE(4);
    }
    int rc = hostfs_mkdir(argv[2], &argv[3], argc - 3);
    if (rc != 0) {
      printf("** error creating host directory\n");
      printf("** reason: %s\n", hostfs_lastErrmsg());
      DONE(12);
    }
  } else if (strequiv(argv[1], "put")) {
    argc = interpretOptions(argc, argv, false);
    if (argc < 0) { DONE(4); }
    if (argc < 4) {
      printf("** missing arguments <fn ft> for subcommand put\n");
      DONE(4);
    }
    int rc = openFile(argv[2], argv[3], "A", true);
    if (rc != 0) { DONE(rc); }
    BULKSTREAM stream = hostfs_putfile(
                          argv[2], argv[3],
                          doReplace,
                          &argv[4], argc - 4,
                          true);
    if (stream == NULL) {
      printf("** error accessing host file\n");
      printf("** reason: %s\n", hostfs_lastErrmsg());
      closeFile();
      DONE(24);
    }
    bool eof;
    int len;
    len = readRecord(&eof);
    while (!eof) {
      if (!nputline(io_buffer, stream)) {
        printf("** error writing to host file, trnasfer aborted\n");
        printf("** reason: %s\n", hostfs_lastErrmsg());
        closeFile();
        nclose(stream);
        DONE(24);
      }
      len = readRecord(&eof);
    }
    closeFile();
    nclose(stream);
  } else if (strequiv(argv[1], "putbin")) {
    doText = false;
    argc = interpretOptions(argc, argv, false);
    if (argc < 0) { DONE(4); }
    if (argc < 4) {
      printf("** missing arguments <fn ft> for subcommand putbin\n");
      DONE(4);
    }
    int rc = openFile(argv[2], argv[3], "A", true);
    if (rc != 0) { DONE(rc); }
    BULKSTREAM stream = hostfs_putfile(
                          argv[2], argv[3],
                          doReplace,
                          &argv[4], argc - 4,
                          false);
    if (stream == NULL) {
      printf("** error accessing host file\n");
      printf("** reason: %s\n", hostfs_lastErrmsg());
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
        printf("** reason: %s\n", hostfs_lastErrmsg());
        closeFile();
        nclose(stream);
        DONE(24);
      }
      len = readRecord(&eof);
    }
    closeFile();
    nclose(stream);
  } else if (strequiv(argv[1], "get")) {
    argc = interpretOptions(argc, argv, false);
    if (argc < 0) { DONE(4); }
    if (argc < 4) {
      printf("** missing arguments <fn ft> for subcommand get\n");
      DONE(4);
    }
    if (f_exists(argv[2], argv[3], "A") && !doReplace) {
      printf("** CMS file already exists, transfer aborted\n");
      DONE(24);
    }
    BULKSTREAM stream = hostfs_getfile(
                          argv[2], argv[3],
                          &argv[4], argc - 4,
                          true);
    if (stream == NULL) {
      printf("** unable to access host file, aborting\n");
      printf("** reason: %s\n", hostfs_lastErrmsg());
      DONE(24);
    }
    int rc = openFile(argv[2], argv[3], "A", false);
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
    doText = false;
    argc = interpretOptions(argc, argv, false);
    if (argc < 0) { DONE(4); }
    if (argc < 4) {
      printf("** missing arguments <fn ft> for subcommand getbin\n");
      DONE(4);
    }
    if (f_exists(argv[2], argv[3], "A") && !doReplace) {
      printf("** CMS file already exists, transfer aborted\n");
      DONE(24);
    }
    BULKSTREAM stream = hostfs_getfile(
                          argv[2], argv[3],
                          &argv[4], argc - 4,
                          false);
    if (stream == NULL) {
      printf("** unable to access host file, aborting\n");
      printf("** reason: %s\n", hostfs_lastErrmsg());
      DONE(24);
    }
    int rc = openFile(argv[2], argv[3], "A", false);
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

/*
** CMSFTPD.C  - FTP server for CMS based on NICOF's socket API
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module implements a simple single-session CMS FTP server simulating
** a hierarchical file system,
** with "simple" meaning:
**   => the control connection is raw TCP/IP, not TELNET
**   => not all FTP protocol commands of RFC-959 are implemented
**      (however most file related commands are available)
**   => login can be made required by specifying the password on the command
**      line (the username must then be the VM user running CMSFTPD)
**   => the program automatically terminates after the first client session
** with "hierarchical file system" meaning:
**   => the accessed disks and files of the current CMS user are serviced
**   => the virtual root of the file system is identified by the / symbol
**   => the file system is one directory level deep, with:
**      -> / is directory separation symbol
**      -> the accessed CMS minidisks are the directories under / (the root),
**         idenfified by their access letter
**      -> the files on a minidisk are identified with a . (dot) joining
**         the filename and filetype components of the file's id.
**      -> file names are returned in lower case by CMSFTPD, but handled as
**         case-insensitive items
**      -> example:
**           the CMS file PROFILE EXEC A is identified in the FTP interaction
**           as:
**                 /a/profile.exec
**      -> a modifier can be appended to the filename when uploading files
**         to specify the RECFM  resp. LRECL attributes or to overwrite
**         existing files, the optional modifier has the format:
**              {:|!}[{V|F}[nnn]]
**         with
**           : -> create but do not overwrite existing file
**           ! -> create file, overwriting an existing file
**           V -> give the file RECFM V
**           F -> give the file RECFM F
**           nnn -> give the file LRECL nnn (wrapping at column nnn)
**                  (must be 1 .. 255)
**         the default modifier is :V80
**         examples:
**            :v255 -> RECFM V, LRECL 255, not overwriting an existing file
**            !f72  -> RECFM F, LRECL 72, overwriting an existing file
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
 
 
/* default listen address / port for the FTP server */
#define SRV_LISTEN_ADDR "0.0.0.0"
#define SRV_LISTEN_PORT 21
 
/* the size for a transmission packet server(CMS) <=> client(outside) */
#define PACKETLEN 1400
 
/* CMS commands used when option -useCmsCmds was specified */
#define CMS_GET_DISKS_CMD  "QUERY DISK ( FIFO"
#define CMS_LIST_FILES_CMD "LISTFILE %s %s %s ( FIFO LABEL NOHEADER"
 
/* C includes */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
 
#include "cmssys.h"
 
/* utility functions borrowed from MECAFF */
#include "eeutil.h"
 
/* Socket-API for CMS resp. VM/370R6 Sixpack 1.2 */
#include "intrapi.h"
#include "nicofclt.h"
#include "socket.h"
 
/* minimal boolean data type, if not already defined (just to be sure) */
#ifndef true
typedef char bool;
#define true 1
#define false 0
#endif
 
/*
** Control data for a single FTP client session
*/
typedef struct _ftpSession {
  /* sockets for the session */
  SOCKET ctlSocket; /* client session/control socket */
  SOCKET psvSocket; /* server socket for passive mode */
 
  /* access management: username passed for login and login status */
  char notedUser[32];
  bool loggedIn;
 
  /* the message last used to tell about PASV mode (re-used if client
  ** re-issues a PASV cmd while we still have the psvSocket open */
  char psvMsg[80];
 
  /* ftp's client address to connect to, transmitted with a PORT cmd */
  bool havingActiveClientAddr;
  struct sockaddr_in clientAddr;
 
  /* is the state currently TYPE I ? */
  bool ftpTrfBinary;
 
  /* "current directory", with -1 => / */
  int currDisk;
 
  /* the file id to rename (FTP uses 2 separate commands for old / new name) */
  char renameFromFid[20];
  int renameDiskIdx;
 
  /* session management: simple linked list */
  struct _ftpSession *next;
} HANDLE, *H;
 
/*
** sockets for the FTP server
*/
static SOCKET srvSocket = -1; /* ftp server socket */
static SOCKET trfSocket = -1; /* socket for a single data transfer */
 
/* control information for CMSFTPD */
static bool verbose = false;       /* log interactions on CMS console */
static bool disksReadonly = false; /* treat all R/W disks as R/O */
static bool autoOverwrite = false; /* do not need ! modifier to replace */
static bool ignoreDashArgs = false;/* skip 1. parameter if begins with - ? */
static bool useCmsCommands = false;/* do LISTFILE / CP DISK ? */
static bool trfBinary = false;     /* is the current transfer binary mode ? */
 
/* client socket and session management */
static fd_set clientSocksSet; /* socket set wot watch, incl. our srvSocket */
static fd_set *clientSocks = &clientSocksSet;
static int clientSockCount = 0;
static int lastSockPlus1 = 0;
 
static H sessions = NULL; /* list of current client sessions */
 
/* initialize session management, putting the server socket into the set */
static void initClientSocks() {
  FD_ZERO(clientSocks);
  FD_SET(srvSocket, clientSocks);
  lastSockPlus1 = srvSocket + 1;
  clientSockCount = 0;
}
 
/* create and initialize the client session for a new client socket
*/
static H addClientSock(SOCKET sock) {
  FD_SET(sock, clientSocks);
  clientSockCount++;
  if (sock >= lastSockPlus1) { lastSockPlus1 = sock + 1; }
 
  H h = (H)malloc(sizeof(HANDLE));
  memset(h, '\0', sizeof(HANDLE));
  h->ctlSocket = sock;
  h->loggedIn = false;
  h->psvSocket = -1;
  h->havingActiveClientAddr = false;
  h->ftpTrfBinary = false;
  h->currDisk = -1;
  h->renameDiskIdx = -1;
  h->next = sessions;
  sessions = h;
 
  return h;
}
 
/* remove the session for the given socket
** returns 'true' if this was the last client session
*/
static bool dropClientSock(SOCKET sock) {
  H prev = NULL;
  H h = sessions;
  while(h && h->ctlSocket != sock) {
    prev = h;
    h = h->next;
  }
  if (h) {
    if (prev) { prev->next = h->next; } else { sessions = h->next; }
    if (h->psvSocket >= 0) { closesocket(h->psvSocket); }
    if (h->ctlSocket >= 0) { closesocket(h->ctlSocket); }
    free(h);
  }
 
  FD_CLR(sock, clientSocks);
  clientSockCount--;
 
  return (clientSockCount <= 0);
}
 
/* general date information possibly needed later */
static char* monthes[] = {
  "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
static char dtsLimit[16];
static char dtsDirDate[16];
 
/* initialize date and time handling
*/
static void getDateInfo() {
  time_t now;
  time(&now);
  struct tm *tmnow = localtime(&now);
  if (!tmnow) {
    printf("**** getdateinfo() => localtime() failed!\n");
    strcpy(dtsLimit, "9999-99-99");
    strcpy(dtsDirDate, "May 31  2014");
    return;
  }
  int day = tmnow->tm_mday;
  int mon = tmnow->tm_mon;
  int year = tmnow->tm_year + 1900;
  sprintf(dtsDirDate, "%s %02d  %04d", monthes[mon], day, year);
  sprintf(dtsLimit, "%04d-%02d-%02d", year - 1, mon+1, day);
}
 
/* get the "old style" date format for the CMS file date
*/
static char ftpFileDate[32];
static char* getFtpFileDate(char *cmsDate) {
  bool withTime = true;
  int i;
  int mon = ((cmsDate[5] - '0') * 10) + (cmsDate[6] - '0') - 1;
  for (i = 0; i < 10; i++) {
    if (cmsDate[i] < dtsLimit[i]) {
      withTime = false;
      break;
    } else if (cmsDate[i] > dtsLimit[i]) {
      break;
    }
  }
  if (mon < 0 || mon > 11) { mon = 0; }
  if (withTime) {
    sprintf(ftpFileDate, "%s %c%c %c%c%c%c%c",
      monthes[mon],
      cmsDate[8], cmsDate[9],
      cmsDate[11], cmsDate[12], cmsDate[13], cmsDate[14], cmsDate[15]);
  } else {
    sprintf(ftpFileDate, "%s %c%c  %c%c%c%c",
      monthes[mon],
      cmsDate[8], cmsDate[9],
      cmsDate[0], cmsDate[1], cmsDate[2], cmsDate[3]);
  }
  return ftpFileDate;
}
 
/* forward declaration: routine to send a status message back to the client */
static void sendCtrlMsg(H h, char *msg);
 
/* forward declaration: drop override definitions */
static void freeOverrides();
 
/* safe shutdown of the program */
static void sock_shutdown(int rc) {
  /* drop all session ressources */
  if (trfSocket >= 0) { closesocket(trfSocket); }
  while (sessions) { dropClientSock(sessions->ctlSocket); }
 
  /* close the sockets if opened, ignoring errors */
  if (srvSocket >= 0) { closesocket(srvSocket); }
 
  /* shutdown the NICOF API */
  nicofclt_deinit();
 
  /* drop overrides */
  freeOverrides();
 
  /* done  */
  __exit(rc);
}
 
/*
** management of overrides for file properties based on the filetype
*/
 
/* data structure for a single override */
typedef struct _override {
  char ft[8];  /* filetype in uppercase without trailing blanks */
  char term;   /* null char to ensure 'ft' termination */
  bool binary; /* do binary transfer (true) or ascii translation (false) */
  char recfm;  /* F or V */
  short lrecl; /* 1..255 */
  struct _override *next;
} FT_OVERRIDE, *FT_OVERRIDE_PTR;
 
/* list of loaded overrides */
static FT_OVERRIDE_PTR ftOverrides = NULL;
 
/* drop all loaded overrides */
static void freeOverrides() {
  while (ftOverrides) {
    FT_OVERRIDE_PTR next = ftOverrides->next;
    free(ftOverrides);
    ftOverrides = next;
  }
}
 
/* add an override to the list */
static void addOverride(char *ft, bool binary, char recfm, short lrecl) {
  FT_OVERRIDE_PTR fto = (FT_OVERRIDE_PTR) malloc(sizeof(FT_OVERRIDE));
  int i;
  memset((char*)fto, '\0', sizeof(FT_OVERRIDE));
  for (i = 0; i < 8; i++) {
    if (*ft && *ft != ' ') {
      fto->ft[i] = c_upper(*ft++);
    } else {
      break;
    }
  }
  fto->binary = binary;
  fto->recfm = c_upper(recfm);
  if (lrecl < 1) {
    fto->lrecl = 1;
  } else if (lrecl > 255) {
    fto->lrecl = 255;
  } else {
    fto->lrecl = lrecl;
  }
  fto->next = ftOverrides;
  ftOverrides = fto;
}
 
/* find the override for the given filetype */
static FT_OVERRIDE_PTR findOverride(char *ft) {
  FT_OVERRIDE_PTR fto = ftOverrides;
  int i;
 
  while (fto) {
    if (!sncmp(ft, fto->ft)) { return fto; }
    fto = fto->next;
  }
  return NULL;
}
 
/* create the filetype-overrides (hardcoded list for now, sorry) */
static void createOverrides() {
  addOverride("exec    ", false, 'V', 80);
  addOverride("c       ", false, 'V', 80);
  addOverride("h       ", false, 'V', 80);
  addOverride("parm    ", false, 'V', 80);
  addOverride("assemble", false, 'F', 80);
  addOverride("copy    ", false, 'F', 80);
  addOverride("macro   ", false, 'F', 80);
  addOverride("cobol   ", false, 'F', 80);
  addOverride("pli     ", false, 'F', 80);
  addOverride("pliopt  ", false, 'F', 80);
  addOverride("plc     ", false, 'F', 80);
  addOverride("fortran ", false, 'F', 80);
  addOverride("basic   ", false, 'F', 80);
  addOverride("basdata ", false, 'F', 80);
  addOverride("snobol4 ", false, 'F', 80);
  addOverride("pascal  ", false, 'F', 80);
  addOverride("script  ", false, 'V', 132);
  addOverride("direct  ", false, 'F', 80);
  addOverride("synonym ", false, 'F', 80);
  addOverride("memo    ", false, 'V', 80);
  addOverride("listing ", false, 'V', 132);
  addOverride("simple  ", false, 'V', 80);
  addOverride("hairy   ", false, 'V', 80);
  addOverride("ee      ", false, 'V', 80);
  addOverride("ind$map ", false, 'V', 80);
  addOverride("helpcmd ", false, 'V', 80);
  addOverride("helpcmd2", false, 'V', 80);
  addOverride("helpdbg ", false, 'V', 80);
  addOverride("helpdbg2", false, 'V', 80);
  addOverride("helpedt ", false, 'V', 80);
  addOverride("helpedt2", false, 'V', 80);
  addOverride("helpexc ", false, 'V', 80);
  addOverride("helpexc2", false, 'V', 80);
  addOverride("helpee  ", false, 'V', 80);
  addOverride("help$ee ", false, 'V', 80);
  addOverride("$help$  ", false, 'V', 80);
  addOverride("document", false, 'V', 80);
  addOverride("text    ", true , 'F', 80);
  addOverride("textlib ", true , 'F', 80);
  addOverride("maclib  ", true , 'F', 80);
  addOverride("map     ", false, 'F', 100);
}
 
/*
** string utilities
*/
 
/* get the next integer on the string
*/
static int getLineInt(char *s) {
  int val = 0;
  while(*s == ' ') { s++; }
  while(*s >= '0' && *s <= '9') {
    val = (val * 10) + (*s - '0');
    s++;
  }
  return val;
}
 
/* get the length of the token starting at 's'
*/
static int getLen(char *s) {
  int len = 0;
  while(*s != ' ' && *s != '\0') {
    len++;
    s++;
  }
  return len;
}
 
/* get the next token starting at 's'
*/
static char* getNextToken(char *s) {
  while (*s && *s != ' ') { s++; }
  if (!*s) { return NULL; }
  while (*s && *s == ' ') { s++; }
  if (!*s) { return NULL; }
  return s;
}
 
/*
** simulation of a simple hierarchical file system over CMS minidisks
*/
 
/* known infos about a minidisk */
typedef struct __minidisk {
  char letter;
  bool readonly;
  int blocksize;
} Minidisk;
 
static Minidisk disks[26]; /* 26 letters -> max. 26 minidisks */
static int diskCount = 0;  /* used entries in 'disks' */
 
/* disk enumeration callback: add an entry to 'disks' */
static void disklistCB(char *line, void *cbdata) {
  char *tok = line;         /* label */
  tok = getNextToken(tok);  /* dev */
  tok = getNextToken(tok);  /* letter */
  disks[diskCount].letter = *tok;
  tok = getNextToken(tok);  /* R/O or R/W */
  disks[diskCount].readonly = (disksReadonly || tok[2] != 'W');
  tok = getNextToken(tok);  /* cyls */
  tok = getNextToken(tok);  /* dev type */
  tok = getNextToken(tok);  /* block size */
  disks[diskCount].blocksize = getLineInt(tok);
 
  diskCount++;
}
 
/* initialize 'disks' with the currently accessed minidisks */
static void initDisks() {
  memset((char*)disks, '\0', sizeof(disks));
  if (useCmsCommands) {
    char line[133];
    /* drain stack (remove any user input present, like TERMINATE) */
    while(CMSstackQuery()) { CMSconsoleRead(line); }
    /* execute command */
    int rc = CMScommand(CMS_GET_DISKS_CMD, CMS_FUNCTION);
    /* get and process the lines stacked */
    if (CMSstackQuery()) { CMSconsoleRead(line); } /* skip header */
    while(CMSstackQuery()) {
      int len = CMSconsoleRead(line);
      line[len] = '\0';
      disklistCB(line, NULL);
    }
  } else {
    getDiskList(&disklistCB, NULL);
  }
}
 
/* get the index in 'disks' for the "minidisk directory" (disk letter) */
static int getDiskIdx(char disk) {
  int i;
  disk = c_upper(disk);
  for (i = 0; i < diskCount; i++) {
    if (disks[i].letter == disk) { return i; }
  }
  return -1;
}
 
/* interpret a path spec (including "." and ".." components)
** returns false if: invalid path, minidisk not accessed ...
*/
static bool getDirIdx(H h, char *path, int *idx) {
  int diskIndex = (*path == '/') ? -1 : h->currDisk;
  bool lastSep = false;
  while(*path) {
    if (*path == '/') {
      if (lastSep) { return false; } /* '//' ist invalid */
      lastSep = true;
      path++;
      continue;
    }
    lastSep = false;
    if (path[0] == '.') {
      if (path[1] == '.' && (path[2] == '/' || path[2] == '\0')) {
        /* '..' => one up is always 'root', as the tree is one level deep */
        diskIndex = -1;
        path += 2;
        continue;
      } else if (path[1] == '/' || path[1] == '\0') {
        /* '.' => no change */
        path++;
      }
    } else if (path[1] == '/' || path[1] == '\0') {
      /* potential disk letter */
      if (diskIndex >= 0) { return false; } /* not at root => no deeper */
      diskIndex = getDiskIdx(*path);
      if (diskIndex < 0) { return false; } /* disk not accessed */
      path++;
    } else {
      /* disk letter + not-a-separator => syntax error */
      return false;
    }
  }
 
  *idx = diskIndex;
  return true;
}
 
/* interpret 'fullPath' as file spec with optional path and creation modifier,
** returning the components in the other output parameters.
** if some error is found (structural error, invalid path or filespec, disk
** not accessed, ...), a corresponding error response is sent to the FTP
** client and 'false' is returned.
*/
static bool parseFullPath(
    H     h,
    char *fullPath,  /* in: path to analyse */
    int  *diskIdx,   /* out: disk index ~ filemode */
    char *fn,        /* out: filename */
    char *ft,        /* out: filetype */
    char *recfm,     /* out: RECFM from upload modifier */
    int  *lrecl,     /* out: LRECL from upload modifier */
    bool *replace,   /* out: replace-flag from upload modifier */
    bool *binary) {  /* out: binary-mode */
 
  int pathLen = strlen(fullPath);
  char line[65];
 
  /* plausibility check */
  if (pathLen > (sizeof(line) - 1)) {
    sendCtrlMsg(h, "501 Syntax error in file spec (path too long)");
    return false;
  }
 
  /* initialize */
  strcpy(line, fullPath);
  *replace = false;
  *binary = h->ftpTrfBinary;
 
  /* spots of interest in the path string */
  int lastSep = -1;
  int lastDot = -1;
  int lastColon = -1;
  int countDot = 0;
  int countColon = 0;
  int lastPos = pathLen - 1;
  int fnPos;
  int i;
 
  /* find the separation spots */
  for (i = 0; i <= lastPos; i++) {
    if (line[i] == '/') { countDot = 0; lastSep = i; }
    if (line[i] == '.') { countDot++; lastDot = i; }
    if (line[i] == ':') { countColon++; lastColon = i; }
    if (line[i] == '!') { countColon++; lastColon = i; *replace = true; }
  }
 
  /* basic structure check */
  if (countDot != 1
     || countColon > 1
     || lastSep > lastDot
     || (lastSep > -1 && lastDot < (lastSep + 1))
     || (lastColon > -1 && (lastSep > lastColon || lastColon < (lastDot + 1)))
     ) {
    sendCtrlMsg(h, "501 Syntax error in file spec");
    return false;
  }
 
  /* parse path if given */
  if (lastSep < 0 && h->currDisk >= 0) {
    *diskIdx = h->currDisk;
    fnPos = 0;
  } else if (lastSep < 1) {
    /* /fn.ft => implicitely use A if available*/
    if (disks[0].letter == 'A') {
      *diskIdx = 0;
      fnPos = lastSep + 1;
    } else {
      /* minidisk A not available */
      sendCtrlMsg(h, "553 Permission denied (default disk A is R/O)");
      return false;
    }
  } else {
    line[lastSep] = '\0';
    int newIdx;
    bool res = getDirIdx(h, line,&newIdx);
    line[lastSep] = '/';
    if (!res) {
      /* path syntax error */
      sendCtrlMsg(h, "501 Syntax error in path spec");
      return false;
    }
    *diskIdx = newIdx;
    fnPos = lastSep + 1;
  }
 
  /* parse fn.ft */
  if ((lastDot - fnPos) > 8) {
    sendCtrlMsg(h, "501 Syntax error in path spec (invalid filename)");
    return false;
  }
  if (lastColon < 0) {
    if (lastPos - lastDot > 8) {
      sendCtrlMsg(h, "501 Syntax error in path spec (invalid filetype)");
      return false;
    }
    memset(ft, '\0', 9);
    memcpy(ft, &line[lastDot + 1], lastPos - lastDot);
  } else {
    if (lastColon - 1 - lastDot > 8) {
      sendCtrlMsg(h, "501 Syntax error in path spec (invalid filetype)");
      return false;
    }
    memset(ft, '\0', 9);
    memcpy(ft, &line[lastDot + 1], lastColon - 1 - lastDot);
  }
  memset(fn, '\0', 9);
  memcpy(fn, &line[fnPos], lastDot - fnPos);
  s_upper(fn, fn);
  s_upper(ft, ft);
 
  /* parse recfm / lrecl if present */
  *recfm = 'V';
  *lrecl = 80;
  if (lastColon > 0 && lastPos > lastColon) {
    char c = line[lastColon + 1];
    if (c == 'v' || c == 'V') {
      *recfm = 'V';
    } else if (c == 'f' || c == 'F') {
      *recfm = 'F';
    } else {
      sendCtrlMsg(h, "501 Syntax error in file spec (invalid RECFM)");
      return false;
    }
    if (lastPos > lastColon + 1) {
      int val = 0;
      for (i = lastColon + 2; i <= lastPos; i++) {
        if (line[i] < '0' || line[i] > '9') {
          sendCtrlMsg(h, "501 Syntax error in file spec (invalid LRECL)");
          return false;
        }
        val = (val * 10) + (line[i] - '0');
      }
      if (val > 255) {
        sendCtrlMsg(h, "501 Syntax error in file spec (LRECL > 255)");
        return false;
      }
      *lrecl = val;
    }
  } else {
    FT_OVERRIDE_PTR fto = findOverride(ft);
    /*
    printf("-- using override for '%s' ( %c , %c , %d )\n",
      fto->ft, (fto->binary) ? 'I' : 'A', fto->recfm, fto->lrecl);
    */
    if (fto) {
      *binary = fto->binary;
      *recfm = fto->recfm;
      *lrecl = fto->lrecl;
    }
  }
 
  /* parse successfull */
  return true;
}
 
/*
** low-level communication client <-> server
*/
 
static char bufCtrl[PACKETLEN]; /* control data server <-> client */
static char bufData[PACKETLEN]; /* data transmission server <-> client */
static int  bufUsed; /* used count in 'bufData' */
 
/* add 'data' with 'datalen' unchanged to the buffer, transmitting the buffer
** content via 'trg' if necessary.
** rc : true if transmission via 'trg' failed (=> errno has the reason)
** requirement: 'dataLen' MUST be < PACKETLEN, or transmission is truncated!
*/
static bool transmitUnit(SOCKET trg, char *data, int dataLen) {
  if (!data || dataLen < 1) { return false; }
  if ((bufUsed + dataLen) > PACKETLEN && bufUsed > 0) {
    int rc = send(trg, bufData, bufUsed, 0);
    if (rc < 0) { return true; }
    bufUsed = 0;
  }
  if (dataLen > PACKETLEN) { dataLen = PACKETLEN; }
  memcpy(&bufData[bufUsed], data, dataLen);
  bufUsed += dataLen;
  return false;
}
 
/* add an EBCDIC string as a new line to the buffer (i.e. translating to ASCII
** and adding the ASCII line separator), transmmitting the buffer to 'sock'
** if necessary)
** rc : true if transmission via 'trg' failed (=> errno has the reason)
*/
static bool transmitAsciiLine(SOCKET trg, char *outline) {
  int len = strlen(outline);
  nicofclt_ebcdic2ascii(outline, len, outline);
  outline[len] = (char)0x0D;
  outline[len+1] = (char)0x0A;
  return transmitUnit(trg, outline, len + 2);
}
 
/* initialize transmission via buffer
*/
static void transmitBegin() {
  bufUsed = 0;
}
 
/* finalize transmission, sending the buffer's via 'trg' current content
** if required
** rc : true if transmission via 'trg' failed (=> errno has the reason)
*/
static bool transmitEnd(SOCKET trg) {
  if (bufUsed <= 0) { return false; }
  int rc = send(trg, bufData, bufUsed, 0);
  if (rc < 0) { return true; }
  bufUsed = 0;
  return false;
}
 
/* send a FTP state response via the control channel
*/
static void sendCtrlMsg(H h, char *msg) {
  if (verbose) { printf("  >>> %s\n", msg); }
  int msgLen = strlen(msg);
  nicofclt_ebcdic2ascii(msg, msgLen, bufCtrl);
  bufCtrl[msgLen] = (char)0x0D;
  bufCtrl[msgLen+1] = (char)0x0A;
  send(h->ctlSocket, bufCtrl, msgLen + 2, 0);
}
 
/* open the data connection socket, handling active (PORT) resp.
** passive (PASV) mode
** returns the open socket
**         or -1 if the connection could not be opened (the error message
**            is already sent to the client)
*/
static SOCKET openDataConnection(H h) {
  if (h->psvSocket < 0 && !h->havingActiveClientAddr) {
    sendCtrlMsg(h, "503 Bad sequence of commands (missing PORT or PASV)");
    return -1;
  }
 
  SOCKET sock;
  int savedErrno = 0;
 
  if (h->psvSocket >= 0) {
    sock = accept(h->psvSocket, NULL, NULL);
    savedErrno = errno;
  } else {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    savedErrno = errno;
    if (sock >= 0 &&
        connect(
          sock,
          (struct sockaddr*)&h->clientAddr, sizeof(h->clientAddr)) < 0) {
      savedErrno = errno;
      closesocket(sock);
      sock = -1;
    }
  }
 
  if (sock < 0) {
    char errMsg[120];
    sprintf(errMsg,
      "425 Can't open data connection (%s)",
      nicofsocket_errmsg(savedErrno));
    sendCtrlMsg(h, errMsg);
    return -1;
  }
 
  return sock;
}
 
/*
** functionality for transfering CMS files (RECV, STOR, APPE)
*/
 
static char filename[128];
static CMSFILE cmsfile;
static CMSFILE *f = NULL;
static char io_buffer[544]; /* 512 bytes buffer + 32 spare */
static int recordNum = 1;
 
/* build a FID string from the components fn ft fm */
static void buildFid(char *fid, char *fn, char *ft, char *fm) {
  strcpy(fid, "                  ");
  int i;
  char *p;
  for (i = 0, p = fn; i < 8 && *p != '\0'; i++, p++) {
    fid[i] = toupper(*p);
  }
  for (i = 8, p = ft; i < 16 && *p != '\0'; i++, p++) {
    fid[i] = toupper(*p);
  }
  p = fm;
  if (*p) { fid[16] = toupper(*p++); } else { fid[16] = 'A'; }
  if (*p) { fid[17] = toupper(*p); } else if (fid[16] != '*') { fid[17] = '1'; }
}
 
/* try to open the file and:
   - set global variable 'f' and return true if successfull
   - send an error status and return false if the file cannot be opened
*/
static int openFile(
    H h,
    char *fn, char *ft, char *fm,
    bool openForRead,
    char recfm, int lrecl,
    bool doAppend) {
  char msg[80];
 
  memset(io_buffer, '\0', sizeof(io_buffer));
 
  memset(filename, '\0', sizeof(filename));
  char *fid = filename;
  buildFid(fid, fn, ft, fm);
 
  CMSFILEINFO *fInfo;
  int rc = CMSfileState(fid, &fInfo);
  if (rc == 28) {
    if (openForRead) {
      f = NULL;
      sendCtrlMsg(h, "550 File not found, file transfer canceled");
      return rc;
    }
  } else if (rc != 0) {
    f = NULL;
    sprintf(msg,
      "550 Error opening file (rc = %d), file transfer canceled", rc);
    sendCtrlMsg(h, msg);
    return rc;
  } else if (!openForRead && !doAppend) {
    /* file exists (rc = 0) and overwrite => delete it */
    rc = CMSfileErase(fid);
    if (rc != 0 && rc != 28) {
      sprintf(msg,
        "550 Error erasing old file (RC = %d), file transfer canceled", rc);
      sendCtrlMsg(h, msg);
      return rc;
    }
  } else if (openForRead && fInfo->lrecl > 255) {
    sendCtrlMsg(h, "550 LRECL > 255 unsupported, file transfer canceled");
    return 4;
  }
 
  int firstLine = 1;
  if (!openForRead && doAppend) { firstLine = 0; }
  rc = CMSfileOpen(
         fid,
         io_buffer,
         (openForRead) ? sizeof(io_buffer)-1 : lrecl,
         recfm,
         1,         /* number of lines read/written per operation */
         firstLine, /* first line to read/write */
         &cmsfile);
  if (rc == 0 || rc == 28) {
    f = &cmsfile;
    if (openForRead) {
      recordNum = 0;
    } else {
      recordNum = (doAppend) ? 0 : 1;
    }
    return 0;
  } else if (rc == 20) {
    f = NULL;
    sendCtrlMsg(h, "550 Invalid file name, file transfer canceled");
    return rc;
  } else {
    f = NULL;
    sprintf(msg,
      "550 Error accessing file (RC = %d), file transfer canceled", rc);
    sendCtrlMsg(h, msg);
    return rc;
  }
 
  return 2;
}
 
/* close the CMS file */
static void closeFile() {
  if (f) { CMSfileClose(f); }
  f = NULL;
}
 
/* read a record of the file into 'io_buffer',
   set 'eof' to true if no more records are available,
   return the length of the record just read.
*/
static int readRecord(H h, bool *eof) {
  int len = 0;
  *eof = false;
  char msg[80];
  int rc = CMSfileRead(f, recordNum, &len);
  recordNum = 0;
  if (rc == 12) {
    *eof = true;
    len = 0;
  } else if (rc == 1) {
    sendCtrlMsg(h, "550 File not found, file transfer canceled");
    len = -1;
  } else if (rc == 14 || rc == 15) {
    sendCtrlMsg(h, "550 Invalid CMS file name, transfer canceled");
    len = -1;
  } else if (rc != 0) {
    printf(msg,
      "550 Error reading file (RC = %d), file transfer canceled", rc);
    sendCtrlMsg(h, msg);
    len = -1;
  } else if (!trfBinary) {
    char *p = &io_buffer[len-1];
    while (p > io_buffer && *p == ' ') { len--; p--; } /* drop blanks at end */
    io_buffer[len] = '\0';
  }
  return len;
}
 
/* write a record from 'io_buffer' with the specified record len,
   return true if writing failed
*/
static bool writeRecord(H h, int len, char recfm, int lrecl) {
  char fillChar = (trfBinary) ? '\0' : ' ';
 
  if (len < 1) { /* avoid a "non-write" for empty records */
    io_buffer[0] = fillChar;
    len = 1;
  }
  if (recfm == 'F' && len < lrecl) { /* fill fixed length records to LRECL */
    char *tail = &io_buffer[len];
    while(len < lrecl) {
      *tail++ = fillChar;
      len++;
    }
  }
 
  int rc = CMSfileWrite(f, recordNum, len);
  recordNum = 0;
  if (rc == 4 || rc == 5 || rc == 20 || rc == 21) {
    sendCtrlMsg(h, "550 Invalid CMS filename, transfer canceled");
    return true;
  } else if (rc == 10 || rc == 13 || rc == 19) {
    sendCtrlMsg(h, "550 CMS disk is full, transfer canceled");
    return true;
  } else if (rc == 12) {
    sendCtrlMsg(h, "550 CMS disk is read-only, transfer canceled");
    return true;
  } else if (rc != 0) {
    char msg[80];
    sprintf(msg,
      "550 Error writing CMS file (RC = %d), transfer canceled", rc);
    sendCtrlMsg(h, msg);
    return true;
  }
  return false;
}
 
/* implementation for the file transfer server => client (RETR)
** returns true is failed (the message is alredy sent to the client)
*/
static bool cmdRETR(H h, char *param) {
  char fn[9];
  char ft[9];
  int diskIdx;
  char recfm;
  int lrecl;
  bool repl;
  bool bin;
 
  if (!param || !*param) {
    sendCtrlMsg(h, "501 Syntax error in RETR command (no parameters)");
    return true;
  }
 
  if (!parseFullPath(h, param, &diskIdx, fn, ft, &recfm, &lrecl, &repl, &bin)) {
    return true;
  }
 
  /* open the file to read */
  char fm[2];
  fm[0] = disks[diskIdx].letter;
  fm[1] = '\0';
  int rc = openFile(h, fn, ft, fm, true, recfm, lrecl, false);
  if (rc != 0) { return true; }
 
  /* get the target socket */
  sendCtrlMsg(h, "150 Opening data connection");
  SOCKET trgSock = openDataConnection(h);
  if (trgSock < 0) { return true; }
 
  /* transfer the file content */
  trfBinary = bin;
  transmitBegin();
  bool eof;
  int len = readRecord(h, &eof);
  while(!eof) {
    if (trfBinary) {
      transmitUnit(trgSock, io_buffer, len);
    } else {
      transmitAsciiLine(trgSock, io_buffer);
    }
    len = readRecord(h, &eof);
  }
  transmitEnd(trgSock);
 
  /* close the file and the target socket */
  closeFile();
  sendCtrlMsg(h, "226 Closing data connection");
  closesocket(trgSock);
 
  /* successfully done */
  return false;
}
 
/* implementation for the file transfer client => server (STOR, APPE)
** returns true is failed (the message is alredy sent to the client)
*/
static bool cmdSTOR(H h, char *param, bool doAppend) {
  char fn[9];
  char ft[9];
  char fm[2];
  int diskIdx;
  char recfm;
  int lrecl;
  bool repl;
  bool bin;
 
  if (!param || !*param) {
    sendCtrlMsg(h, "501 Syntax error in STOR command (no parameters)");
    return true;
  }
 
  /* interpret target file spec parameter */
  if (!parseFullPath(h, param, &diskIdx, fn, ft, &recfm, &lrecl, &repl, &bin)) {
    return true;
  }
  fm[0] = disks[diskIdx].letter;
  fm[1] = '\0';
 
  /* check read-only disks and handle file existence */
  if (disks[diskIdx].readonly) {
    sendCtrlMsg(h, "553 Permission denied (disk read-only)");
    return true;
  }
  if (f_exists(fn, ft, fm) && !doAppend && !autoOverwrite && !repl) {
    sendCtrlMsg(h, "553 Permission denied (file exists)");
    return true;
  }
 
  /* open the file to write */
  int rc = openFile(h, fn, ft, fm, false, recfm, lrecl, doAppend);
  if (rc != 0) { return true; }
 
  /* get the target socket */
  sendCtrlMsg(h, "150 Opening data connection");
  trfSocket = openDataConnection(h);
  if (trfSocket < 0) { return true; }
 
  /* receive the file content */
  int recsWritten = 0;
  int recFilled = 0;
  trfBinary = bin;
  bufUsed = recv(trfSocket, bufData, PACKETLEN, 0);
  while (bufUsed > 0) {
    char *p = bufData;
    char *bound = &bufData[bufUsed];
    if (trfBinary) {
      while (p < bound) {
        io_buffer[recFilled] = *p++;
        recFilled++;
        if (recFilled >= lrecl) {
          writeRecord(h, recFilled, recfm, lrecl);
          recsWritten++;
          recFilled = 0;
        }
      }
    } else {
      while (p < bound) {
        char c = *p++;
        if (c == (char)0x0A) {
          /* end of line (LF) => write record */
          nicofclt_ascii2ebcdic(io_buffer, recFilled, io_buffer);
          writeRecord(h, recFilled, recfm, lrecl);
          recsWritten++;
          recFilled = 0;
        } else if (c != (char)0x0D) { /* ignore CR */
          io_buffer[recFilled] = c;
          recFilled++;
          if (recFilled >= lrecl) {
            nicofclt_ascii2ebcdic(io_buffer, recFilled, io_buffer);
            writeRecord(h, recFilled, recfm, lrecl);
            recsWritten++;
            recFilled = 0;
          }
        }
      }
    }
    bufUsed = recv(trfSocket, bufData, PACKETLEN, 0);
  }
  if (recFilled > 0) {
    if (!trfBinary) {
      nicofclt_ascii2ebcdic(io_buffer, recFilled, io_buffer);
    }
    writeRecord(h, recFilled, recfm, lrecl);
    recsWritten++;
  }
  if (recsWritten == 0 && !doAppend) {
    /* ensure the file exists */
    writeRecord(h, 0, recfm, lrecl);
  }
 
  /* close the file and the target socket */
  closeFile();
  sendCtrlMsg(h, "226 Closing data connection");
  closesocket(trfSocket);
 
  /* successfully done */
  return false;
}
 
/*
** functionality for listing directories (LIST, NLST)
*/
 
/* list the content of the file system "root" via the 'trg' socket
** rc: 'true' on some error (message already sent via control channel)
*/
static bool listRootDir(SOCKET trg, char* pattern, bool longFormat) {
  char pat = '*';
  int idx = -1;
  char line[90];
  int i;
 
  transmitBegin();
 
  if (pattern) {
    int patLen = strlen(pattern);
    if (patLen > 1) { pat = ' '; }
    if (patLen == 1) { pat = pattern[0]; }
    if (pat != '*') { idx = getDiskIdx(pat); }
  }
 
  for (i = 0; i < diskCount; i++) {
    if (i != idx && pat != '*') { continue; }
    if (longFormat) {
      sprintf(line,
        "d%s   1 root  root          0 %s %c",
        (disks[i].readonly) ? "r-xr-xr-x" : "rwxrwxrwx",
        dtsDirDate,
        c_lower(disks[i].letter));
    } else {
      sprintf(line, "%c", c_lower(disks[i].letter));
    }
    if (transmitAsciiLine(trg, line)) { return true; }
  }
 
  transmitEnd(trg);
 
  return false; /* no error */
}
 
/* callback for minidisk enumeration to create a long format (LIST) line
** for a single file, extracting the fileid components, timestamp and size
** information for reformatting to a 'ls -l' like format.
*/
static void filelistLongCB(char *line, void *cbdata) {
  int filelistIdx = (int)cbdata;
  char *fn = line;
  char *ft = &line[9];
 
  char *recfm = getNextToken(&line[20]);
  char *lrecl = getNextToken(recfm);
  char *recs = getNextToken(lrecl);
  char *blocks = getNextToken(recs);
  char *date = getNextToken(blocks);
  char *time = getNextToken(date);
 
  char tsBuffer[24];
  char *ts = (useCmsCommands)
           ? tsBuffer
           : date /*&line[45]*/; /* 16 chars long + ":00" */
  int  size = (*recfm == 'V')
            ? getLineInt(blocks) * disks[filelistIdx].blocksize
            : getLineInt(lrecl) * getLineInt(recs);
  char outline[90];
 
  memset(outline, '\0', sizeof(outline));
 
  if (useCmsCommands) { /* build the ISODATE from old CMS format */
    if (date[1] == '/') { date--; *date = '0'; }
    char *month = date;
    char *day = &date[3];
    char *year = &date[6];
    month[2] = '\0';
    day[2] = '\0';
    year[2] = '\0';
 
    if (time[1] == ':') { time--; *time = '0'; }
    time[5] = '\0';
 
    sprintf(ts,
      "%s%s-%s-%s %s",
      (getLineInt(year) < 60) ? "20" : "19", year, month, day, time);
  }
 
  ts[16] = '\0';
  sprintf(outline,
    "-%s   1 root  root    %7u %s ",
    (disks[filelistIdx].readonly) ? "r--r--r--" : "rw-rw-rw-",
    size,
    getFtpFileDate(ts));
  ts[16] = ' ';
  memcpy(&outline[strlen(outline)], fn, getLen(fn));
  strcat(outline, ".");
  memcpy(&outline[strlen(outline)], ft, getLen(ft));
  s_lower(outline, outline);
  transmitAsciiLine(trfSocket, outline);
}
 
/* callback for minidisk enumeration to create a short format (LIST) line
** for a single file, extracting the fileid components.
*/
static void filelistShortCB(char *line, void *cbdata) {
  int filelistIdx = (int)cbdata;
  char *fn = line;
  char *ft = &line[9];
  char outline[24];
  memset(outline, '\0', sizeof(outline));
  memcpy(&outline[strlen(outline)], fn, getLen(fn));
  strcat(outline, ".");
  memcpy(&outline[strlen(outline)], ft, getLen(ft));
  s_lower(outline, outline);
  transmitAsciiLine(trfSocket, outline);
}
 
/* enumerate CMS files based on the 'param'-spec, either in the NSLT (short)
** or LIST (long) output format dependign on 'longFormat'.
** if 'useCtlSocket' is true, the output is sent to the client through
** the control connection instead of a separate (active or passive) data
** connection.
** rc : true if transmission or enumeration failed somehow
**      (the error message is already sent)
*/
static bool cmdList(H h, char *param, bool longFormat, bool useCtlSocket) {
  char fnPat[9];
  char ftPat[9];
  int diskIdx;
  char recfm;
  int lrecl;
  bool repl;
  bool bin;
  bool listRoot = false;
 
  /* get the enumeration parameters */
  if (!param || !*param) {
    /* no parameter => list "current directory" */
    if (h->currDisk < 0) {
      listRoot = true;
    } else {
      diskIdx = h->currDisk;
      strcpy(fnPat, "*");
      strcpy(ftPat, "*");
    }
  } else {
    if (getDirIdx(h, param, &diskIdx)) {
      /* pure "directory" spec */
      if (diskIdx < 0) {
        /* list root */
        listRoot = true;
      } else {
        /* list a root directory => content of this minidisk */
        listRoot = false;
        strcpy(fnPat, "*");
        strcpy(ftPat, "*");
      }
    } else if (parseFullPath(h, param, &diskIdx, fnPat, ftPat, &recfm,
                             &lrecl, &repl, &bin)) {
      /* "file" spec, possibly with "directory" */
      char fm[2];
      fm[0] = disks[diskIdx].letter;
      fm[1] = '\0';
      char *msg = compileFidPattern(fnPat, ftPat, fm);
      if (msg) {
        char errMsg[120];
        sprintf(errMsg, "550 invalid pattern: %s", msg);
        sendCtrlMsg(h, errMsg);
        return true;
      }
    } else {
      /* invalid pattern spec */
      sendCtrlMsg(h, "550 invalid file pattern specification");
      return true;
    }
  }
 
  /* get the target socket */
  sendCtrlMsg(h, "150 Opening data connection");
  trfSocket = (useCtlSocket) ? h->ctlSocket : openDataConnection(h);
  if (trfSocket < 0) { return true; }
 
  /* transfer the enumeration data */
  bool result;
  char fmPat[2] = { 'A', '\0' };
  transmitBegin();
  if (listRoot) {
    fmPat[0] = '*'; /*rootPattern;*/
    result = listRootDir(trfSocket, fmPat, longFormat);
  } else {
    fmPat[0] = disks[diskIdx].letter;
    if (useCmsCommands) {
      char line[133];
      /* drain stack (remove any user input present, like TERMINATE) */
      while(CMSstackQuery()) { CMSconsoleRead(line); }
      /* execute command */
      char cmd[80];
      sprintf(cmd,
          CMS_LIST_FILES_CMD,
          fnPat, ftPat, fmPat);
      int rc = CMScommand(cmd, CMS_FUNCTION);
      /* get and process the lines stacked */
      if (CMSstackQuery()) { CMSconsoleRead(line); } /* skip header */
      while(CMSstackQuery()) {
        int len = CMSconsoleRead(line);
        line[len] = '\0';
        if (longFormat) {
          filelistLongCB(line, (void*)diskIdx);
        } else {
          filelistShortCB(line, (void*)diskIdx);
        }
      }
    } else {
      getFileList(
        (longFormat) ? &filelistLongCB : &filelistShortCB,
        (void*)diskIdx,
        fnPat,
        ftPat,
        fmPat);
    }
    result = false;
  }
  transmitEnd(trfSocket);
 
  /* done */
  sendCtrlMsg(h, "226 Closing data connection");
  if (!useCtlSocket) { closesocket(trfSocket); }
  trfSocket = -1;
  return result;
}
 
/*
** implementation of "simpler" non-trivial FTP commands
*/
 
/* change current "directory" (CWD)
*/
static bool cmdCWD(H h, char *param) {
  int diskIdx;
  if (!param || !*param || !getDirIdx(h, param, &diskIdx)) {
    sendCtrlMsg(h, "550 unable to change directory");
    return true;
  }
  h->currDisk = diskIdx;
  sendCtrlMsg(h, "250 CWD command successful");
  return false;
}
 
/* return current "directory" (PWD)
*/
static bool cmdPWD(H h) {
  char msg[80];
  if (h->currDisk < 0) {
    sprintf(msg, "257 \"/\""); /* "250 current directory: /" */
  } else {
    sprintf(msg, "257 \"/%c\"", /*"250 current directory: /%c" */
      c_lower(disks[h->currDisk].letter));
  }
  sendCtrlMsg(h, msg);
  return false;
}
 
/* enter passive mode (PASV)
*/
static bool cmdPASV(H h) {
  if (h->psvSocket >= 0) {
    /* still having a passive socket open -> send the last status message */
    sendCtrlMsg(h, h->psvMsg);
    return false;
  }
 
  struct sockaddr_in zeAddr;
  int zeLen = sizeof(zeAddr);
  struct sockaddr_in boundTo;
  int btLen = sizeof(boundTo);
 
  SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
  int savedErrno = errno;
 
  if (sock >= 0 ) {
    memset((char*)&zeAddr, '\0', sizeof(zeAddr));
    getsockname(h->ctlSocket, (struct sockaddr*)&zeAddr, &zeLen);
    zeAddr.sin_family = AF_INET;
    zeAddr.sin_port = 0;
    if (bind(sock, (struct sockaddr*)&zeAddr, sizeof(zeAddr)) >= 0
        && listen(sock, 2) >= 0
        && getsockname(sock, (struct sockaddr*)&boundTo, &btLen) >= 0) {
      unsigned char *p = (unsigned char*)&boundTo;
      sprintf(h->psvMsg,
        "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
        p[4], p[5], p[6], p[7], p[2], p[3]);
      sendCtrlMsg(h, h->psvMsg);
      h->psvSocket = sock;
      h->havingActiveClientAddr = false;
      return false;
    } else {
      savedErrno = errno;
      closesocket(sock);
    }
  }
 
  char ctlMsg[120];
  sprintf(ctlMsg,
    "500 unable to create PASV socket (%s)",
    nicofsocket_errmsg(savedErrno));
  sendCtrlMsg(h, ctlMsg);
  return true;
}
 
/* set the port for active data connections (PORT)
*/
static bool cmdPORT(H h, char *param) {
  if (!param || !*param) {
    sendCtrlMsg(h, "501 Syntax error in PORT command (no parameters)");
    return true;
  }
 
  char *c = param;
  int nibbles[6];
  int n;
  bool ok = true;
 
  for (n = 0; n < 6; n++) { nibbles[n] = 0; }
  n = 0;
  while(*c && *c == ' ') { c++; }
  while(*c && ok && n < 6) {
    if (*c >= '0' && *c <= '9') {
      nibbles[n] = (nibbles[n] * 10) + (*c - '0');
      if (nibbles[n] > 255) { ok = false; }
    } else if (*c == ',') {
      n++;
    } else {
      sendCtrlMsg(h, "501 Syntax error in PORT command (invalid char)");
      return true;
    }
    c++;
  }
  if (!ok) {
    sendCtrlMsg(h, "501 Syntax error in PORT command (nibble out of range)");
    return true;
  }
 
  h->clientAddr.sin_family = AF_INET;
  h->clientAddr.sin_addr.S_un.S_un_b.s_b1 = nibbles[0];
  h->clientAddr.sin_addr.S_un.S_un_b.s_b2 = nibbles[1];
  h->clientAddr.sin_addr.S_un.S_un_b.s_b3 = nibbles[2];
  h->clientAddr.sin_addr.S_un.S_un_b.s_b4 = nibbles[3];
  h->clientAddr.sin_port = (nibbles[4] << 8) | nibbles[5];
  h->havingActiveClientAddr = true;
  if (h->psvSocket >= 0) {
    closesocket(h->psvSocket);
    h->psvSocket = -1;
  }
  sendCtrlMsg(h, "200 PORT command successful");
}
 
/* set file transmission encoding (TYPE)
** supported encodings: I (binary) and A (ascii)
*/
static bool cmdTYPE(H h, char *param) {
  if (!param || !*param) {
    sendCtrlMsg(h, "501 Syntax error in TYPE command (no parameters)");
    return true;
  }
 
  if (*param == 'I' || *param == 'i') {
    h->ftpTrfBinary = true;
    sendCtrlMsg(h, "200 TYPE set to I");
  } else if (*param == 'A' || *param == 'a') {
    h->ftpTrfBinary = false;
    sendCtrlMsg(h, "200 TYPE set to A");
  } else {
    sendCtrlMsg(h, "504 Command TYPE not implemented for the given parameter");
    return true;
  }
  return false;
}
 
/* rename file
**  -> phase 1: set the file-id for the file to rename (RNFR)
**  -> phase 2: get the file-id for name to rename to and rename (RNTO)
*/
static const char* FNFT_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$+-_ ";
 
static bool checkInvalidFid(H h, char *fid) {
  int i;
  for (i = 0; i < 18; i++, fid++) {
    *fid = c_upper(*fid);
    if (!strchr(FNFT_CHARS, *fid)) {
      sendCtrlMsg(h, "553 Permission denied (syntax error in file id)");
      return true;
    }
  }
  return false;
}
 
static bool cmdRNFR(H h, char *param) {
  int diskIdx;
  char fnFrom[16];
  char ftFrom[16];
  char fmFrom[3];
  char rf;
  int lc;
  bool re;
  bool bin;
 
  h->renameDiskIdx = -1;
  if (!parseFullPath(h, param, &diskIdx, fnFrom, ftFrom, &rf, &lc, &re, &bin)) {
    return true;
  }
 
  if (diskIdx >= 0) {
    fmFrom[0] = disks[diskIdx].letter;
    h->renameDiskIdx = diskIdx;
  } else if (disks[0].letter == 'A') {
    fmFrom[0] = 'A';
    h->renameDiskIdx = 0;
  } else {
    sendCtrlMsg(h, "553 Permission denied (default disk A not accessed)");
    return true;
  }
  fmFrom[1] = '\0';
  buildFid(h->renameFromFid, fnFrom, ftFrom, fmFrom);
  if (checkInvalidFid(h, h->renameFromFid)) {
    h->renameDiskIdx = -1;
    return true;
  }
  sendCtrlMsg(h, "350 Requested file action pending further information");
  return false;
}
 
static bool cmdRNTO(H h, char *param) {
  int diskIdx;
  char fnTo[16];
  char ftTo[16];
  char fmTo[3];
  char rf;
  int lc;
  bool re;
  bool bin;
  char toFid[20];
 
  if (h->renameDiskIdx < 0) {
    sendCtrlMsg(h, "503 Bad sequence of commands (missing RNFR)");
    return true;
  }
 
  if (!parseFullPath(h, param, &diskIdx, fnTo, ftTo, &rf, &lc, &re, &bin)) {
    return true;
  }
 
  if (diskIdx < 0) { diskIdx = 0; }
  if (h->renameDiskIdx != diskIdx) {
    sendCtrlMsg(h, "550 Permission denied (disk change not allowed)");
    h->renameDiskIdx = -1;
    return true;
  }
  h->renameDiskIdx = -1;
 
  if (disks[diskIdx].readonly || disksReadonly) {
    sendCtrlMsg(h, "550 Permission denied (disk is readonly)");
    return true;
  }
 
  fmTo[0] = disks[diskIdx].letter;
  fmTo[1] = '\0';
  buildFid(toFid, fnTo, ftTo, fmTo);
  if (checkInvalidFid(h, toFid)) { return true; }
  int rc = CMSfileRename(h->renameFromFid, toFid);
  if (rc == 0) {
    sendCtrlMsg(h, "250 RNTO command successful");
    return false;
  } else {
    sendCtrlMsg(h, "550 Permission denied (rc != 0)");
    return true;
  }
}
 
/* delete file (DELE)
*/
static bool cmdDELE(H h, char *param) {
  int diskIdx;
  char fn[16];
  char ft[16];
  char fm[3];
  char rf;
  int lc;
  bool re;
  bool bin;
  char fid[20];
 
  if (!parseFullPath(h, param, &diskIdx, fn, ft, &rf, &lc, &re, &bin)) {
    return true;
  }
 
  if (diskIdx >= 0) {
    fm[0] = disks[diskIdx].letter;
  } else if (disks[0].letter == 'A') {
    fm[0] = 'A';
    diskIdx = 0;
  } else {
    sendCtrlMsg(h, "553 Permission denied (default disk A not accessed)");
    return true;
  }
 
  if (disks[diskIdx].readonly || disksReadonly) {
    sendCtrlMsg(h, "550 Permission denied (disk is readonly)");
    return true;
  }
 
  fm[1] = '\0';
  buildFid(fid, fn, ft, fm);
  if (checkInvalidFid(h, fid)) { return true; }
 
  int rc = CMSfileErase(fid);
  if (rc == 0) {
    sendCtrlMsg(h, "250 DELE command successful");
    return false;
  } else {
    sendCtrlMsg(h, "550 Permission denied (rc != 0)");
    return true;
  }
}
 
/*
** main FTP interpreter
*/
 
/* split a control line from the client into the components 'ftp command'
** and (optional) 'command parameter'
*/
static void splitCtlLine(char *s, char **cmd, char **param) {
  int state = 0; /* 0 = before-cmd; 1 = in-cmd; 2 = before-param; 3 = param */
  char *cmdStart = NULL;
  char *paramStart = NULL;
  char *lastNonBlank = NULL;
 
  if (!s) { s = ""; }
 
  while (*s) {
    char c = *s;
    if (c == '\r' || c == '\n') { *s = '\0'; break; }
    if (c == '\t') { c = ' '; }
    if (state == 0 && c != ' ') {
      state = 1;
      cmdStart = s;
    } else if (state == 1 && c == ' ') {
      state = 2;
      *s = '\0';
    } else if (state == 2 && c != ' ') {
      state = 3;
      paramStart = s;
      lastNonBlank = s;
    } else if (state == 3 && c != ' ') {
      lastNonBlank = s;
    }
    s++;
  }
  if (lastNonBlank) { lastNonBlank[1] = '\0'; }
 
  if (cmdStart) { s_upper(cmdStart, cmdStart); }
  *cmd = cmdStart;
  *param = paramStart;
}
 
/* wait for the socket to show activity or for the user to have entered
** the 'terminate' command (user input is checked at 1 second intervals)
** returns 'true' if the user entered the 'terminate' command
*/
static bool waitForSocket(fd_set *activeSet) {
  char consoleBuffer[133];
  struct timeval tv;
 
  tv.tv_sec = 1;
  tv.tv_usec = 0;
  int count = selectX(
                 lastSockPlus1,
                 clientSocks, NULL, NULL,
                 activeSet, NULL, NULL,
                 &tv);
  while (count == 0) {
    while(CMSstackQuery()) {
      int cbLen = CMSconsoleRead(consoleBuffer);
      if (cbLen > 132) { cbLen = 132; }
      consoleBuffer[cbLen] = '\0';
      if (sncmp(consoleBuffer, "terminate") == 0) {
        printf("** shutting down FTP server on user request\n");
        return true;
      }
    }
    count = selectX(
                 lastSockPlus1,
                 clientSocks, NULL, NULL,
                 activeSet, NULL, NULL,
                 &tv);
  }
  return (count < 0);
}
 
/* read a line from the client via the control channel
** returns the received line in EBCDIC or NULL if the connection is lost
*/
static char* getCtlLine(H h) {
  int rest = sizeof(bufCtrl) - 1;
  char *buf = bufCtrl;
 
  memset(bufCtrl, 0, sizeof(bufCtrl));
  while(1) {
    int len = recv(h->ctlSocket, buf, rest, 0);
    if (len < 0) {
      return NULL;
    }
    if (len == 0) { break; }
    rest -= len;
    buf += len;
    if (strstr(bufCtrl, "\r")) { break; }
    if (rest < 2) { break; }
  }
  nicofclt_ascii2ebcdic(bufCtrl, strlen(bufCtrl), bufCtrl);
  return bufCtrl;
}
 
 
static char* skipSpuriousOptions(char* arg) {
  if (!ignoreDashArgs) { return arg; }
  if (*arg != '-') { return arg; } /* not an option argument */
 
  char *s = arg;
 
  while (*s && *s != ' ') { s++; } /* find end of 1. parameter */
  if (!*s) { return s; }           /* string end => no 2. parameter => done */
 
  while (*s && *s == ' ') { s++; } /* find end of white space */
 
  return s;
}
 
static char* usage(char *pname) {
  printf("Usage: %s <options>\n", pname);
  printf("with <options>:\n");
  printf(" -h <hostname>   -> bind control to <hostname>\n");
  printf(" -p <port>       -> listen on port <port> (21)\n");
  printf(" -pwd <password> -> require login as current user and <password>\n");
  printf(" -ro             -> treat all minidisks as read-only\n");
  printf(" -replace        -> automatically overwrite existing files\n");
  printf(" -override       -> use filetype dep. defaults instead of V80\n");
  printf(" -ignoredashargs -> ignore 1. param to FTP cmds starting with -\n");
  printf(" -usecmscmds     -> use LISTFILE and Q DISK instead of builtins\n");
  printf(" -v              -> print commands and responses on console\n");
  printf("(enter TERMINATE to stop CMSFTPD while waiting for the client\n");
  printf("connection or for a FTP command from the client)\n");
  __exit(2);
  return NULL;
}
 
/* macros to help checking command line parameters ('a': current argv index) */
#define _is(_parm) (sncmp((_parm), argv[a]) == 0)
#define _getvalue() ((++a < argc) ? argv[a] : usage(argv[0]))
 
/* macros to simplify the coding for the FTP command switching */
#define _select(_cmp) { char *__selectcmp = (_cmp); while(1) { if (0) {
#define _when(_val) break; } if (strcmp(__selectcmp,(_val))==0) {
#define _ifnot(_req) break; } if (!(_req)) {
#define _else() break; } if (1) {
#define _endselect() break; } } }
 
/* read and process a single FTP command, returning 'true' if done
** with this client
*/
static bool processSingleCmd(
    H     h,
    char *reqUser,
    char *reqPwd) {
 
  bool done = false;
  char *cmd;
  char *param;
 
  char *ctlLine = getCtlLine(h);
  splitCtlLine(ctlLine, &cmd, &param);
  if (verbose) {
    printf("<<< %s %s\n",
      cmd, (!param) ? "" : (sncmp(cmd,"PASS")) ? param : "XXXX");
  }
 
  /* no command? (~ empty line) => implicitely done */
  if (!cmd) { return true; }
 
  _select(cmd);
 
    _when("USER")
      if (reqPwd) {
        if (strlen(param) >= 8) {
          param[8] = '\0';
        }
        strcpy(h->notedUser, param);
      }
      h->loggedIn = false;
      sendCtrlMsg(h, "331 User name noted, need password.");
 
    _when("PASS")
      if (reqPwd && (sncmp(h->notedUser, reqUser) || strcmp(param, reqPwd))) {
        sendCtrlMsg(h, "530 Not logged in.");
        h->loggedIn = false;
      } else {
        h->loggedIn = true;
        sendCtrlMsg(h, "230 User logged in, proceed.");
      }
      memset(h->notedUser, '\0', sizeof(h->notedUser));
 
    _when("NOOP")
      sendCtrlMsg(h, "200 Command okay.");
 
    _when("QUIT")
      sendCtrlMsg(h, "221 Good bye, thank you for using CMSFTPD.");
      return true;
 
    _when("SYST")
      sendCtrlMsg(h, "215 VM/370 CMSFTPD V0.1");
 
    _when("PORT")
      cmdPORT(h, param);
 
    _when("PASV")
      cmdPASV(h);
 
    _when("PWD")
      cmdPWD(h);
 
    _when("XPWD") /* MS-Windows FTP sends XPWD instead of PWD ... */
      cmdPWD(h);
 
    _ifnot(h->loggedIn)
      sendCtrlMsg(h, "530 Not logged in.");
 
    _when("CWD")
      cmdCWD(h, param);
 
    _when("CDUP")
      cmdCWD(h, "/");
 
    _when("LIST")
      param = skipSpuriousOptions(param);
      cmdList(h, param, true, false);
 
    _when("NLST")
      param = skipSpuriousOptions(param);
      cmdList(h, param, false, false);
 
    _when("STAT")
      param = skipSpuriousOptions(param);
      cmdList(h, param, true, true);
 
    _when("RETR")
      cmdRETR(h, param);
 
    _when("STOR")
      cmdSTOR(h, param, false);
 
    _when("APPE")
      cmdSTOR(h, param, true);
 
    _when("TYPE")
      cmdTYPE(h, param);
 
    _when("DELE")
      cmdDELE(h, param);
 
    _when("MKD")
      sendCtrlMsg(h, "550 Permission denied");
 
    _when("XMKD") /* MS-Windows FTP sends XMKD instead of MKD ... */
      sendCtrlMsg(h, "550 Permission denied");
 
    _when("RMD")
      sendCtrlMsg(h, "550 Permission denied");
 
    _when("XRMD") /* MS-Windows FTP sends XRMD instead of RMD ... */
      sendCtrlMsg(h, "550 Permission denied");
 
    _when("RNFR")
      /* part 1 of rename: 'from' name */
      cmdRNFR(h, param);
 
    _when("RNTO")
      /* part 2 of rename: 'to' name and rename the file */
      cmdRNTO(h, param);
 
    _else()
      sendCtrlMsg(h, "502 Command not implemented.");
 
  _endselect();
 
  return false;
}
 
/* the main line code for the CMS FTP server */
int main(int argc, char **argv) {
 
  char *listenAddr = SRV_LISTEN_ADDR;
  unsigned short listenPort = SRV_LISTEN_PORT;
 
  char currUser[9];
  char *reqUser = NULL;
  char *reqPwd = NULL;
 
  /* interpret command line parameters */
  int a;
  for (a = 1; a < argc; a++) {
    if (_is("-h")) {
      listenAddr = _getvalue();
    } else if (_is("-p")) {
      int port = atoi(_getvalue());
      if (port < 1 || port > 65535) {
        printf("** invalid listen port specified\n");
        return 4;
      }
      listenPort = (unsigned short)port;
    } else if (_is("-pwd")) {
      reqPwd = _getvalue();
    } else if (_is("-ro")) {
       disksReadonly = true;
    } else if (_is("-replace")) {
      autoOverwrite = true;
    } else if (_is("-v")) {
      verbose = true;
    } else if (_is("-override")) {
      createOverrides();
    } else if (_is("-ignoredashargs")) {
      ignoreDashArgs = true;
    } else if (_is("-usecmscmds")) {
      useCmsCommands = true;
    } else {
      usage(argv[0]);
    }
  }
 
  /* initialize all components */
  char x00data[32];
  diagx00(x00data, sizeof(x00data));
  char systype[9];
  memcpy(systype, x00data, 8);
  systype[8] = '\0';
  if (strcmp(systype, "VM/370  ") && !useCmsCommands) {
    if (verbose) { printf("### Not a VM/370 system, forcing -useCmsCmds\n"); }
    useCmsCommands = true;
  }
  getDateInfo();
  initDisks();
  nicofclt_init();
  if (reqPwd) { /* a password is required => remember the username for USER */
    memcpy(currUser, &x00data[16], 8);
    currUser[8] = '\0';
    currUser[getLen(currUser)] = '\0';
    reqUser = currUser;
    if (verbose) { printf("### required user = '%s'\n", reqUser); }
  }
 
  /* resolve the hostname to bind to */
  struct hostent *h = gethostbyname(listenAddr);
  if (!h) {
    printf("** bind to name '%s' could not be resolved\n", listenAddr);
    printf("** (h_errno = %d (%s)\n", h_errno, nicofsocket_errmsg(h_errno));
    return 4;
  }
 
  /* create the server socket */
  struct sockaddr_in zeAddr;
  zeAddr.sin_family = AF_INET;
  zeAddr.sin_port = htons(listenPort);
  zeAddr.sin_addr.s_addr = *((ncs_ulong*)h->h_addr);
 
  srvSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (srvSocket < 0) {
    printf(
        "** socket() failed: errno = %d (%s)\n",
        errno, nicofsocket_errmsg(errno));
    sock_shutdown(20);
  }
 
  int retConn = bind(srvSocket, (struct sockaddr*)&zeAddr, sizeof(zeAddr));
  if (bind(srvSocket, (struct sockaddr*)&zeAddr, sizeof(zeAddr)) < 0) {
    printf("** bind() failed: errno = %d (%s)\n",
      errno, nicofsocket_errmsg(errno));
    sock_shutdown(21);
  }
  if (verbose || listenAddr == SRV_LISTEN_ADDR){
    struct sockaddr_in myAddr;
    int myAddrLen = sizeof(myAddr);
    int r = getsockname(srvSocket, (struct sockaddr*)&myAddr, &myAddrLen);
    if (r >= 0) {
      printf("%slistening on %d.%d.%d.%d:%d\n",
        (verbose) ? "### start " : "",
        myAddr.sin_addr.S_un.S_un_b.s_b1,
        myAddr.sin_addr.S_un.S_un_b.s_b2,
        myAddr.sin_addr.S_un.S_un_b.s_b3,
        myAddr.sin_addr.S_un.S_un_b.s_b4,
        myAddr.sin_port);
    } else {
      printf("%slistening on unknown local address\n(errno = %d: %s)\n",
        (verbose) ? "### start " : "", errno, nicofsocket_errmsg(errno));
    }
  }
 
  if (listen(srvSocket, 2) < 0) {
    printf("** listen() failed, errno = %d (%s)\n",
      errno, nicofsocket_errmsg(errno));
    sock_shutdown(22);
  }
 
  initClientSocks();
 
  /* wait for client to connect and send commands */
  fd_set activeSet;
  bool done = waitForSocket(&activeSet);
  while (!done) {
    if (FD_ISSET(srvSocket, &activeSet)) {
      struct sockaddr clientAddr;
      int clientAddrLen = sizeof(clientAddr);
      SOCKET ctlSocket = accept(srvSocket, &clientAddr, &clientAddrLen);
      if (ctlSocket < 0) {
        printf("** accept() failed: errno = %d (%s)\n",
          errno, nicofsocket_errmsg(errno));
        if (clientSockCount <= 0) {
          /* no client connected, so terminate FTP server */
          sock_shutdown(99);
        } else {
          /* some client connected, so eait for "natural" end of FTP server,
          ** but no longer accept new control connections */
          FD_CLR(srvSocket, clientSocks);
          closesocket(srvSocket);
          srvSocket = -1;
        }
      } else {
        if (verbose) { printf("CMDFTPD client connection opened\n"); }
        H h = addClientSock(ctlSocket);
        sendCtrlMsg(h, "220 CMSFTPD ready");
      }
    }
 
    H h = sessions;
    while (h) {
      if (FD_ISSET(h->ctlSocket, &activeSet)) {
        bool doneWithSession = processSingleCmd(h, reqUser, reqPwd);
        if (doneWithSession) {
          SOCKET sock = h->ctlSocket;
          h = h->next;
          done |= dropClientSock(sock);
        } else {
          h = h->next;
        }
      } else {
        h = h->next;
      }
    }
 
    if (!done) { done = waitForSocket(&activeSet); }
  }
 
  /* done */
  if (verbose) {
    printf("Shutting down %s after session terminated\n", argv[0]);
  }
  sock_shutdown(0);
}

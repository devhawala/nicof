/*
*** NHFSCOMN.H  - common functionality for NHFS and RNHFS
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
 
#include "nhfscomn.h"
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
** Uppercase for EBCDIC
*/
 
/* EBCDIC 'bracket' charset uppercase translation */
 
const char tbl_2upr[] = {
0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
 
0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
 
0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
 
0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
 
0x40, 0x41, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
0x68, 0x69, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
 
0x50, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
0x78, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
 
0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
 
0x80, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
 
0x80, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
0xC8, 0xC9, 0x8A, 0x8B, 0xAC, 0xBA, 0x8E, 0x8F,
 
0x90, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
0xD8, 0xD9, 0x9A, 0x9B, 0x9E, 0x9D, 0x9E, 0x9F,
 
0xA0, 0xA1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
0xE8, 0xE9, 0xAA, 0xAB, 0xAC, 0xAD, 0x8E, 0xAF,
 
0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
 
0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
0xC8, 0xC9, 0xCA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
 
0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
0xD8, 0xD9, 0xDA, 0xFB, 0xFC, 0xFD, 0xFE, 0xDF,
 
0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
 
0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};
 
/* test if 2 strings are case-insensitive equal */
bool strequiv(char *s1, char *s2) {
  if (!s1 && !s2) { return true; }
  if (!s1 || !s2) { return false; }
  if (strlen(s1) != strlen(s2)) { return false; }
 
  while(*s1) {
    if (toupper(*s1++) != toupper(*s2++)) { return false; }
  }
  return true;
}
 
/*
** CMS file I/O
*/
 
bool doText = true;
bool doReplace = false;
char recfm = 'V';
int  lrecl = 80;
bool doAppend = false;
static char filename[128];
static CMSFILE cmsfile;
static CMSFILE *f = NULL;
char io_buffer[544]; /* 512 bytes buffer + 32 spare */
static int recordNum = 1;
 
/* build a FID string from the components fn ft fm */
void buildFid(char *fid, char *fn, char *ft, char *fm) {
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
 
/* check if the file 'fn ft fm' exists */
bool f_exists(char *fn, char *ft, char *fm) {
  char fid[19];
  CMSFILEINFO *fInfo;
 
  buildFid(fid, fn, ft, fm);
  int rc = CMSfileState(fid, &fInfo);
  return (rc == 0);
}
 
/* try to open the file and:
   - set global variable 'f' and return true if successfull
   - send an error status and return false if the file cannot be opened
*/
int openFile(
    char *fn, char *ft, char *fm,
    bool openForRead) {
  memset(io_buffer, '\0', sizeof(io_buffer));
 
  memset(filename, '\0', sizeof(filename));
  char *fid = filename;
  buildFid(fid, fn, ft , fm);
 
  CMSFILEINFO *fInfo;
  int rc = CMSfileState(fid, &fInfo);
  if (rc == 28) {
    if (openForRead) {
      f = NULL;
      printf("CMS file '%s' not found: file transfer canceled\n", fid);
      return rc;
    }
  } else if (rc != 0) {
    f = NULL;
    printf(
      "Error accessing file '%s' (RC = %d): file transfer canceled\n",
      fid, rc);
    return rc;
  } else if (!openForRead && !doAppend) {
    /* file exists (rc = 0) and overwrite => delete it */
    rc = CMSfileErase(fid);
    if (rc != 0 && rc != 28) {
      printf("Error erasing old file (RC = %d): file transfer canceled\n", rc);
      return rc;
    }
  } else if (openForRead && fInfo->lrecl > 255) {
    printf("LRECL > 255 unsupported: file transfer canceled\n");
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
    printf("Invalid file name '%s': file transfer canceled\n", fid);
    return rc;
  } else {
    f = NULL;
    printf(
      "Error accessing file '%s' (RC = %d): file transfer canceled\n",
      fid, rc);
    return rc;
  }
 
  return 2;
}
 
/* close the CMS file */
void closeFile() {
  if (f) { CMSfileClose(f); }
  f = NULL;
}
 
/* read a record of the file into 'io_buffer',
   set 'eof' to true if no more records are available,
   return the length of the record just read.
*/
int readRecord(bool *eof) {
  int len = 0;
  *eof = false;
  char msg[80];
  int rc = CMSfileRead(f, recordNum, &len);
  recordNum = 0;
  if (rc == 12) {
    *eof = true;
    len = 0;
  } else if (rc == 1) {
    printf("CMS file '%s' not found\n", filename);
    len = -1;
  } else if (rc == 14 || rc == 15) {
    printf("Invalid CMS file name '%s', transfer canceled\n", filename);
    len = -1;
  } else if (rc != 0) {
    printf(
      "Error reading file '%s' (RC = %d): file transfer canceled\n",
      filename, rc);
    len = -1;
  } else if (doText) {
    char *p = &io_buffer[len-1];
    while (p > io_buffer && *p == ' ') { len--; p--; } /* strip blanks a end */
    io_buffer[len] = '\0';
  }
  return len;
}
 
/* write a record from 'io_buffer' with the specified record len,
   return true if writing failed
*/
bool writeRecord(int len) {
  char fillChar = (doText) ? ' ' : '\0';
 
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
    printf("Incorrect CMS filename '%s', transfer canceled\n", filename);
    return true;
  } else if (rc == 10 || rc == 13 || rc == 19) {
    printf("CMS disk is full, file transfer canceled\n");
    return true;
  } else if (rc == 12) {
    printf("CMS disk is read-only, file transfer canceled\n");
    return true;
  } else if (rc != 0) {
    char msg[80];
    printf("Error writing CMS file (RC = %d): file transfer canceled\n", rc);
    return true;
  }
  return false;
}
 

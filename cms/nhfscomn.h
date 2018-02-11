/*
** NHFSCOMN.H  - common functionality for NHFS and RNHFS
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2012
** Released to the public domain.
*/
 
#ifndef __NHFSCOMN_included
#define __NHFSCOMN_included
 
#ifndef true
typedef char bool;
#define true ((bool)1)
#define false ((bool)0)
#endif
 
/*
** Uppercase for EBCDIC
*/
 
/* EBCDIC 'bracket' charset uppercase translation */
 
extern const char tbl_2upr[];
 
#define toupper(c) tbl_2upr[c]
 
/* test if 2 strings are case-insensitive equal */
extern bool strequiv(char *s1, char *s2);
 
/*
** CMS file I/O
*/
 
extern bool doText;
extern bool doReplace;
extern char recfm;
extern int  lrecl;
extern bool doAppend;
/*extern char filename[128];*/
/*extern CMSFILE cmsfile;*/
/*extern CMSFILE *f = NULL;*/
extern char io_buffer[544]; /* 512 bytes buffer + 32 spare */
/*extern int recordNum = 1;*/
 
/* build a FID string from the components fn ft fm */
extern void buildFid(char *fid, char *fn, char *ft, char *fm);
 
/* check if the file 'fn ft fm' exists */
extern bool f_exists(char *fn, char *ft, char *fm);
 
/* try to open the file and:
   - set global variable 'f' and return true if successfull
   - send an error status and return false if the file cannot be opened
*/
extern int openFile(
    char *fn, char *ft, char *fm,
    bool openForRead);
 
/* close the CMS file */
extern void closeFile();
 
/* read a record of the file into 'io_buffer',
   set 'eof' to true if no more records are available,
   return the length of the record just read.
*/
extern int readRecord(bool *eof);
 
/* write a record from 'io_buffer' with the specified record len,
   return true if writing failed
*/
extern bool writeRecord(int len);
 
#endif /* #ifndef __NHFSCOMN_included */

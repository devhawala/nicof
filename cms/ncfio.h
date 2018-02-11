/*
** NCFIO.H     - NICOF stream operation definitions
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module defines the operations on data streams that are created on the
** java side and provided on the VM/370 side by a custom service.
**
** The operations on streams are inspired from the STDIO.H operations on
** FILE* streams.
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
 
 
#ifndef NCFIO_INCLUDED
#define NCFIO_INCLUDED
 
/* the object type representing a remote data stream
*/
typedef struct _ncfb_bulkstream {} BULKSTR_STRUCT, *BULKSTREAM;
 
/* read up to 'bufferLen - 1' text bytes from the stream up to a line end,
** leaving the line end on the string and terminating the string with a
** null char.
** The remote bytes are assumed to be ASCII and are translated to EBCDIC
**
** Returns the buffer pointer if any bytes were written or NULL if the stream
** ended (EOF or the like).
*/
#define ngets(buffer, bufferLen, stream) \
  ngetstr(buffer, bufferLen, true, stream)
 
/* Read up to 'bufferLen - 1' text bytes from the stream up to a line end,
** removing the line end on the string and terminating the string with a
** null char.
** The remote bytes are assumed to be ASCII and are translated to EBCDIC.
**
** Returns the buffer pointer if any bytes were written or NULL if the stream
** ended (EOF or the like).
*/
#define ngetline(buffer, bufferLen, stream) \
  ngetstr(buffer, bufferLen, false, stream)
 
/* Read up to 'bufferLen - 1' text bytes from the stream up to a line end,
** leaving or removing the line end on the string depending in 'keepNL' and
** terminating the string with a null char.
** The remote bytes are assumed to be ASCII and are translated to EBCDIC.
**
** Returns the buffer pointer if any bytes were written or NULL if the stream
** ended (EOF or the like).
*/
extern char* ngetstr(
    char *buffer,
    uint bufferLen,
    bool keepNL,
    BULKSTREAM stream);
 
/* Read a binary block of data (without any character set conversion) with
** up to 'bufferLen' bytes. If 'noWait == false', the operation may block until
** the buffer can be filled up to 'bufferLen' bytes; with 'noWait == true',
** only the data immediately available may be returned (with the semantics of
** "immediately" being left to the service providing the stream).
**
** Returns the number of bytes copied to 'buffer'.
*/
extern uint nread(
    void *buffer,
    uint bufferLen,
    bool noWait,
    BULKSTREAM stream);
 
/* Write a text string to the stream, converting the local EBCDIC bytes to
** ASCII.
**
** Returns true if writing was at least started (i.e. false if the stream was
** in a state forbidding a write when the function was called).
*/
#define nputs(string,stream) \
  nputstr(string,false,stream)
 
/* Write a text string to the stream, converting the local EBCDIC bytes to
** ASCII and appending a newline.
**
** Returns true if writing was at least started or false if the stream was
** in a state forbidding a write when the function was called.
*/
#define nputline(string,stream) \
  nputstr(string,true,stream)
 
/* Write a text string to the stream, converting the local EBCDIC bytes to
** ASCII and appending a newline if 'appendNewLine == true'.
**
** Returns true if writing was at least started or false if the stream was
** in a state forbidding a write when the function was called.
*/
extern bool nputstr(
    char *string,
    bool appendNewline,
    BULKSTREAM stream);
 
/* Write a binary data block to the stream.
**
** Returns 'bufLen' if writing was at least started or 0 if the stream was in
** a state forbidding a write when the function was called.
*/
extern uint nwrite(
    void *buffer,
    uint bufLen,
    BULKSTREAM stream);
 
/* Check if a source stream has reached the end of stream and has no more
** data to read.
*/
extern bool neof(BULKSTREAM stream);
 
/* Transmit all data currently buffered in a sink stream to the remote
** service.
*/
extern void nflush(BULKSTREAM stream);
 
/* Close the stream.
*/
extern void nclose(BULKSTREAM stream);
 
#define NERR_NOERROR          0
#define NERR_NOT_SOURCE      20
#define NERR_NOT_SINK        21
#define NERR_EOF           1000
#define NERR_READERROR     1001
#define NERR_WRITEERROR    1002
#define NERR_NOTTEXTSTREAM 1011
#define NERR_NOTBINSTREAM  1012
 
/* Return the error code of the last failed operation for the stream. The
** stream specified state codes are the NERR_* constants, but other codes (for
** example for communication errors) can also be returned.
*/
extern int nerror(BULKSTREAM stream);
 
/* Return the error message associated with the error of the last failed
** operation for the stream.
*/
extern char* nerrmsg(BULKSTREAM stream);
 
#endif

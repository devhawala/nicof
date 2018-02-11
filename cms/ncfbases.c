/*
** NCFBASES.C  - NICOF level-one base services implementation
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This module implements the base services allowing to communicate with the
** level one dispatcher on the java proxy side. The provided functionality
** is defined in the header files:
** - NCFBASES.H -> invoke function on the base service and custom services
** - NCFIO.H    -> standard operations on streams returned by custome services
**
** Using this module requires the NICOF infastructure to be already
** initialized by our client!
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
 
 
#include "ncfbases.h"
#include "ncfio.h"
 
/*
** rc <- ncfbasesvc_resolve(serviceName, (out) serviceId)
**
** resolve the service name to its ID on the external process
*/
int ncfb_001(const char *serviceName, short *serviceId) {
  char svcNameAscii[SERVICENAME_MAXLEN];
  int svcNameLen = strlen(serviceName);
  if (svcNameLen > SERVICENAME_MAXLEN) { svcNameLen = SERVICENAME_MAXLEN; }
  nicofclt_ebcdic2ascii(serviceName, svcNameLen, svcNameAscii);
 
  *serviceId = -1;
 
  request_handle h = nicofclt_createRequest(0, 0); /* uw1: svc = 0, cmd = 0 */
  int dataRc = nicofclt_setRequestData(h, svcNameLen, svcNameAscii);
  if (dataRc != 0) {
  /*printf("## ncfb_001: setRequestData -> rc = %d\n", dataRc);*/
    return dataRc;
  }
 
  int sendRc = nicofclt_sendRequest(h);
  if (sendRc != 0) {
  /*printf("## ncfb_001: sendRequest -> rc = %d\n", sendRc);*/
    return sendRc;
  }
 
  int recvRc = nicofclt_waitForResponse(h);
  if (recvRc != 0) {
  /*printf("## ncfb_001: waitForResponse -> rc = %d\n", recvRc);*/
    return recvRc;
  }
 
  uint w1;
  uint w2;
  nicofclt_getResponseUserWords(h, &w1, &w2);
  if (w1 == 0) { /* is RC == 0 ? */
    short svcId = (short)(w2 & 0xFFFF);
    *serviceId = svcId;
    return 0;
  }
  return (int)w1;
}
 
 
#define CHAR_CR ((unsigned char)0x0D)
#define CHAR_LF ((unsigned char)0x25)
 
 
/*
** RC <- ncfbasesvc_invoke_sync(
**         svcId,
**         svcCmd,
**         inCtlWord,
**         inData,
**         inDataLen,
**         outCtlWord,
**         outData,
**         outDataLen,
**         dataFlags);
*/
int ncfb_020(
        short   svcId,
        short   svcCmd,
        int     inCtlWord,
        void   *inData,
        uint    inDataLen,
        int    *outCtlWord,
        void   *outData,
        uint   *outDataLen,
        byte    dataFlags) {
  request_handle h;
  int rc = ncfbasesvc_invoke_begin(
                       &h,
                       svcId,
                       svcCmd,
                       inCtlWord,
                       inData,
                       inDataLen,
                       dataFlags);
  if (rc != 0) { return rc; }
  return ncfbasesvc_invoke_end(
                       h,
                       outCtlWord,
                       outData,
                       outDataLen,
                       dataFlags);
}
 
 
/*
** RC <- ncfbasesvc_invoke_begin(
**         hndl,
**         svcId,
**         svcCmd,
**         inCtlWord,
**         inData,
**         inDataLen,
**         dataFlags);
*/
int ncfb_021(
        request_handle *hndl,
        short   svcId,
        short   svcCmd,
        int     inCtlWord,
        void   *inData,
        uint    inDataLen,
        byte    dataFlags) {
  *hndl = NULL;
  int ctlw1 = (svcId << 16) | svcCmd;
  request_handle h = nicofclt_createRequest(ctlw1, inCtlWord);
  if (inData && inDataLen > 0) {
    int dataRc = nicofclt_setRequestDataXlate(h, inDataLen, inData,
                    (dataFlags & INDATA_TEXT) ? e2a : NULL);
    if (dataRc != 0) {
      nicofclt_freeRequest(h);
    /*printf("## ncfb_021: setRequestData -> rc = %d\n", dataRc);*/
      return dataRc;
    }
  }
 
  int sendRc = nicofclt_sendRequest(h);
  if (sendRc != 0) {
    nicofclt_freeRequest(h);
  /*printf("## ncfb_021: sendRequest -> rc = %d\n", sendRc);*/
    return sendRc;
  }
 
  *hndl = h;
  return 0;
}
 
 
/*
** RC <- ncfbasesvc_invoke_end(
**         h,
**         outCtlWord,
**         outData,
**         outDataLen,
**         dataFlags);
*/
extern int ncfb_022(
              request_handle h,
              int    *outCtlWord,
              void   *outData, /* must have space for 2048 bytes ! */
              uint   *outDataLen,
              byte    dataFlags) {
  int recvRc = nicofclt_waitForResponse(h);
  if (recvRc != 0) {
  /*printf("## ncfb_022: waitForResponse -> rc = %d\n", recvRc);*/
    return recvRc;
  }
 
  uint w1;
  uint w2;
  nicofclt_getResponseUserWords(h, &w1, &w2);
  if (outCtlWord != NULL) { *outCtlWord = (int)w2; }
 
  if (outData && outDataLen) {
    int dataRc = nicofclt_getResponseDataXlate(h, 2048, outData, outDataLen,
                    (dataFlags & OUTDATA_TEXT) ? a2e : NULL);
    if (dataRc != 0) {
      nicofclt_freeRequest(h);
    /*printf("## ncfb_022: getResponseData -> rc = %d\n", dataRc);*/
      return dataRc;
    }
  }
 
  nicofclt_freeRequest(h);
 
  return (int)w1;
}
 
/*
** implementation of stream oriented reading/writing data like STDIO
*/
 
#define STREAM_BUFFER_LEN 2048  /* must be in sync with NICOF */
 
/* the representation of a single NCFIO-stream behind BULKSTREAM */
typedef struct _ncfb_bulkstream_private {
  uint streamId;
  bool isText;
  bool isSourceStream;
  bool lastLineWasBufferEnd;
  uint streamState;
  int  nerr;
  int  commrc;
  uint bufLen;
  uint bufPos;
  char buffer[STREAM_BUFFER_LEN];
  } BULKSTREAMSTRUCTPRIV, *BULKSTREAMPRIV;
 
/* states of the remote stream */
 
#define STATE_OK 0
 
#define STATE_SOURCE_CLOSED -1
#define STATE_SOURCE_ENDED -2
#define STATE_SOURCE_READ_ERROR -3
 
#define STATE_SINK_CLOSED -1
#define STATE_SINK_MEDIA_FULL -2
#define STATE_SINK_WRITE_ERROR -3
 
#define NERR_COMMERROR        1
 
/* how does the platform where the outside proxy runs represent line ends ? */
static int lineEndMode = -1; /* 0 = LF-CR ; 1 = LF ; 2 = CR ; 3 = CR-LF */
 
/* convert a streamId of the outside proxy in a bulk stream
*/
BULKSTREAM ncfbid2s(int streamId, bool isSourceStream, bool isText) {
  BULKSTREAMPRIV str = (BULKSTREAMPRIV)malloc(sizeof(BULKSTREAMSTRUCTPRIV));
  str->streamId = streamId;
  str->isSourceStream = isSourceStream;
  str->isText = isText;
  str->lastLineWasBufferEnd;
  str->streamState = STATE_OK;
  str->nerr = NERR_NOERROR;
  str->commrc = 0;
  str->bufLen = (isSourceStream) ? 0 : STREAM_BUFFER_LEN;
  str->bufPos = 0;
 
  if (isText && lineEndMode < 0) {
    int ctlWord;
    int rc = ncfbasesvc_invoke_sync(
               0,         /* svcId : base services */
               1,         /* svcCmd : get env info */
               0,         /* inCtlWord : ignored */
               NULL,      /* inData : no data */
               0,         /* inDataLen : no data */
               &ctlWord,  /* outCtlWord : the env data */
               NULL,      /* outData : not needed */
               NULL,      /* outDataLen : not needed */
               0);        /* dataFlags : ignored */
    lineEndMode = (ctlWord & 0x00000300) >> 8;
  }
 
  return (BULKSTREAM)str;
}
 
 
/* Read up to 'bufferLen - 1' text bytes from the stream up to a line end,
** leaving or removing the line end on the string depending in 'keepNL' and
** terminating the string with a null char.
** The remote bytes are assumed to be ASCII and are translated to EBCDIC.
**
** Returns the buffer pointer if any bytes were written or NULL if the stream
** ended (EOF or the like).
*/
char* ngetstr(
    char *buffer,
    uint bufferLen,
    bool keepNL,
    BULKSTREAM stream) {
  BULKSTREAMPRIV str = (BULKSTREAMPRIV)stream;
 
  if (!str->isSourceStream) {
    str->nerr = NERR_NOT_SOURCE;
    return NULL;
  }
  if (str->nerr == NERR_EOF) { return NULL; }
  if (str->bufPos >= str->bufLen && str->streamState == STATE_SOURCE_ENDED) {
    str->nerr == NERR_EOF;
    return NULL;
  }
 
  if (!str->isText) {
    str->nerr = NERR_NOTTEXTSTREAM;
    return NULL;
  }
 
  bool skipLineEnd = str->lastLineWasBufferEnd;
  str->lastLineWasBufferEnd = false;
 
  str->nerr = NERR_NOERROR;
 
  if (bufferLen < 2) {
    *buffer = '\0';
    return buffer;
  }
 
  char *limit = &buffer[bufferLen - 1];
  char *curr = buffer;
  while (curr < limit) {
    if (str->bufPos >= str->bufLen) {
      if (str->streamState != 0) { /* last read's state in this loop */
        limit = curr;
        continue;
      }
      str->commrc = ncfbasesvc_invoke_sync(
               0, /* svcId = base services */
               101, /* svcCmd = BULKSRC_READ */
               str->streamId, /* inCtlWord */
               NULL, /* inData, irrelevant for source streams */
               0, /* inDataLen, irrelevant for source streams */
               &str->streamState, /* putCtlWord = state of the stream */
               str->buffer, /* outData = our read buffer */
               &str->bufLen, /* outDataLen = transferred bytes */
               OUTDATA_TEXT /* dataFlags = do ASCII->EBCDIC translation */
               );
      if (str->commrc != 0) { str->nerr = NERR_COMMERROR; }
      str->bufPos = 0;
      if (str->bufLen == 0) {
        if (curr == buffer && str->streamState != 0) { return NULL; }
        limit = curr;
        continue;
      }
    }
 
    unsigned char c = str->buffer[str->bufPos++];
    if (c == CHAR_LF && lineEndMode != 2 && skipLineEnd) {
      skipLineEnd = false;
    } else if (c == CHAR_CR && lineEndMode == 2 && skipLineEnd) {
      skipLineEnd = false;
    } else if (c == CHAR_LF) {
      if (lineEndMode != 2) {
        if (keepNL) { *curr++ = (char)c; }
        limit = curr;
      }
    } else if (c == CHAR_CR) {
      if (lineEndMode == 2) {
        if (keepNL) { *curr++ = CHAR_LF; }
        limit = curr;
      }
    } else {
      *curr++ = (char)c;
      skipLineEnd = false;
    }
  }
  *curr = '\0';
  str->lastLineWasBufferEnd = (!keepNL && curr == &buffer[bufferLen - 1]);
  return buffer;
}
 
 
/* Read a binary block of data (without any character set conversion) with
** up to 'bufferLen' bytes. If 'noWait == false', the operation may block until
** the buffer can be filled up to 'bufferLen' bytes; with 'noWait == true',
** only the data immediately available may be returned (with the semantics of
** "immediately" being left to the service providing the stream).
**
** Returns the number of bytes copied to 'buffer'.
*/
uint nread(
    void *buffer,
    uint bufferLen,
    bool noWait,
    BULKSTREAM stream) {
  BULKSTREAMPRIV str = (BULKSTREAMPRIV)stream;
 
  if (!str->isSourceStream) {
    str->nerr = NERR_NOT_SOURCE;
    return 0;
  }
  if (str->nerr == NERR_EOF) { return 0; }
  if (str->bufPos >= str->bufLen && str->streamState == STATE_SOURCE_ENDED) {
    str->nerr == NERR_EOF;
    return 0;
  }
 
  if (str->isText) {
    str->nerr = NERR_NOTBINSTREAM;
    return 0;
  }
 
  str->nerr = NERR_NOERROR;
 
  int count = 0;
  char *dest = (char*)buffer;
  while (count < bufferLen) {
    if (str->bufPos >= str->bufLen) {
      if (str->streamState != 0) { break; } /* last read's state in this loop */
      short cmd = (noWait) ? 102 : 101; /* READNOWAIT vs. READ */
      str->commrc = ncfbasesvc_invoke_sync(
               0, /* svcId = base services */
               cmd, /* svcCmd */
               str->streamId, /* inCtlWord */
               NULL, /* inData, irrelevant for source streams */
               0, /* inDataLen, irrelevant for source streams */
               &str->streamState, /* putCtlWord = state of the stream */
               str->buffer, /* outData = our read buffer */
               &str->bufLen, /* outDataLen = transferred bytes */
               DATA_BINARY /* dataFlags = no translation */
               );
      if (str->commrc != 0) { str->nerr = NERR_COMMERROR; }
      str->bufPos = 0;
      if (str->bufLen == 0) {
        break; /* the reason should be in streamState */
      }
    }
    *dest++ = str->buffer[str->bufPos++];
    count++;
  }
 
  return count;
}
 
static nflush_inner(BULKSTREAMPRIV str) {
  if (str->bufPos == 0) { return; }
  uint flags = (str->isText) ? INDATA_TEXT : DATA_BINARY;
  str->commrc = ncfbasesvc_invoke_sync(
           0, /* svcId = base services */
           201, /* svcCmd : BULKSINK_WRITE */
           str->streamId, /* inCtlWord */
           str->buffer, /* inData = our write buffer */
           str->bufPos, /* inDataLen = used buffer length */
           &str->streamState, /* outCtlWord = state of the stream */
           NULL, /* outData, irrelevant for writing */
           NULL, /* outDataLen, irrelevant for writing */
           flags /* dataFlags */
           );
  if (str->commrc != 0) { str->nerr = NERR_COMMERROR; }
  str->bufPos = 0;
  str->bufLen = STREAM_BUFFER_LEN;
}
 
#define _putc(str,c,errval) \
  str->buffer[str->bufPos++] = c; \
  if (str->bufPos == str->bufLen) { \
    nflush_inner(str); \
  } \
  if (str->streamState != STATE_OK) { return errval; }
 
 
/* Write a text string to the stream, converting the local EBCDIC bytes to
** ASCII and appending a newline if 'appendNewLine == true'.
**
** Returns true if writing was at least started or false if the stream was
** in a state forbidding a write when the function was called.
*/
bool nputstr(
    char *string,
    bool appendNewline,
    BULKSTREAM stream) {
  BULKSTREAMPRIV str = (BULKSTREAMPRIV)stream;
 
  if (str->isSourceStream) {
    str->nerr = NERR_NOT_SINK;
    return false;
  }
  if (str->nerr != NERR_NOERROR) { return false; }
  if (str->streamState != STATE_OK) {
    str->nerr == NERR_WRITEERROR;
    return false;
  }
 
  if (!str->isText) {
    str->nerr = NERR_NOTTEXTSTREAM;
    return false;
  }
 
  str->nerr = NERR_NOERROR;
 
  char *s = string;
  while (*s) {
    _putc(str, *s++, false);
  }
 
  if (appendNewline) {
    if (lineEndMode == 1) {
      _putc(str, CHAR_LF, false);
    } else if (lineEndMode == 2) {
      _putc(str, CHAR_CR, false);
    } else if (lineEndMode == 3) {
      _putc(str, CHAR_CR, false);
      _putc(str, CHAR_LF, false);
    } else {
      _putc(str, CHAR_LF, false);
      _putc(str, CHAR_CR, false);
    }
  }
 
  return true;
}
 
 
/* Write a binary data block to the stream.
**
** Returns 'bufLen' if writing was at least started or 0 if the stream was in
** a state forbidding a write when the function was called.
*/
uint nwrite(void *buffer, uint bufLen, BULKSTREAM stream) {
  BULKSTREAMPRIV str = (BULKSTREAMPRIV)stream;
 
  if (str->isSourceStream) {
    str->nerr = NERR_NOT_SINK;
    return 0;
  }
  if (str->nerr != NERR_NOERROR) { return 0; }
  if (str->streamState != STATE_OK) {
    str->nerr == NERR_WRITEERROR;
    return 0;
  }
 
  if (str->isText) {
    str->nerr = NERR_NOTBINSTREAM;
    return 0;
  }
 
  str->nerr = NERR_NOERROR;
 
  char *s = (char*)buffer;
  int i;
  for (i = 0; i < bufLen; i++) {
    _putc(str, *s++, i);
  }
 
  return bufLen;
}
 
 
/* Check if a source stream has reached the end of stream and has no more
** data to read.
*/
bool neof(BULKSTREAM stream) {
  BULKSTREAMPRIV str = (BULKSTREAMPRIV)stream;
  if (!str->isSourceStream) { return false; }
  if (str->bufPos < str->bufLen) { return false; }
  /*if (str->nerr != NERR_NOERROR) { return true; }*/
  return (str->streamState == STATE_SOURCE_ENDED
          || str->streamState == STATE_SOURCE_CLOSED);
}
 
 
/* Transmit all data currently buffered in a sink stream to the remote
** service.
*/
void nflush(BULKSTREAM stream) {
  BULKSTREAMPRIV str = (BULKSTREAMPRIV)stream;
 
  if (str->isSourceStream) {
    str->nerr = NERR_NOT_SINK;
    return;
  }
  if (str->nerr != NERR_NOERROR) { return; }
  if (str->streamState != STATE_OK) {
    str->nerr == NERR_WRITEERROR;
    return;
  }
  nflush_inner(str);
}
 
 
/* Close the stream.
*/
void nclose(BULKSTREAM stream) {
  BULKSTREAMPRIV str = (BULKSTREAMPRIV)stream;
 
  if ((str->isSourceStream && str->streamState == STATE_SOURCE_CLOSED)
      || (!str->isSourceStream && str->streamState == STATE_SINK_CLOSED)) {
    return;
  }
 
  if (!str->isSourceStream) {
    nflush_inner(str);
  }
 
  int cmd = (str->isSourceStream) ? 100 : 200;
  int rc = ncfbasesvc_invoke_sync(
              0, /* svcId = base services */
              cmd, /* svcCmd = BULKSRC_CLOSE or BULKSINK_CLOSE */
              str->streamId, /* inCtlWord = stream id */
              NULL, /* inData, irrelevant  */
              0, /* inDataLen, irrelevant */
              &str->streamState, /* putCtlWord = state of the stream after op */
              NULL, /* outData = irrelevant */
              NULL, /* outDataLen = irrelevant */
              DATA_BINARY /* dataFlags = irrelevant */
              );
  free(stream);
}
 
 
/* get the error message text for the passed return/error code
*/
char* ncfb_000(int rc) {
  if (rc <= -1000000 || rc >= 0) { return nicofclt_errmsg(rc); }
 
  switch(rc) {
    case ERR_INVALID_SERVICE :
      return "invalid level-1 service";
    case ERR_SVC_INVALIDRESULT :
      return "invalid result from level-1 service";
    case ERR_SVC_EXCEPTION :
      return "exception thrown by level-1 service";
    case ERR_BASESVC_INVCMD :
      return "invalid command for level-1 service";
 
    case NEW_BULK_SOURCE :
      return "new bulk source available";
    case ERR_BULK_SOURCE_INVALID :
      return "invalid bulk source";
 
    case NEW_BULK_SINK :
      return "new bulk sink available";
    case ERR_BULK_SINK_INVALID :
      return "invalid sink source";
    default:
      return nicofclt_errmsg(rc);
  }
}
 
 
/* Return the error code of the last failed operation for the stream. The
** stream specified state codes are the NERR_* constants, but other codes (for
** example for communication errors) can also be returned.
*/
int nerror(BULKSTREAM stream) {
  BULKSTREAMPRIV str = (BULKSTREAMPRIV)stream;
  if (str->nerr == NERR_COMMERROR) { return str->commrc; }
  return str->nerr;
}
 
 
/* Return the error message associated with the error of the last failed
** operation for the stream.
*/
char* nerrmsg(BULKSTREAM stream) {
  return ncfbasesvc_errmsg(nerror(stream));
}
 

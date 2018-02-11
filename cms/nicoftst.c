/*
** NICOFCLT.C  - NICOF client interface (mass) test program
**
** This file is part of NICOF (Non-Invasive COmmunication Facility)
** for VM/370 R6 "SixPack".
**
** This program test the NICOF communication path from a client VM (where this
** program is run) to the outside proxy and back to the client VM.
**
** At the outside proxy, a special "level-0" handler must be registered, which
** echos all ingoing packets in a special way (the user words are returned
** unchanged, the response data block is the same as the request data block,
** except that byte positions 0, 17, 253, 2047 are replaced by (EBCDIC) 'A').
**
** The program is invoked with the optional parameters:
**   count    :: number of packets to be sent (default: 1)
**   fillchar :: character to fill the data buffer with (default: 'X')
**
** Each request has a data block of 2048 bytes (the maximum length) filled with
** fillchar. The userword1 is the request current number counter, the userword2
** is also filled with the fillchar.
** Each response coming back is checked if it meets the expectations (see above)
** before the next request is sent.
**
** While the request-response loop is running, the program can receive SMSGs
** which are dumped to the console.
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
 
#include "nicofclt.h"
 
 
/** write out the content of a SMSG received
*/
static void handleSmsg(DBLWORD vmcmuse, char *smsg) {
  char fromUser[9];
  memcpy(fromUser, vmcmuse.chars, 8);
  fromUser[9] = '\0';
  printf("SMSG from: '%s':\n%s\n", fromUser, smsg);
}
 
/* the buffer for the data block to be sent */
static char zeData[2048];
 
int main(int argc, char **argv) {
 
  /* the variables modified by the command line parameters */
  int maxReqCount = 1;
  char zeFillChar = 'X';
  uint uw2 = 0x33333333;
 
  /* interpret the command line parameters and give help if necessary */
  if (argc > 1) {
    int count = atoi(argv[1]);
    if (count > 0) {
      maxReqCount = count;
    } else {
      printf("usage: %s [ count  [ initchar ] ]\n", argv[0]);
      return 4;
    }
  }
  if (argc > 2 && *argv[2]) {
    zeFillChar = *argv[2];
  }
 
  /* initialize the data block and 2. user word */
  memset(zeData, zeFillChar, sizeof(zeData));
  memset(&uw2, zeFillChar, sizeof(uw2));
 
  /* initialize the NICOF client API */
  nicofclt_initForSMSGs(&handleSmsg);
 
  /* issue the requests until the specified count is reached */
  int reqCount = 0;
  while (reqCount++ < maxReqCount) {
    /* allocate a new request and set its data block */
    request_handle h = nicofclt_createRequest(reqCount, uw2);
    int dataRc = nicofclt_setRequestData(h, sizeof(zeData), zeData);
    if (dataRc != 0) {
      printf("#### setRequestData -> rc = %d\n", dataRc);
    }
 
    /* send the request */
    int sendRc = nicofclt_sendRequest(h);
    if (sendRc != 0) {
      printf("##\n## unable to send after %d requests\n##\n", reqCount);
      break;
    }
 
    /* wait for a response and check it is the request sent */
    request_handle x;
    int waitAnyRc = nicofclt_waitForAnyAvailable(&x);
    if (waitAnyRc != 0) {
      printf("##\n## waitForAnyAvailable() -> %d\n## msg: %s\n##\n",
        waitAnyRc, nicofclt_errmsg(waitAnyRc));
      break;
    }
    if (x != h) {
      printf("#### waitForAnyAvailable returned a different Request !!!\n");
    }
 
    /* get the response */
    int waitRespRc = nicofclt_waitForResponse(h);
    if (waitRespRc != 0) {
      printf("##\n## waitForResponse() -> %d\n## msg: %s\n##\n",
        waitRespRc, nicofclt_errmsg(waitAnyRc));
      break;
    }
 
    /* check the userword1 for identity */
    uint w1;
    uint w2;
    nicofclt_getResponseUserWords(x, &w1, &w2);
    if (w1 != reqCount) {
      printf("##### response: userWord1 != reqCount (%d != %d)\n",
        w1, reqCount);
     }
     if (w2 != uw2) {
      printf("##### response: userWord1 != uw2 (%d != %d)\n",
        w2, uw2);
     }
 
    /* check that the response data block meets the expectations */
    uint respLen;
    nicofclt_getResponseDataLength(x, &respLen);
    if (respLen != sizeof(zeData)) {
      printf(" !! diff: req[%d] :: requestLength = %d => responseLength = %d\n",
        reqCount, sizeof(zeData), respLen);
    }
    char b;
    nicofclt_getResponseDataByte(x, 0, &b);
    if (b != 'A') {
      printf("!! req[%d] :: response[0] != 'A' (0x%02x = '%c')\n",
        reqCount, b, b);
    }
    nicofclt_getResponseDataByte(x, 17, &b);
    if (b != 'A') {
      printf("!! req[%d] :: response[17] != 'A' (0x%02x = '%c')\n",
        reqCount, b, b);
    }
    nicofclt_getResponseDataByte(x, 253, &b);
    if (b != 'A') {
      printf("!! req[%d] :: response[253] != 'A' (0x%02x = '%c')\n",
        reqCount, b, b);
    }
    nicofclt_getResponseDataByte(x, 2047, &b);
    if (b != 'A') {
      printf("!! req[%d] :: response[2047] != 'A' (0x%02x = '%c')\n",
        reqCount, b, b);
    }
/* to speed up thiungs, only 2 positions are check for the fillchar */
    nicofclt_getResponseDataByte(x, 1, &b);
    if (b != zeFillChar) {
      printf("!! req[%d] :: response[1] != '%c' (0x%02x = '%c')\n",
        reqCount, zeFillChar, b, b);
    }
    nicofclt_getResponseDataByte(x, 2046, &b);
    if (b != zeFillChar) {
      printf("!! req[%d] :: response[2046] != '%c' (0x%02x = '%c')\n",
        reqCount, zeFillChar, b, b);
    }
 
    /* deallocate the request */
    nicofclt_freeRequest(x);
  }
 
  /* free all ressources bound by the NICOF client */
  nicofclt_deinit();
 
  /* give some final information */
  printf("** sent %d packets with size = %d\n", reqCount-1, sizeof(zeData));
  return 0;
}

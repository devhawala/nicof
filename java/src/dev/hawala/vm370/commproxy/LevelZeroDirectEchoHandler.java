/*
** This file is part of the external (outside) NICOF proxy implementation.
** (NICOF :: Non-Invasive COmmunication Facility 
**           for VM/370 R6 SixPack 1.2)
**
** This software is provided "as is" in the hope that it will be useful, with
** no promise, commitment or even warranty (explicit or implicit) to be
** suited or usable for any particular purpose.
** Using this software is at your own risk!
**
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
** Released to the public domain.
*/

package dev.hawala.vm370.commproxy;

import dev.hawala.vm370.Log;
import dev.hawala.vm370.ebcdic.Ebcdic;
import dev.hawala.vm370.ebcdic.EbcdicHandler;


/**
 * Level zero load test service. This service produces the responses expected
 * by the NICOFTST.C program on the CMS side.
 * <p> 
 * Each packet received must have the length of 2048 and will be returned with
 * the offset positions 0, 17, 253 and 2047 to EBCDIC-'A'. The 2 userwords
 * are returned unmodified.  
 *   
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public class LevelZeroDirectEchoHandler implements ILevelZeroHandler {
	
	private static Log logger = Log.getLogger();
	
	private IHostConnector hostConnection = null;
	private IErrorSink errorSink= null;
	
	private EbcdicHandler firstUser = null; // used internal to verify the requesting users are not confused by the proxy
	
	public void initalize(PropertiesExt configuration, EbcdicHandler clientVm, IHostConnector hostConnection, IErrorSink errorSink) {
		this.hostConnection = hostConnection;
		this.errorSink = errorSink;
		
		this.firstUser = new EbcdicHandler().append(clientVm);
	}
	
	public void deinitialize() { }
	
	private class DoEcho implements Runnable {
		
		private final IHostConnector connection;
		private final IErrorSink errorSink;
		private final IRequestResponse request;
		
		public DoEcho(IHostConnector c, IErrorSink e, IRequestResponse r) {
			this.connection = c;
			this.errorSink = e;
			this.request = r;
		}

		@Override
		public void run() {
			try {
				byte[] user = request.getReqUser();
				EbcdicHandler username = new EbcdicHandler(8).appendEbcdic(user, 0, user.length);
				short slot = request.getSlot();
				
				int uw1 = request.getReqUserWord1();
				int uw2 = request.getReqUserWord2();
				
				byte[] reqData = request.getReqData();
				int reqDataLen = request.getReqDataLen();
				
				logger.debug(
						"\n  slot = ", slot,
						"\n  user = ", username,
						"\n  uw1  = ", uw1, 
						"\n  uw2  = ", uw2,
						"\n");
				
				request.setRespUserWord1(uw1);
				request.setRespUserWord2(uw2);
				byte[] respData = request.getRespData();
				if (reqDataLen > 0) {
					System.arraycopy(reqData, 0, respData, 0, reqDataLen);
				}
				respData[0] = Ebcdic._A;
				respData[17] = Ebcdic._A;
				respData[253] = Ebcdic._A;
				respData[2047] = Ebcdic._A;
				request.setRespDataLen(reqDataLen);
				
				connection.sendResponse(request);
			} catch (CommProxyStateException exc) {
				this.errorSink.consumeException(exc);
			} catch (Throwable thr) {
				thr.printStackTrace();
				this.errorSink.consumeException(new CommProxyStateException("** Caught exception while processing request: " + thr.getMessage()));
			}
		}
		
	}

	@Override
	public Runnable getRequestHandler(IRequestResponse request) throws CommProxyStateException {
		byte[] user = request.getReqUser();
		EbcdicHandler username = new EbcdicHandler(8).appendEbcdic(user, 0, user.length);
		if (this.firstUser == null) {
			this.firstUser = username;
		} else if (!this.firstUser.eq(username)) {
			logger.error("Current client-VM does not match intial VM");
		}
		
		return new DoEcho(this.hostConnection, this.errorSink, request);
	}
}

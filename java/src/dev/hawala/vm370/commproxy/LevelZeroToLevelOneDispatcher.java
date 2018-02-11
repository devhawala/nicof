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
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2012
** Released to the public domain.
*/

package dev.hawala.vm370.commproxy;

import java.util.HashMap;

import dev.hawala.vm370.Log;
import dev.hawala.vm370.ebcdic.EbcdicHandler;

/**
 * Level-Zero service implementing the Level-One infrastructure and base services, allowing
 * all configured Level-One services to work.
 * <p>
 * This services mainly receives all requests from the host (as it is the Level-Zero service
 * for this NICOF outside proxy instance) and dispatches the requests to the invoked Level-One
 * services.
 * <p>
 * Furthermore, this class also implements the Level-One base service providing the utility
 * functionality for other Level-One services at the inside (resolving other services by name,
 * getting information about the environment where NICOF runs, reading and writing bulk streams).
 * This Level-One base service is always bound to the service-id 0, whereas all other Level-One
 * services have varying service-ids.   
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public class LevelZeroToLevelOneDispatcher implements ILevelZeroHandler {

	// the direct commands to the base service
	private final static int CMD_RESOLVE = 0; // resolve a service name to the service id
	private final static int CMD_GETENVINFO = 1; // get information about the environment where NICOF runs
	
	// commands for accessing bulk source streams
	private final static int CMD_BULKSRC_CLOSE = 100; // close the stream
	private final static int CMD_BULKSRC_READ = 101; // read the next block and wait if neccessary
	private final static int CMD_BULKSRC_READNOWAIT = 102; // read the data available, not waiting for more data to get available
	private final static int CMD_BULKSRC_GETCOUNTS = 103; // get the count information (immediately available and total remaining)
	private final static int CMD_BULKSRC_LAST = 199; // (not a command)

	// commands for accessing bulk sink streams
	private final static int CMD_BULKSINK_CLOSE = 200; // close the stream
	private final static int CMD_BULKSINK_WRITE = 201; // write a data block
	private final static int CMD_BULKSINK_LAST = 299; // (not a command)
	
	// negative RCs are reserved for technical state transmission (positive RCs are for the services)
	public final static int STATE_NEW_BULK_SOURCE = -32; // (result).controlData & 0xFFFF is the Stream-ID
	public final static int STATE_ERR_BULK_SOURCE_INVALID = -33; // the specified bulk source is not or no longer valid
	public final static int STATE_NEW_BULK_SINK = -64; // (result).controlData & 0xFFFF is the Stream-ID
	public final static int STATE_ERR_BULK_SINK_INVALID = -65; // the specified bulk sink is not or no longer valid
	public final static int STATE_ERR_INVALID_SERVICE = -1024; // anything else in the response is unspecified
	public final static int STATE_ERR_SVC_INVALIDRESULT = -1025; // anything else in the response is unspecified
	public final static int STATE_ERR_SVC_EXCEPTION = -1026; // anything else in the response is unspecified
	public final static int STATE_ERR_BASESVC_INVCMD = -2048; // anything else in the response is unspecified
	
	private static Log logger = Log.getLogger();
	
	// the Level-One environment
	//private PropertiesExt configuration = null;
	private IHostConnector hostConnection = null;
	private IErrorSink errorSink = null;
	
	// information about the registered Level-One services
	private HashMap<String,Integer> svcNameToId = new HashMap<String,Integer>();
	private HashMap<Integer,ILevelOneHandler> svcIdToHandler = new HashMap<Integer,ILevelOneHandler>();
	
	// bulk-stream management
	private StreamManager streamManager = new StreamManager(); // manager id => stream
	private ILevelOneHandler sourceStreamsHandler = null; // internal Level-One handler to process commands for source bulks streams
	private ILevelOneHandler sinkStreamsHandler = null; // internal Level-One handler to process commands for sink bulks streams
	
	// manager stream-id => stream-object
	// -> creates the stream-id for a new stream to be used when communicating with the client implementation
	// -> returns the stream for a given stream-id
	// -> forgets a id => stream mapping when requested to
	private static class StreamManager {
		private int lastBulkSourceId;
		private int lastBulkSinkId;

		private HashMap<Integer,IBulkSource> bulkSources = new HashMap<Integer,IBulkSource>();
		private HashMap<Integer,IBulkSink> bulkSinks = new HashMap<Integer,IBulkSink>();
		
		public StreamManager() {
			this.lastBulkSourceId = (int)(System.currentTimeMillis() & 0x0FFE);
			this.lastBulkSinkId = this.lastBulkSourceId + 1;
		}
		
		public int addStream(IBulkSource stream) {
			synchronized(this) {
				this.lastBulkSourceId += 2;
				this.bulkSources.put(this.lastBulkSourceId, stream);
				return this.lastBulkSourceId;
			}
		}
		
		public int addStream(IBulkSink stream) {
			synchronized(this) {
				this.lastBulkSinkId += 2;
				this.bulkSinks.put(this.lastBulkSinkId, stream);
				return this.lastBulkSinkId;
			}
		}
		
		public void removeStream(int streamId) {
			if ((streamId % 2) == 0) {
				this.bulkSources.remove(streamId);
			} else {
				this.bulkSinks.remove(streamId);
			}
		}
		
		public IBulkSource getSourceStream(int streamId) {
			synchronized(this) {
				if (this.bulkSources.containsKey(streamId)) {
					return this.bulkSources.get(streamId);
				} else { 
					return null;
				}
			}
		}
	
		public IBulkSink getSinkStream(int streamId) {
			synchronized(this) {
				if (this.bulkSinks.containsKey(streamId)) {
					return this.bulkSinks.get(streamId);
				} else { 
					return null;
				}
			}
		}
	}
	
	/**
	 * Initialize the base services: instantiate and initialize all Level-One services configured
	 * in the properties file. Each successfully loaded Level-One service gets a service id valid
	 * only for this base services instance, so the same service name will probably be resolved to
	 * a different id for 2 NICOF clients or on the next run on NICOF. 
	 */
	@Override
	public void initalize(PropertiesExt configuration, EbcdicHandler clientVm, IHostConnector hostConnection, IErrorSink errorSink) {
		//this.configuration = configuration;
		this.hostConnection = hostConnection;
		this.errorSink = errorSink;
		
		logger.info("New Level1 dispatcher, client-VM: ", clientVm);
		logger.debug("Level1 dispatcher for client ", clientVm, " -- begin initializing");
		
		// create the handlers for bulk-streams
		this.sourceStreamsHandler = new BulkSourceHandler(this.streamManager);
		this.sinkStreamsHandler = new BulkSinkHandler(this.streamManager);
		
		// load the configured services and assign the service-ids
		int svcId = (short)(System.currentTimeMillis() & 0x0FFF);
		int svcIncr = (svcId & 0x0007) + 3;
		int i = 0;
		String svcDef = configuration.getString("service." + i, null);
		while(svcDef != null && svcDef.length() > 0) {
			String[] parts = svcDef.split(":");
			if (parts.length == 2 && parts[0].length() > 0 && parts[1].length() > 0) {
				String svcName = parts[0].trim().toLowerCase();
				String svcClassName = parts[1].trim();
				ILevelOneHandler svcHandler = null;
				
				try {
					Class<?> cl = this.getClass().getClassLoader().loadClass(svcClassName);
					Object o = cl.newInstance();
					if (o instanceof ILevelOneHandler) {
						svcHandler = (ILevelOneHandler)o;
					} else {
						logger.error("service.", i, ": ", svcName, " => invalid level1 handler (not a ILevelOneHandler): ", svcClassName);
					}
				} catch(ClassNotFoundException e) {
					logger.error("service.", i, ": ", svcName, " => invalid level1 handler (class cannot be instanciated): ", svcClassName);
				} catch(IllegalAccessException e) {
					logger.error("service.", i, ": ", svcName, " => invalid level1 handler (IllegalAccessException): ", svcClassName);
				} catch(InstantiationException e) {
					logger.error("service.", i, ": ", svcName, " => invalid level1 handler (InstantiationException): ", svcClassName);
				}
				
				if (svcHandler != null) {
					logger.debug("SvcId: " + svcId + " : service[" + i + "] " + svcName + " : OK : " + svcClassName);
					this.svcNameToId.put(svcName, svcId);
					this.svcIdToHandler.put(svcId, svcHandler);
					svcHandler.initialize(svcName, clientVm, configuration);
				}
			} else {
				logger.error("invalid service specification for service." + i, ": '", svcDef, "'");
			}
			i++;
			svcId += svcIncr;
			svcDef = configuration.getString("service." + i);
		}
		
		// log that we are initialized and dump the services maps for debugging
		logger.debug("Level1 dispatcher for client ", clientVm, " -- done initializing");
		for (String sn : this.svcNameToId.keySet()) {
		  logger.debug("ServiceName(", sn, ") => id: ", this.svcNameToId.get(sn));
		}
		for (int sid : this.svcIdToHandler.keySet()) {
		  logger.debug("ServiceId(", sid, ") => handler: ", this.svcIdToHandler.get(sid));	
		}
	}
	
	@Override
	public void deinitialize() {
		for (ILevelOneHandler h : this.svcIdToHandler.values()) {
			h.deinitialize();
		}
	}
	
	// resolve a service name (as defined in the configuration for Level-One) to
	// the service-id.
	private ILevelOneResult processService0CmdResolve(byte[] requestData, int requestDataLength) {
		if (requestDataLength < 1) {
			return new LevelOneProtErrResult(STATE_ERR_INVALID_SERVICE);
		}
		byte[] nameBytes = new byte[requestDataLength];
		for (int i = 0; i < requestDataLength; i++) {nameBytes[i] = requestData[i]; }
		String svcName = new String(nameBytes).toLowerCase(); // new String(requestData, requestDataLength).toLowerCase();
		logger.debug("processService0CmdResolve() for: '",svcName,"'"); 
		if (!this.svcNameToId.containsKey(svcName)) {
			return new LevelOneProtErrResult(STATE_ERR_INVALID_SERVICE);
		}
		return new LevelOneBufferResult((short)0, this.svcNameToId.get(svcName).intValue(), 0);
	}
	
	// the Level-One service implementing the bulk source stream operations
	private static class BulkSourceHandler implements ILevelOneHandler {
		private final StreamManager streamManager;
		
		public BulkSourceHandler(StreamManager streamManager) {
			this.streamManager = streamManager;
		}

		@Override
		public void deinitialize() {}

		@Override
		public void initialize(String name, EbcdicHandler clientVm, PropertiesExt configuration) {}

		@Override
		public ILevelOneResult processRequest(
				short cmd, 
				int controlData,
				byte[] requestData, 
				int requestDataLength, 
				byte[] responseBuffer) {
			IBulkSource src = this.streamManager.getSourceStream(controlData);
			
			if (src == null) {
				return new LevelOneProtErrResult(LevelZeroToLevelOneDispatcher.STATE_ERR_BULK_SOURCE_INVALID); 
			}
			
			if (cmd == CMD_BULKSRC_READ || cmd == CMD_BULKSRC_READNOWAIT) {
				int bytes = src.getNextBlock(responseBuffer, (cmd == CMD_BULKSRC_READNOWAIT));
				return new LevelOneBufferResult(0, src.getState(), bytes);
			} else if (cmd == CMD_BULKSRC_GETCOUNTS) {
				int remaining = src.getRemainingCount();
				responseBuffer[0] = (byte)((remaining & 0xFF000000) >> 24);
				responseBuffer[1] = (byte)((remaining & 0x00FF0000) >> 16);
				responseBuffer[2] = (byte)((remaining & 0x0000FF00) >> 8);
				responseBuffer[3] = (byte)(remaining & 0x000000FF);
				int available = src.getAvailableCount();
				responseBuffer[4] = (byte)((available & 0xFF000000) >> 24);
				responseBuffer[5] = (byte)((available & 0x00FF0000) >> 16);
				responseBuffer[6] = (byte)((available & 0x0000FF00) >> 8);
				responseBuffer[7] = (byte)(available & 0x000000FF);
				return new LevelOneBufferResult(0, src.getState(), 8);
			} else if (cmd == CMD_BULKSRC_CLOSE) {
				src.close();
				this.streamManager.removeStream(controlData);
				return new LevelOneBufferResult(0, src.getState(), 0);
			}			
			
			return new LevelOneProtErrResult(LevelZeroToLevelOneDispatcher.STATE_ERR_BASESVC_INVCMD);
		}
	}
	
	// the Level-One service implementing the bulk sink stream operations
	private static class BulkSinkHandler implements ILevelOneHandler {
		private final StreamManager streamManager;
		
		public BulkSinkHandler(StreamManager streamManager) {
			this.streamManager = streamManager;
		}

		@Override
		public void deinitialize() {}

		@Override
		public void initialize(String name, EbcdicHandler clientVm, PropertiesExt configuration) {}

		@Override
		public ILevelOneResult processRequest(
				short cmd, 
				int controlData,
				byte[] requestData, 
				int requestDataLength, 
				byte[] responseBuffer) {
			IBulkSink snk = this.streamManager.getSinkStream(controlData);
			
			if (snk == null) {
				return new LevelOneProtErrResult(LevelZeroToLevelOneDispatcher.STATE_ERR_BULK_SINK_INVALID); 
			}
			
			if (cmd == CMD_BULKSINK_WRITE) {
				snk.putBlock(requestData, requestDataLength);
				return new LevelOneBufferResult(0, snk.getState(), 0);
			} else if (cmd == CMD_BULKSINK_CLOSE) {
				snk.close();
				this.streamManager.removeStream(controlData);
				return new LevelOneBufferResult(0, snk.getState(), 0);
			}
			
			return new LevelOneProtErrResult(LevelZeroToLevelOneDispatcher.STATE_ERR_BASESVC_INVCMD);
		}
	}
	
	// Wrapper for Level-One services to let them process a request asynchronously in background
	// (in a different thread)
	private static class LevelOneRunnable implements Runnable {
		
		private final ILevelOneResult result;
		
		private final ILevelOneHandler handler;
		private final short cmd;
		private final IRequestResponse request;
		
		private final IHostConnector hostConnection;
		private final IErrorSink errorSink;
		private final StreamManager streams;
		
		public LevelOneRunnable(
				IHostConnector hostConnection, 
				IErrorSink errorSink,
				StreamManager streams,
				ILevelOneHandler handler,
				short cmd,
				IRequestResponse request) {
			this.hostConnection = hostConnection;
			this.errorSink = errorSink;
			this.streams = streams;

			this.handler = handler;
			this.cmd = cmd;
			this.request = request;
			
			this.result = null;
		}
		
		public LevelOneRunnable(
				IHostConnector hostConnection, 
				IErrorSink errorSink,
				StreamManager streams,
				ILevelOneResult result,
				IRequestResponse request) {
			this.hostConnection = hostConnection;
			this.errorSink = errorSink;
			this.streams = streams;
			
			this.result = result;
			this.request = request;

			this.handler = null;
			this.cmd = 0;
		}

		@Override
		public void run() {
			ILevelOneResult r = this.result;
			if (r == null) {
				try {
				r = this.handler.processRequest(
						cmd, 
						this.request.getReqUserWord2(), 
						this.request.getReqData(), 
						this.request.getReqDataLen(), 
						this.request.getRespData());
				} catch(Exception exc) {
					exc.printStackTrace();
					r = new LevelOneProtErrResult(LevelZeroToLevelOneDispatcher.STATE_ERR_SVC_EXCEPTION);
				}
			}
			if (r instanceof LevelOneProtErrResult) {
				this.request.setRespUserWord1(((LevelOneProtErrResult) r).getErrCode());
				this.request.setRespUserWord2(0);
				this.request.setRespDataLen(0);
			} else if (r instanceof LevelOneBufferResult) {
				this.request.setRespUserWord1(((LevelOneBufferResult) r).getRc());
				this.request.setRespUserWord2(((LevelOneBufferResult) r).getControlData());
				this.request.setRespDataLen(((LevelOneBufferResult) r).getBufferLength());
			} else if (r instanceof IBulkSource) {
				int streamId = this.streams.addStream((IBulkSource)r);
				this.request.setRespUserWord1(LevelZeroToLevelOneDispatcher.STATE_NEW_BULK_SOURCE);
				this.request.setRespUserWord2(streamId);
				this.request.setRespDataLen(0);
			} else if (r instanceof IBulkSink) {
				int streamId = this.streams.addStream((IBulkSink)r);
				this.request.setRespUserWord1(LevelZeroToLevelOneDispatcher.STATE_NEW_BULK_SINK);
				this.request.setRespUserWord2(streamId);
				this.request.setRespDataLen(0);
			} else {
				this.request.setRespUserWord1(LevelZeroToLevelOneDispatcher.STATE_ERR_SVC_INVALIDRESULT);
				this.request.setRespUserWord2(0);
				this.request.setRespDataLen(0);
			}
			
			try {
				this.hostConnection.sendResponse(request);
			} catch (CommProxyStateException exc) {
				this.errorSink.consumeException(exc);
			} catch (Throwable thr) {
				thr.printStackTrace();
				this.errorSink.consumeException(new CommProxyStateException("** Caught exception while sending response: " + thr.getMessage()));
			}
		}
	}
	
	// create the response for the "get environment information" request to the base service.
	private ILevelOneResult createEnvInfo() {		
		// lower 8 bits: service version
		// bits 9-10: line-end convention on this platform
		//   0x01: LF
		//   0x02; CR
		//   0x03: CR-LF
		//   0x00: LF-CR (or anything else very strange...)
		int result = 1; 
		
		// get the line-end info
		String lineEnd = System.getProperty("line.separator");
		if ("\n".equals(lineEnd)) {
			result |= 0x0100;
		} else if ("\r".equals(lineEnd)) {
			result |= 0x0200;
		} else if ("\r\n".equals(lineEnd)) {
			result |= 0x0300;
		}
		
		return new LevelOneBufferResult(0, result, 0); // rc = OK, controlData = result, length = 0
	}
	
	@Override
	public Runnable getRequestHandler(IRequestResponse request) throws CommProxyStateException {
		int uw1 = request.getReqUserWord1();
		int serviceId = (uw1 >> 16);
		short serviceCmd = (short)(uw1 & 0x0000FFFF);
		
		logger.debug("getRequestHandler(id:", serviceId, ",cmd:",serviceCmd, ")");

		if (serviceId == 0 && serviceCmd == CMD_RESOLVE) {
			ILevelOneResult res = processService0CmdResolve(request.getReqData(), request.getReqDataLen());
			return new LevelOneRunnable(this.hostConnection, this.errorSink, this.streamManager, res, request);
		} else if (serviceId == 0 && serviceCmd == CMD_GETENVINFO) {
			ILevelOneResult res =  this.createEnvInfo();
			return new LevelOneRunnable(this.hostConnection, this.errorSink, this.streamManager, res, request);
		} else if (serviceId == 0 && (serviceCmd >= CMD_BULKSRC_CLOSE && serviceCmd <= CMD_BULKSRC_LAST)) {
			return new LevelOneRunnable(this.hostConnection, this.errorSink, this.streamManager, this.sourceStreamsHandler, serviceCmd, request);
		} else if (serviceId == 0 && (serviceCmd >= CMD_BULKSINK_CLOSE && serviceCmd <= CMD_BULKSINK_LAST)) {
			return new LevelOneRunnable(this.hostConnection, this.errorSink, this.streamManager, this.sinkStreamsHandler, serviceCmd, request);
		} else if (serviceId == 0) {
			ILevelOneResult res = new LevelOneProtErrResult(LevelZeroToLevelOneDispatcher.STATE_ERR_BASESVC_INVCMD);
			return new LevelOneRunnable(this.hostConnection, this.errorSink, this.streamManager, res, request);
		} else if (this.svcIdToHandler.containsKey(serviceId)) {
			ILevelOneHandler handler = this.svcIdToHandler.get(serviceId);
			return new LevelOneRunnable(this.hostConnection, this.errorSink, this.streamManager, handler, serviceCmd, request);
		} else {
			logger.debug("... invalid service id");
			ILevelOneResult res = new LevelOneProtErrResult(LevelZeroToLevelOneDispatcher.STATE_ERR_INVALID_SERVICE);
			return new LevelOneRunnable(this.hostConnection, this.errorSink, this.streamManager, res, request);
		}
	}
}

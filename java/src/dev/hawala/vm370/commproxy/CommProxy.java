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

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.HashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import dev.hawala.vm370.Log;
import dev.hawala.vm370.ebcdic.EbcdicHandler;

/**
 * The main class for the outside NICOF communication proxy.
 * <p>
 * This class manages the connection object as well as the threads processing the
 * requests at Level-Zero, which are handled by the Level-Zero handler instantiated
 * here as specified by the configuration.
 * <p>
 * More than one instance of this class (outside proxies) can be run at OS level,
 * provided each is invoked with a different configuration file (java properties file)
 * indicating a different VM/370 inside proxy VM. 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public class CommProxy implements Runnable, IErrorSink {
	
	private static Log logger = Log.getLogger();
	
	private static Object connectLock = new Object();
	private static Calendar nextConnect = Calendar.getInstance();
	
	// the retry interval to connect to the proxy VM when the connection
	// is lost or not yet build up
	private static int connRetryIntervall = 3000; // 3 seconds
	
	private static File chkFile(String fn) {
		File f = new File(fn);
		if (f.isFile() && f.canRead()) { return f; }
		return null;
	}
	
	// load a properties file with predefined defaults suitable for the
	// default setup with NICOFPXY being the inside proxy on a local Hercules
	// machine (having the 3270 default port) using the Level-One dispatching mechanism.
	// If no file is specified, the "default.properties" file is used.
	private static PropertiesExt loadProperties(String cfgFileName) {
		PropertiesExt props = new PropertiesExt();
		
		props.setProperty("vm", "nicofpxy");
		props.setProperty("host","localhost");
		props.setProperty("port", "3270");
		props.setProperty("luname", "");
		
		props.setProperty("level0handler", "dev.hawala.vm370.commproxy.LevelZeroToLevelOneDispatcher");
		
		File configFile = null;
		
		if (cfgFileName != null && cfgFileName.length() > 0) {
			configFile = chkFile(cfgFileName);
			if (configFile == null) {
				configFile = chkFile(cfgFileName + ".properties");
			}
		} else {
			configFile = chkFile("default.properties");
		}
		
		if (configFile != null) {		
			try {
				FileInputStream fis = new FileInputStream(configFile);
				props.load(fis);
				fis.close();
			} catch (IOException e) {
				System.out.println("*****");
				System.out.println("***** ERROR - cannot read properties from: " + cfgFileName);
				System.out.println("*****");
			}
		}
		
		return props;
	}
	
	// the configuration of this outside proxy
	private final PropertiesExt props;
	
	private final String logPrefix;
	
	// the Level-Zero handler factory object, used for each VM user connecting
	// for the first time to get us a Level-Zero handler specifically this user.
	private Class<ILevelZeroHandler> level0factory;
	
	// the thread pool for the asynchronous and parallel processing of the incoming
	// requests.
	private ExecutorService threadPool = Executors.newCachedThreadPool();
	
	// get us a new Level-Zero handler with error handling
	private ILevelZeroHandler createLevelZeroHandler() throws CommProxyStateException {
		try {
			ILevelZeroHandler handler = level0factory.newInstance();
			return handler;
		} catch(IllegalAccessException e) {
			throw new CommProxyStateException("invalid level0 handler (IllegalAccessException)");
		} catch(InstantiationException e) {
			throw new CommProxyStateException("invalid level0 handler (InstantiationException)");
		}
	}
	
	// the constructor, taking the command line specifying the properties file to use
	// for configuration.
	@SuppressWarnings("unchecked")
	private CommProxy(String cfgFileName) {
		
		// load the configuration data
		this.props = loadProperties(cfgFileName);
		this.logPrefix = "[" + this.props.getProperty("vm", "?") + "] ";
		
		// create the Level-Zero handler factory for the configured handler class
		String level0handlerName = this.props.getString("level0handler", null);
		if (level0handlerName == null || level0handlerName.length() == 0) {
			logger.error(this.logPrefix, "ctor(): level0 handler undefined in configuration");
			return;
		} else {
			try {
				Class<?> cl = this.getClass().getClassLoader().loadClass(level0handlerName);
				Object o = cl.newInstance();
				if (!(o instanceof ILevelZeroHandler)) {
					logger.error(this.logPrefix, "ctor(): invalid level0 handler (does not implement ILevelZeroHandler)");
					return;
				}
				this.level0factory = (Class<ILevelZeroHandler>)cl;
			} catch(ClassNotFoundException e) {
				logger.error(this.logPrefix, "ctor(): invalid level0 handler (class cannot be instanciated)");
				this.level0factory = null;
				return;
			} catch(IllegalAccessException e) {
				logger.error(this.logPrefix, "ctor(): invalid level0 handler (IllegalAccessException)");
				this.level0factory = null;
				return;
			} catch(InstantiationException e) {
				logger.error(this.logPrefix, "ctor(): invalid level0 handler (InstantiationException)");
				this.level0factory = null;
				return;
			}
		}
	}
	
	// the main thread is the one receiving the packets from the inside proxy
	// and dispatching them to the Level-Zero handler of the remote user in a 
	// separate thread in the thread pool.
	private Thread mainThread = null;
	
	// the exception received when processing a request asynchronously to be
	// passed to the main thread.
	private CommProxyStateException asyncException = null;
	
	/**
	 * Pass a caught exception to the main processing thread from an asynchronous
	 * request processing.
	 * @param exc the exception to be transmitted to the main processing thread.
	 */
	public void consumeException(CommProxyStateException exc) {
		this.asyncException = exc;
		logger.warn(this.logPrefix, exc.getMessage());
		this.mainThread.interrupt();
	}
	
	/**
	 * Main worker routine (re-)connecting to the inside proxy on the VM/370 host,
	 * receiving the requests and dispatching the to the requesting user's 
	 * Level-Zero handler. 
	 */
	public void run() { 
		// no Level-Zero handler factory, no work
		if (this.level0factory == null) { return; } // the reason has already been logged
		
		// this is the main thread
		this.mainThread = Thread.currentThread();
		
		// the remote proxy connection object and the status data
		IHostConnector hostConn = new Dialed3270HostConnector(this.props);
		String lastErrMsg = "";
		String connectMsg = "Connecting...";
		
		// just a counter for stats
		int packetCount = 0;
		
		// main loop: try (re-)connections and transmissions until program is stopped
		while(true) {

			// connect to the proxy VM
			logger.info(this.logPrefix, connectMsg);
			while(!hostConn.isConnected()) {
				try {
					synchronized(connectLock) {
						if (Calendar.getInstance().before(nextConnect)) { 
							try { Thread.sleep(500); } catch (InterruptedException e) { } 
						}
						nextConnect = Calendar.getInstance();
						nextConnect.add(Calendar.MILLISECOND, 500);
					}
					// start the proxy connection
					hostConn.connect(); 
				} catch (CommProxyStateException exc) {
					String msg = exc.getMessage();
					if (!lastErrMsg.equals(msg)) {
						logger.info(this.logPrefix, msg);
						lastErrMsg = msg;
					}
					if (exc.isUnrecoverable()) { return; } // abort proxy communication...
					try { Thread.sleep(connRetryIntervall); } catch(InterruptedException e) {}
				}
			}
				
			// we are connected
			logger.info(this.logPrefix, "Connected to Proxy-VM");
			lastErrMsg = "";
			connectMsg = "Reconnecting...";
			
			// reset for new connection
			this.asyncException = null;
			HashMap<String,ILevelZeroHandler> clientvmHandlers = new HashMap<String,ILevelZeroHandler>();
	
			// loop through all incoming requests, catching connection errors when receiving the
			// requests
			try {
				while(this.asyncException == null) {
					logger.debug(this.logPrefix, "... waiting data packet");
				
					// get the next request
					IRequestResponse req = hostConn.receiveRecord();
					if (req == null) { continue; }
					packetCount++;
					logger.debug(this.logPrefix, "... received data packet ", packetCount, " for processing");
					
					// get the Level-Zero handler of the VM/370 user, checking if a new user issued
					// the request and get a new handler for this user if so 
					byte[] user = req.getReqUser();
					EbcdicHandler username = new EbcdicHandler(8).appendEbcdic(user, 0, user.length);
					String clientVm = username.toString();
					
					ILevelZeroHandler vmHandler;
					if (clientvmHandlers.containsKey(clientVm)) {
						vmHandler = clientvmHandlers.get(clientVm);
					} else {
						vmHandler = this.createLevelZeroHandler();
						clientvmHandlers.put(clientVm, vmHandler);
						vmHandler.initalize(this.props, username, hostConn, this);
					}
					
					// dispatch the request to the handler and process it in the background
					Runnable reqHandler = vmHandler.getRequestHandler(req);
					this.threadPool.execute(reqHandler);
				}
			} catch (CommProxyStateException exc) {
				logger.warn(this.logPrefix, exc.getMessage());
			} catch (Throwable thr) {
				logger.error(this.logPrefix, thr.getMessage());
				thr.printStackTrace();
			}
			
			// if we are here, the connection to the host was lost, so forget all
			// Level-Zero handlers (and their potential state shared with the inside)
			// and try to reconnect
			for (ILevelZeroHandler h : clientvmHandlers.values()) {
				h.deinitialize();
			}
		}		
	}

	/**
	 * Main line code, invoked from command line
	 * @param args
	 */
	public static void main(String[] args) {
		Log.watchLogConfiguration("nicof_logging.properties");
		
		System.out.printf("\nNICOF %s -- Non-Invasive Communication Facility\n\n", "0.7.0");
		
		ArrayList<Thread> proxies = new ArrayList<Thread>();
		
		for (String arg : args) {
			CommProxy proxy = new CommProxy(arg);
			Thread thr = new Thread(proxy);
			proxies.add(thr);
			thr.start();
		}
		if (proxies.isEmpty()) { // at least run the default proxy
			CommProxy proxy = new CommProxy(null);
			Thread thr = new Thread(proxy);
			proxies.add(thr);
			thr.start();
		}
		
		for(Thread thr : proxies) {
			try { thr.join(); } catch (InterruptedException e) { }
		}
		
		Log.shutdown();
	}
}

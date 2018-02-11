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
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2012,2014
** Released to the public domain.
*/

package dev.hawala.vm370.commproxy.socketapi;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.ArrayList;

import dev.hawala.vm370.Log;
import dev.hawala.vm370.commproxy.CommProxyStateException;
import dev.hawala.vm370.commproxy.IErrorSink;
import dev.hawala.vm370.commproxy.IHostConnector;
import dev.hawala.vm370.commproxy.ILevelZeroHandler;
import dev.hawala.vm370.commproxy.IRequestResponse;
import dev.hawala.vm370.commproxy.PropertiesExt;
import dev.hawala.vm370.ebcdic.Ebcdic;
import dev.hawala.vm370.ebcdic.EbcdicHandler;

/**
 * Level 0 service implementation for the Socket API provided by NICOF.
 * 
 * This class implements the requests issued by the socket API implementation
 * on the CMS client.
 * An instance of this class is created for each client VM on the first request
 * from this VM.
 * 
 * The requests commands CMD_ALLOCSOCKET, CMD_GETHOSTBYNAME and CMD_GETHOSTBADDR
 * are handled here, as these operations do not address a specific socket.
 *   
 * All other request commands are delegated to the ISocketProxy instance for the
 * socket to which the operation is addressed, with the proxy instance identified by
 * the socket-fd as index in an internal table
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2014
 *
 */
public class Level0SocketAPIHandler implements ILevelZeroHandler {
	
	// must be in sync with the parameter in the CMS-side API
	private static final int MAX_SOCKET_COUNT = 64;
	
	// commands for the CMS-API
	private static final int CMD_GETHOSTBYNAME = 16;
	private static final int CMD_GETHOSTBYADDR = 17;
	
	private static final int CMD_ALLOCSOCKET = 32;
	private static final int CMD_CLOSE = 33;
	private static final int CMD_BIND = 34;
	private static final int CMD_CONNECT = 35;
	private static final int CMD_LISTEN = 36;
	private static final int CMD_ACCEPT = 37;
	private static final int CMD_GETSOCKNAME = 38;
	private static final int CMD_GETPEERNAME = 39;
	private static final int CMD_SHUTDOWN = 40;
	private static final int CMD_RECV = 48;
	private static final int CMD_RECVFROM = 49;
	private static final int CMD_SEND = 50;
	private static final int CMD_SENDTO = 51;
	
	private static final ArrayList<String> cmdNames = new ArrayList<String>();
	
	static {
		for (int i = 0; i < 128; i++) {
			cmdNames.add("INVALID");
		}
		cmdNames.set(CMD_GETHOSTBYNAME, "CMD_GETHOSTBYNAME");
		cmdNames.set(CMD_GETHOSTBYADDR, "CMD_GETHOSTBYADDR");
		cmdNames.set(CMD_ALLOCSOCKET, "CMD_ALLOCSOCKET");
		cmdNames.set(CMD_CLOSE, "CMD_CLOSE");
		cmdNames.set(CMD_BIND, "CMD_BIND");
		cmdNames.set(CMD_CONNECT, "CMD_CONNECT");
		cmdNames.set(CMD_LISTEN, "CMD_LISTEN");
		cmdNames.set(CMD_ACCEPT, "CMD_ACCEPT");
		cmdNames.set(CMD_GETSOCKNAME, "CMD_GETSOCKNAME");
		cmdNames.set(CMD_GETPEERNAME, "CMD_GETPEERNAME");
		cmdNames.set(CMD_SHUTDOWN, "CMD_SHUTDOWN");
		cmdNames.set(CMD_RECV, "CMD_RECV");
		cmdNames.set(CMD_RECVFROM, "CMD_RECVFROM");
		cmdNames.set(CMD_SEND, "CMD_SEND");
		cmdNames.set(CMD_SENDTO, "CMD_SENDTO");
	}
	
	// our logger
	private static Log logger = Log.getLogger();
	
	// our environment
	//private PropertiesExt configuration = null;
	private IHostConnector hostConnection = null;
	private EbcdicHandler clientVm = null;
	private IErrorSink errorSink = null;
	
	private String hostBasename = null;
	private String vmFQN = null;
	private InetAddress vmDefaultAddress = null;
	
	private static class DummyReservedSocket implements ISocketProxy {
		@Override
		public SocketProxyAcceptResult accept(byte[] buffer) { return new SocketProxyAcceptResult(ISocketError.ENOTSOCK); }

		@Override
		public int bind(byte[] buffer, int bufferLength) { return ISocketError.ENOTSOCK; }

		@Override
		public int close() { return ISocketError.ENOTSOCK; }

		@Override
		public int connect(byte[] buffer, int bufferLength) { return ISocketError.ENOTSOCK; }

		@Override
		public int getpeername(byte[] buffer) { return ISocketError.ENOTSOCK; }

		@Override
		public int getsockname(byte[] buffer) { return ISocketError.ENOTSOCK; }

		@Override
		public int listen(byte[] buffer, int bufferLength) { return ISocketError.ENOTSOCK; }

		@Override
		public int recv(byte[] buffer, int maxCount, int userWord) { return ISocketError.ENOTSOCK; }

		@Override
		public int recvFrom(byte[] buffer, int maxCount, int userWord) { return ISocketError.ENOTSOCK; }

		@Override
		public int send(byte[] buffer, int bufferLength, int userWord) { return ISocketError.ENOTSOCK; }

		@Override
		public int sendTo(byte[] buffer, int bufferLength, int userWord) { return ISocketError.ENOTSOCK; }

		@Override
		public int shutdown(byte[] buffer, int bufferLength) { return ISocketError.ENOTSOCK; }		
	}
	
	private final ISocketProxy reservingSocket = new DummyReservedSocket();
	
	// current open sockets as array Java-Socket with index = socket-fd
	// (an unused socket-fd is null, of course)
	private ISocketProxy[] sockets = new ISocketProxy[MAX_SOCKET_COUNT];
	
	protected int getFreeSocket() {
		synchronized(this.sockets) {
			for(int i = 0; i < this.sockets.length; i++) {
				if (this.sockets[i] == null) {
					/*
					System.out.printf("-- getFreeSocket() -> %d\n", i);
					*/ 
					this.sockets[i] = reservingSocket;
					return i;
				}
			}
		}
		return -1;
	}
	
	protected ISocketProxy getSocket(int fd) {
		if (fd < 0 || fd >= this.sockets.length) { return null; }
		synchronized(this.sockets) {
			return this.sockets[fd];
		}
	}
	
	protected void setSocket(int fd, ISocketProxy sock) {
		if (fd < 0 || fd >= this.sockets.length) { return; }
		/*
		if (sock == null) {
			System.out.printf("-- setSocket(%d) -> RESET\n", fd);
		} else {
			System.out.printf("-- setSocket(%d) -> in use\n", fd);
		}
		*/
		synchronized(this.sockets) {
			this.sockets[fd] = sock;
		}
	}
	
	protected void dropSockets() {
		for(int i = 0; i < MAX_SOCKET_COUNT; i++) {
			ISocketProxy sock = this.sockets[i];
			if (sock != null) { 
				try {
					sock.close();
				} catch (Exception e) {
					// ignore it
				}
			}
			this.sockets[i] = null;
		}
	}
	
	private class SocketManager {
		final int sockNo;
		final boolean allowSet;
		
		public  SocketManager(int sockNo, boolean allowSet) {
			this.sockNo = sockNo;
			this.allowSet = allowSet;
		}
		
		public int get() { return sockNo; }
		
		public void set(ISocketProxy proxy) {
			if (this.allowSet) { setSocket(this.sockNo, proxy); }
		}
		
		public void release() {
			setSocket(this.sockNo, null);
		}
	}
	
	private InetAddress getFirstV4Address(String name) {
		try {
			InetAddress[] as = InetAddress.getAllByName(name);
			for (InetAddress a : as) {
				if (a.getAddress().length == 4) { return a; }
			}
			return null;
		} catch (UnknownHostException exc) {
			return null;
		}
	}

	@Override
	public void initalize(PropertiesExt configuration, EbcdicHandler clientVm,
			IHostConnector hostConnection, IErrorSink errorSink) {
		//this.configuration = configuration;
		this.hostConnection = hostConnection;
		this.clientVm = clientVm;
		this.errorSink = errorSink;
		
		this.dropSockets();
		
		logger.info("New Level0 socket-API-handler initialized, client-VM: ", clientVm);
		
		this.vmDefaultAddress = null;
		this.hostBasename = configuration.getProperty("hostbasename", null);
		if (this.hostBasename != null && this.hostBasename.length() > 0) {
			this.vmFQN = clientVm.getString().trim().toLowerCase() + "." + this.hostBasename;
			//logger.info("trying client VM FQN: '", this.vmFQN, "'");
			InetAddress a = this.getFirstV4Address(vmFQN);
			if (a != null) {
			  this.vmDefaultAddress = a;
			  logger.info("** client-VM has dedicated IP address",
					  "\n  => FQN     = ", vmFQN,
					  "\n  => address = ", this.vmDefaultAddress.toString());
			  return;
			}
			
			a = this.getFirstV4Address(this.hostBasename);
			if (a != null) {
			  this.vmDefaultAddress = a;
			  logger.info("** client-VM uses base IP address of VM/370 host",
					  	"\n  => address  = ", this.vmDefaultAddress.toString());
			  return;
			}
			
		    logger.info("** client-VM uses base IP address of VM/370 host",
					"\n  => no IPv4 address found for: ", this.hostBasename,  
					"\n  => falling back to 'localhost'/127.0.0.1");
		}
		
		this.hostBasename = null;
		this.vmDefaultAddress = this.getFirstV4Address("localhost");
		if (this.vmDefaultAddress == null) { this.vmDefaultAddress = this.getFirstV4Address("127.0.0.1"); }
		logger.info("** client-VM '", this.clientVm, "' => ",
				(this.vmDefaultAddress != null) ? this.vmDefaultAddress.toString() : "NULL IP-address !!!");
	}

	@Override
	public void deinitialize() {
		this.dropSockets();
		logger.info("Level0 socket-API-handler de-initialized, client-VM: ", this.clientVm);
	}
	
	static class ByteSink {
		private final byte[] sink;
		
		private int pos = 0;
		
		public ByteSink(byte[] target) {
			this.sink = target;
		}
		
		public ByteSink put(byte b) {
			this.sink[this.pos++] = b;
			return this;
		}
		
		public ByteSink putShort(int s) {
			this.sink[this.pos++] = (byte)((s & 0xFF00) >> 8);
			this.sink[this.pos++] =  (byte)(s & 0x00FF);
			return this;
		}
		
		public ByteSink putInt(int i) {
			this.sink[this.pos++] = (byte)((i & 0xFF000000) >> 24);
			this.sink[this.pos++] = (byte)((i & 0x00FF0000) >> 16);
			this.sink[this.pos++] = (byte)((i & 0x0000FF00) >> 8);
			this.sink[this.pos++] =  (byte)(i & 0x000000FF);
			return this;
		}
		
		public ByteSink putBytes(byte[] bs) {
			for (byte b : bs) { this.sink[this.pos++] = b; }
			return this;
		}
		
		public ByteSink putString(byte[] bs) {
			this.putBytes(bs);
			this.sink[this.pos++] = (byte)0;
			return this;
		}
		
		public int getCurrPos() { return this.pos; }
	}
	
	// returns: length used in 'outBuffer', with 0 => name could not be resolved
	private static int gethostbyname(byte[] inbuffer, int inbufferLength, byte[] outbuffer) {
		String hostname = Ebcdic.toAscii(inbuffer, 0, inbufferLength);
		try
		{		
			InetAddress[] as = InetAddress.getAllByName(hostname);	
			return getHostent(hostname, as, outbuffer);
		} catch(UnknownHostException exc) {
			System.out.println("[" + hostname + "] ... **unknown**");
			return -1;
		} 
	}
	
	private static int gethostbyaddr(byte[] inbuffer, int inbufferLength, byte[] outbuffer) {
		// inBuffer[0,1] = format (must be AF_INET) => [0] == 0, [1]
		// inBuffer[2..5]} = address
		if (inbuffer[0] != 0 || inbuffer[1] != 2) { return -2; } // wrong protocol
		if (inbufferLength != 6) { return -3; } // wrong length
		byte[] addr = new byte[] { inbuffer[2], inbuffer[3], inbuffer[4], inbuffer[5] };
		try
		{		
			InetAddress a = InetAddress.getByAddress(addr);	
			if (a == null) { return -1; }
			
			String hostname = a.getCanonicalHostName();
			InetAddress[] as = new InetAddress[] { a };
			return getHostent(hostname, as, outbuffer);
		} catch(UnknownHostException exc) {
			String aa = "" + ((int)addr[3]) + "." + ((int)addr[2]) + ((int)addr[1]) + ((int)addr[0]);
			System.out.println("[" + aa + "] ... ** unable to resolve **");
			return -1;
		} 
	}
	
	private static int getHostent(String hostname, InetAddress[] as, byte[] outbuffer) {
		ByteSink sink = new ByteSink(outbuffer);
		int remaining = outbuffer.length;
/*
		for(InetAddress a : as) {
			byte[] addr = a.getAddress();
			System.out.printf("  CanonicalHostName: '%s'\n  ", a.getCanonicalHostName());
			System.out.printf("HostName: '%s'\n    ", a.getHostName());
			for(byte nibble : addr) {
				System.out.printf("  %-3d", ((nibble < 0) ? 256 + nibble : nibble) );
			}
			System.out.println();
		}
*/		
		ArrayList<byte[]> v4addresses = new ArrayList<byte[]>();
		ArrayList<String> names = new ArrayList<String>(); 
		ArrayList<byte[]> rawNames = new ArrayList<byte[]>();
		for(InetAddress a : as) {
			byte[] address = a.getAddress(); 
			if (address.length == 4) {
				v4addresses.add(address);
			} else {
				continue; // skip non-V4 names
			}
			String name = a.getCanonicalHostName();	
			if (!names.contains(name)) { names.add(name); rawNames.add(Ebcdic.toEbcdic(name)); }
			name = a.getHostName();
			if (!names.contains(name)) { names.add(name); rawNames.add(Ebcdic.toEbcdic(name)); }
		}
		if (v4addresses.size() == 0) { return 0; } // no V4 addresses available
		
		// if we are here, we can return some data...
		// compute minimal size in buffer required for hostent + "official host name"
		byte[] h_name = (rawNames.size() == 0) ? Ebcdic.toEbcdic(hostname.toLowerCase()) : rawNames.get(0); 
		remaining -= h_name.length + 1; // + null-byte
		if (remaining <= 4) { return 0; } // no space for a single address 
		
		// compute the number of transmittable addresses
		int maxAddressCount = Math.min(v4addresses.size(), remaining / 4);
		remaining -= maxAddressCount * 4;
		
		// compute the number of possible aliases
		int aliasCount = 0;
		for (int i = 0; i < rawNames.size(); i++) {
			byte[] rawName = rawNames.get(i);
			if (rawName == h_name) { continue; }
			int reqBytes = rawName.length + 1; // + null-byte
			if (reqBytes < remaining) {
				aliasCount++;
				remaining -= reqBytes;
			} else {
				break;
			}
		}
		
		// build the outbuffer structure:
		// - (2) h_addr_type (always: AF_INET)
		// - (2) h_length    (always: 4)
		// - (4) number of addresses (-> n)
		// - (4) number of aliases   (-> m)
		// - n * 4-byte-addresses
		// - 1 * null-terminated "official" hostname
		// - m * null-terminated host-alias
		sink.putShort(ISocketProxy.AF_INET) // h_addr_type
			.putShort(4) // h_length
			.putInt(maxAddressCount)
			.putInt(aliasCount);
		
			// address table
		for (int i = 0; i < maxAddressCount; i++) {
			sink.putBytes(v4addresses.get(i));
		}
			
			// h_name incl. fill bytes
		sink.putString(h_name);
		
			// aliases
		int i = 0;
		while(aliasCount > 0) {
			byte[] rawName = rawNames.get(i++);
			if (rawName == h_name) { continue; }
			aliasCount--;
			sink.putString(rawName);
		}
		
		return sink.getCurrPos();
	}
	
	private static class SocketProxyHandler implements Runnable {
		
		private final IHostConnector connection;
		private final IErrorSink errorSink;
		private final IRequestResponse request;
		private final ISocketProxy proxy;
		private final int command;
		private final SocketManager socketManager;
		//private final int sockfd;
		
		public SocketProxyHandler(
				IHostConnector connection,
				IErrorSink errorSink,
				IRequestResponse request,
				ISocketProxy socket,
				int command,
				SocketManager socketManager,
				int sockfd) {
			this.connection = connection;
			this.errorSink = errorSink;
			this.request = request;
			this.proxy = socket;
			this.command = command;
			this.socketManager = socketManager;
			//this.sockfd = sockfd;
		}
		
		public int getRequestDataAsShort(int atOffset) {
			int dataLen = this.request.getReqDataLen();
			if (atOffset < 0 || atOffset > (dataLen-2)) { return 0;	}
			byte[] buffer = this.request.getReqData();
			short s1 = (short)((short)buffer[atOffset] & (short)0x00FF);
			short s2 = (short)((short)buffer[atOffset+1] & (short)0x00FF);
			int value = (int)((s1 << 8) | s2);
			return value;
		}
	
		public void run() {
			int rc = ISocketError.EUNSPEC;
			int respLen = 0;

			try {
				// interpret and handle command
				switch(this.command) {
				
				case CMD_SEND:
					rc = this.proxy.send(this.request.getReqData(), this.request.getReqDataLen(), this.request.getReqUserWord2());
					break;
				
				case CMD_RECV:
					rc = this.proxy.recv(this.request.getRespData(), this.getRequestDataAsShort(0), this.request.getReqUserWord2());
					respLen = (rc & 0xFFFF);
					rc &= 0xFFFF0000;
					break;
				
				case CMD_SENDTO:
					rc = this.proxy.sendTo(this.request.getReqData(), this.request.getReqDataLen(), this.request.getReqUserWord2());
					break;
				
				case CMD_RECVFROM:
					rc = this.proxy.recvFrom(this.request.getRespData(), this.getRequestDataAsShort(0), this.request.getReqUserWord2());
					respLen = (rc & 0xFFFF);
					rc &= 0xFFFF0000; 
					break;
				
				case CMD_BIND:
					rc = this.proxy.bind(this.request.getReqData(), this.request.getReqDataLen());
					break;
				
				case CMD_CONNECT:
					rc = this.proxy.connect(this.request.getReqData(), this.request.getReqDataLen());
					break;
					
				case CMD_LISTEN:
					rc = this.proxy.listen(this.request.getReqData(), this.request.getReqDataLen());
					break;
					
				case CMD_ACCEPT:
					SocketProxyAcceptResult res = this.proxy.accept(this.request.getRespData());
					rc = res.getRc();
					if (rc == ISocketError.EOK) {
						respLen = 16;
					}
					if (this.socketManager != null) {
						if (rc == ISocketError.EOK) {
							this.socketManager.set(res.getProxy());
							int sockfd = this.socketManager.get();
							rc = (rc & 0xFFFF0000) | (sockfd & 0xFFFF);
						} else {
							this.socketManager.release();
						}	
					}
					break;
					
				case CMD_GETSOCKNAME:
					rc = this.proxy.getsockname(this.request.getRespData());
					if (rc == ISocketError.EOK) {
						respLen = 16;
					}
					break;
					
				case CMD_GETPEERNAME:
					rc = this.proxy.getpeername(this.request.getRespData());
					if (rc == ISocketError.EOK) {
						respLen = 16;
					}
					break;
					
				case CMD_CLOSE:
					rc = this.proxy.close();
					if (this.socketManager != null) { this.socketManager.release(); }
					break;
					
				case CMD_SHUTDOWN:
					rc = this.proxy.shutdown(this.request.getReqData(), this.request.getReqDataLen());
					break;
					
				case CMD_GETHOSTBYNAME:
				case CMD_GETHOSTBYADDR:
					respLen = (command == CMD_GETHOSTBYNAME)
						? gethostbyname(request.getReqData(), request.getReqDataLen(), request.getRespData())
						: gethostbyaddr(request.getReqData(), request.getReqDataLen(), request.getRespData());
					if (respLen < 0) {
						rc = ISocketError.HOST_NOT_FOUND;
						respLen = 0;
					} else if (respLen == 0) {
						rc = ISocketError.NO_ADDRESS;
					} else {
						rc = ISocketError.EOK;
					}
					break;
					
				default:					
				}
				
				// send response
				request.setRespUserWord1(rc);
				request.setRespDataLen(respLen);	
				connection.sendResponse(request);
			} catch (CommProxyStateException exc) {
				this.errorSink.consumeException(exc);
			} catch (Throwable thr) {
				thr.printStackTrace();
				this.errorSink.consumeException(new CommProxyStateException("** Caught exception while processing request: " + thr.getMessage()));
			}
		}
	}
	
	private static class RcResponse implements Runnable {
		
		private final IHostConnector connection;
		private final IErrorSink errorSink;
		private final IRequestResponse request;		
		private final int rc;
		private final int respLen;
		
		public RcResponse(IHostConnector connection, IErrorSink errorSink, IRequestResponse request, int rc, int respLen) {
			this.connection = connection;
			this.errorSink = errorSink;
			this.request = request;
			this.rc = rc;
			this.respLen= respLen;
		}
		
		public RcResponse(IHostConnector connection, IErrorSink errorSink, IRequestResponse request, int rc) {
			this(connection, errorSink, request, rc, 0);
		}
		
		@Override
		public void run() {
			try {
				request.setRespUserWord1(this.rc);
				request.setRespDataLen(this.respLen);
				connection.sendResponse(this.request);
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
		int ruw1 = request.getReqUserWord1();
		int command = (ruw1 & 0xFFFF0000) >> 16;
		String commandName = (command < cmdNames.size()) ? cmdNames.get(command) : "INVALID";
		byte[] r = request.getReqData();
					
		logger.debug("getRequestHandler, command = ", commandName, "[", command, "] (ruw1.low = ", (ruw1 & 0xFFFF), ")");

		if (command == CMD_ALLOCSOCKET) {
			int ruw2 = request.getReqUserWord2();

			int af = (ruw2 & 0xFF000000) >> 24;
			if (af != ISocketProxy.AF_INET) {
				return new RcResponse(this.hostConnection, this.errorSink, request, ISocketError.EAFNOSUPPORT);
			}

			int sockNo = this.getFreeSocket();
			if (sockNo < 0) {
				return new RcResponse(this.hostConnection, this.errorSink, request, ISocketError.EMFILE);
			}
			
			int socktype = (ruw2 & 0x00FF0000) >> 16;
			int ipproto =  (ruw2 & 0x0000FF00) >> 8;
			ISocketProxy proxy = null;
			if (socktype == 1 && ipproto == 6) {         // SOCK_STREAM && IPPROTO_TCP
				proxy = new InetV4StreamSocketProxy(this.vmDefaultAddress);
			} else if (socktype == 2 && ipproto == 17) { // SOCK_DGRAM && IPPROTO_UDP
				try {
					proxy = new InetV4DatagramSocketProxy(this.vmDefaultAddress);
				} catch (Exception exc) {
					this.setSocket(sockNo, null); // free socket fd
					return new RcResponse(this.hostConnection, this.errorSink, request, ISocketError.EMFILE);
				}
			} else {
				this.setSocket(sockNo, null); // free socket fd
				return new RcResponse(this.hostConnection, this.errorSink, request, ISocketError.EPROTONOSUPPORT);
			}
			logger.debug("CMD_ALLOCSOCKET => new socket fd: ", sockNo);
			
			this.setSocket(sockNo, proxy);
			int rc = ISocketError.EOK | (sockNo & 0x0000FFFF);
			return new RcResponse(this.hostConnection, this.errorSink, request, rc);
		} else if (command == CMD_GETHOSTBYNAME && request.getReqDataLen() == 7
				   && r[0] == Ebcdic._0 && r[2] == r[0] && r[4] == r[0] && r[6] == r[0]
				   && r[1] == Ebcdic._Point && r[3] == r[1] && r[3] == r[1]) {
			// this is: gethostbyname("0.0.0.0") => return the dedicated IP-address
			InetAddress[] as = { this.vmDefaultAddress };
			int respLen = getHostent((this.vmFQN != null) ? this.vmFQN : "localhost", as, request.getRespData());
			return new RcResponse(this.hostConnection, this.errorSink, request, ISocketError.EOK, respLen);
		} else {
			int sockNo = ruw1 & 0xFFFF;
			ISocketProxy proxy = this.getSocket(sockNo);			
			if (proxy == null && command > CMD_ALLOCSOCKET) {
				return new RcResponse(this.hostConnection, this.errorSink, request, ISocketError.ENOTSOCK);
			}
			
			SocketManager smanager = null;
			if (command == CMD_ACCEPT) {
				int newSockNo = this.getFreeSocket();
				if (newSockNo < 0) {
					return new RcResponse(this.hostConnection, this.errorSink, request, ISocketError.EMFILE);
				}
				smanager = new SocketManager(newSockNo, true);
			} else if (command == CMD_CLOSE) {
				smanager = new SocketManager(sockNo, false);
			}
			
			return new SocketProxyHandler(this.hostConnection, this.errorSink, request, proxy, command, smanager, sockNo);
		}
	}
}
	
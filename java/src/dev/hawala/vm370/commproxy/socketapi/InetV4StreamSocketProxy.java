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

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;

/**
 * Implementation of a ISocketProxy for a "IPv4 STREAM" type socket.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2014
 *
 */
public class InetV4StreamSocketProxy extends AbstractInetV4Proxy implements ISocketProxy {
	
	private InetSocketAddress localAddress = null;
	private InetSocketAddress remoteAddress = null;
	
	private Socket clientSocket = null;
	private ServerSocket serverSocket = null;
	
	protected InputStream inStream = null; // remote -> local
	protected OutputStream outStream = null; // local -> remote
	
	public InetV4StreamSocketProxy(InetAddress defaultAddress) {
		super(defaultAddress);
	}
	
	public InetV4StreamSocketProxy(
			InetAddress defaultAddress,
			Socket cs,
			InetSocketAddress ra,
			InputStream is,
			OutputStream os) {
		super(defaultAddress);
		this.clientSocket = cs;
		this.remoteAddress = ra;
		this.inStream = is;
		this.outStream = os;
	}
	
	// rc <- bind(local_address) 
	// buffer usage:
	//   offset 0 -> 16 bytes :: raw local address (sockaddr) to bind to
	// rc:
	// - EACCES -- The address is protected, and the user is not the superuser
	// - EADDRINUSE -- The given address is already in use
	// - EAFNOSUPPORT -- not AF_INET
	// - EINVAL -- socket already bound to an address, invalid argument (e.g. port not 0..65535, not AF_INET)
	@Override
	public int bind(byte[] buffer, int bufferLength) {
		if (this.clientSocket != null || this.serverSocket != null) {
			return ISocketError.EINVAL;
		}
		
		int saFamily = this.getUShort(buffer, 0);
		if (saFamily != ISocketProxy.AF_INET) { return ISocketError.EAFNOSUPPORT; }

		int port = this.getUShort(buffer, 2);
		InetAddress ia = this.getInetAddress(buffer, 4);
		
		if (ia == null || port < 0) { return ISocketError.EINVAL; }
		
		InetSocketAddress isa = new InetSocketAddress(this.mapAllInetAddress(ia), port);
		
		try {
			Socket cs = new Socket();
			cs.bind(isa);
			cs.close();
		} catch (IOException e) {
			return ISocketError.EADDRINUSE;
		}
		
		this.localAddress = isa;
		return ISocketError.EOK;
	}

	// rc <- close()
	// rc:
	// - 
	@Override
	public int close() {
		if (this.clientSocket != null)
		{
			try {
				if (this.inStream != null) { try { this.inStream.close(); } catch(Exception e) {} }
				if (this.outStream != null) { try { this.outStream.close(); } catch(Exception e) {} } 
				this.clientSocket.close();
			} catch (IOException e) {
			}
			this.inStream = null;
			this.outStream = null;
			this.clientSocket = null;
			this.remoteAddress = null;
		}
		
		if (this.serverSocket != null)
		{
			try {
				this.serverSocket.close();
			} catch (IOException e) {
			}
			this.serverSocket = null;
		}
		
		return ISocketError.EOK;
	}
	
	// rc <- shutdown(how) 
	// buffer usage:
	//   offset 0 :: how (0 = in channel, 1 = out channel, 2 = both channels)
	// rc:
	// - ENOTCONN -- not a connected client socket
	// - EINVAL -- missing argument 'how'
	@Override
	public int shutdown(byte[] buffer, int bufferLength) {
		if (this.clientSocket == null) { return ISocketError.ENOTCONN; }
		if (bufferLength < 1) { return ISocketError.EINVAL; }
		
		byte how = buffer[0];
		
		if ((how == 0 || how == 2) // shutdown ingoing channel (or both channels)
			&& this.inStream != null) {
			try
			{
				this.inStream.close();
				this.clientSocket.shutdownInput();
			}
			catch (IOException e) {
				// ignored!
			}
			this.inStream = null;
		}
		
		if ((how == 1 || how == 2) // shutdown outgoing channel (or both channels)
			&& this.outStream != null) {
			try
			{
				this.outStream.close();
				this.clientSocket.shutdownOutput();
			}
			catch (IOException e) {
				// ignored!
			}
			this.outStream = null;
		}
		
		return ISocketError.EOK;
	}

	// rc <- connect(remote_address)
	// buffer usage:
	//   offset 0 -> 16 bytes :: raw remote address (sockaddr) to connect to
	// rc:
	// - EAFNOSUPPORT -- not AF_INET
	// - EINVAL -- Invalid argument (e.g. port not 0..65535)
	// - EISCONN -- socket is already connected
	// - ECONNREFUSED -- no-one listering at remote address
	// - EADDRINUSE -- local bind address already in use
	@Override
	public int connect(byte[] buffer, int bufferLength) {
		if (this.clientSocket != null || this.serverSocket != null) {
			return ISocketError.EISCONN;
		}
		
		int saFamily = this.getUShort(buffer, 0);
		if (saFamily != ISocketProxy.AF_INET) { return ISocketError.EAFNOSUPPORT; }

		int port = this.getUShort(buffer, 2);		
		InetAddress ia = this.getInetAddress(buffer, 4);
		if (ia == null || port < 0) { return ISocketError.EINVAL; }
		
		InetSocketAddress isa = new InetSocketAddress(ia, port);
		
		int errCode = ISocketError.EUNSPEC;
		try {
			Socket s = new Socket();
			if (this.localAddress != null) {
				errCode = ISocketError.EADDRINUSE;
				s.bind(this.localAddress);
			}
			errCode = ISocketError.ECONNREFUSED;
			s.connect(isa);
			s.setTcpNoDelay(true);
			this.clientSocket = s;
			this.remoteAddress = isa;
			this.inStream = s.getInputStream();
			this.outStream = s.getOutputStream();
		} catch (IOException e) {
			return errCode;
		}
		
		return ISocketError.EOK;
	}

	// rc <- listen(backlog)
	// open listening socket 
	// buffer usage:
	//   offset 0 -> 1 byte :: backlog (unsigned byte: 1..255)
	// rc:
	// - EINVAL -- Invalid argument, no valid bind()
	// - EADDRINUSE -- The given address is already in use
	// - EISCONN -- socket is already connected
	@Override
	public int listen(byte[] buffer, int bufferLength) {
		if (this.clientSocket != null || this.serverSocket != null) {
			return ISocketError.EISCONN;
		}
		if (this.localAddress == null) {
			return ISocketError.EINVAL;
		}
		
		try {
			ServerSocket s = new ServerSocket();
			s.bind(this.localAddress);
			this.serverSocket = s;
			this.localAddress = (InetSocketAddress) s.getLocalSocketAddress();
		} catch (IOException e) {
			return ISocketError.EADDRINUSE;
		}
		
		return ISocketError.EOK;
	}
	
	// {rc,proxy} <- accept(buffer)
	// wait for incoming connection
	// buffer usage (OUT):
	//   offset 0 -> 16 bytes :: src_addr (where the connection comes from)
	// rc:
	// - EINVAL -- Socket is not listening for connections
	// - ECONNABORTED -- A connection has been aborted
	// - EINTR -- interrupted by a signal that was caught before a valid connection arrived
	public SocketProxyAcceptResult accept(byte[] buffer) {
		if (this.serverSocket == null) { return new SocketProxyAcceptResult(ISocketError.EINVAL); }
		
		try {
			Socket s = this.serverSocket.accept();
			s.setTcpNoDelay(true);
			
			InetSocketAddress sockAddr = (InetSocketAddress)s.getRemoteSocketAddress();			
			this.putInetSocketAddress(sockAddr, buffer, 0);
			
			return new SocketProxyAcceptResult(
						new InetV4StreamSocketProxy(this.defaultAddress, s, sockAddr, s.getInputStream(), s.getOutputStream()));
		} catch (IOException e) {
			return new SocketProxyAcceptResult(ISocketError.ECONNABORTED);
		}
	}

	// {rc+len} <- recv(buffer, maxCount, flags)
	// receive data-packet over stream connection
	// buffer usage:
	//   offset 0 -> 1..2048 bytes :: data received
	// userWord: flags (currently unused)
	// len: byte count received (bytes in buffer used)
	// rc:
	// - ENOTCONN -- The socket is associated with a connection-oriented protocol and has not been connected.
	// - others...?
	@Override
	public int recv(byte[] buffer, int maxCount, int userWord) {
		if (this.clientSocket == null || this.inStream == null) {
			return ISocketError.ENOTCONN;
		}
	
		try {
			int len = this.inStream.read(buffer, 0, Math.min(2048, maxCount));
			if (len >= 0) {
				return ISocketError.EOK + (len & 0x0FFF);
			} else {
				return ISocketError.ECONNABORTED;
			}
		} catch (IOException e) {
			return ISocketError.ECONNABORTED;
		}
	}

	// {rc+len} <- recvfrom(buffer, maxCount, flags)
	// receive data-packet over datagram socket
	// buffer usage:
	//   offset 0 -> 16 bytes :: src_addr (where the packet comes from)
	//   offset 16 -> 1..2032 bytes :: data received
	// userWord: flags (currently unused)
	// len: byte count received (bytes in buffer used)
	// rc:
	// - ENOTCONN -- The socket is associated with a connection-oriented protocol and has not been connected.
	// - EINVAL -- not supported (connected connection-oriented protocol)
	// - others...?
	@Override
	public int recvFrom(byte[] buffer, int maxCount, int userWord) {
		return ISocketError.EINVAL;
	}

	// (rc+sentLen) <- send(data, flags)
	// send data-packet over stream connection
	// buffer usage:
	//   offset 0 -> 1..2048 bytes :: data to send
	// userWord: flags (currently unused)
	// rc:
	// - ENOTCONN -- The socket is not connected.
	// - EOPNOTSUPP -- Some bit in the flags argument is inappropriate for the socket type.
	// - ECONNABORTED  -- Connection reset by peer
	// - EDESTADDRREQ  -- The socket is not connection-mode.
	@Override
	public int send(byte[] buffer, int bufferLength, int userWord) {
		if (this.clientSocket == null || this.outStream == null) {
			return ISocketError.ENOTCONN;
		}

		try {
			this.outStream.write(buffer, 0, bufferLength);
			return ISocketError.EOK + (bufferLength & 0x0FFF);
		} catch (IOException e) {
			return ISocketError.ECONNABORTED;
		}
	}

	// (rc+sentLen) <- sendTo(data, flags, dest_addr)
	// send data-packet over datagram socket
	// buffer usage:
	//   offset 0 -> 16 bytes :: dest_addr
	//   offset 16 -> 1..2032 bytes :: data to send
	// userWord: flags (currently unused)
	// rc:
	// - ENOTCONN -- The socket is not connected and no target has been given.
	// - EOPNOTSUPP -- Some bit in the flags argument is inappropriate for the socket type.
	// - ECONNRESET  -- Connection reset by peer
	// - EDESTADDRREQ  -- The socket is not connection-mode and no peer address is set (all zeroes).
	// - EISCONN -- The socket is connected (connection-mode) but a recipient was specified
	@Override
	public int sendTo(byte[] buffer, int bufferLength, int userWord) {
		return ISocketError.EISCONN;
	}
	
	// rc <- getpeername(buffer)
	// get the address of the remote endpoint of the connected socket
	// buffer usage (OUT):
	//   offset 0 -> 16 bytes :: raw remote address (sockaddr)
	// rc:
	// - ENOTCONN -- The socket is not connected
	public int getpeername(byte[] buffer) {
		if (this.remoteAddress != null) {
			this.putInetSocketAddress(this.remoteAddress, buffer, 0);
			return ISocketError.EOK;
		}
		return ISocketError.ENOTCONN;
	}
	
	// rc <- getsockname(buffer)
	// get the local address of the socket (resp. 0.0.0.0:0 if not connected and not bound)
	// buffer usage (OUT):
	//   offset 0 -> 16 bytes :: raw local (own) address (sockaddr)
	// rc:
	// - ...?
	public int getsockname(byte[] buffer) {
		if (this.localAddress != null) {
			this.putInetSocketAddress(this.localAddress, buffer, 0);
		} else if (this.clientSocket != null) {
			this.putInetSocketAddress((InetSocketAddress)this.clientSocket.getLocalSocketAddress(), buffer, 0);
		} else {
			for(int i = 0; i < Math.min(buffer.length, 16); i++) {
				buffer[i] = (byte)0;
			}
		}
		return ISocketError.EOK;
	}
}

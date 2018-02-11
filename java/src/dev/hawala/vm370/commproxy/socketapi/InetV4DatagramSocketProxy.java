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
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketException;

/**
* Implementation of a ISocketProxy for a "IPv4 DATAGRAM" type socket.
* 
* @author Dr. Hans-Walter Latz, Berlin (Germany), 2014
*
*/
public class InetV4DatagramSocketProxy extends AbstractInetV4Proxy implements ISocketProxy {
	
	public InetV4DatagramSocketProxy(InetAddress defaultAddress) {
		super(defaultAddress);
	}
	
	// the current socket bound to a local address and socket with bind() 
	private DatagramSocket socket = null;
	private int sockPort = -1;
	private InetAddress sockAddress = null;
	
	// the remote computer to which this datagram socket is "connected" to after a connect() call
	// (target for "send()", filter for "recv()")
	private int connPort = -1;
	private InetAddress connAddress = null;
	
	private boolean noSocket() {
		if (this.socket != null) { return false; }
		try {
			this.socket = new DatagramSocket();
			return false;
		} catch (SocketException e) {
		}
		return true;
	}

	// rc <- bind(local_address) 
	// buffer usage:
	//   offset 0 -> 16 bytes :: raw local address (sockaddr) to bind to
	// rc:
	// - EADDRINUSE -- The given address is already in use
	// - EAFNOSUPPORT -- not AF_INET
	// - EINVAL -- invalid argument (e.g. port not 0..65535)
	@Override
	public int bind(byte[] buffer, int bufferLength) {
		DatagramSocket currSocket = this.socket;
		
		int saFamily = this.getUShort(buffer, 0);
		if (saFamily != ISocketProxy.AF_INET) { return ISocketError.EAFNOSUPPORT; }
		
		int port = this.getUShort(buffer, 2);
		InetAddress ia = this.getInetAddress(buffer, 4);
		
		if (ia == null || port < 0) { return ISocketError.EINVAL; }
		ia = this.mapAllInetAddress(ia);
		
		if (port == this.sockPort && this.sockAddress != null && this.sockAddress.equals(ia)) {
			// the current socket is already bound to the same port/address
			return ISocketError.EOK;
		}
		
		try {
			this.socket = new DatagramSocket(port, ia);
			this.sockPort = port;
			this.sockAddress = ia;
		} catch (SocketException e) {
			return ISocketError.EADDRINUSE;
		}
		
		if (currSocket != null) { currSocket.close(); }
		
		return ISocketError.EOK;
	}

	@Override
	public int getsockname(byte[] buffer) {
		if (this.noSocket()) { return ISocketError.EUNSPEC; }
		this.putInetSocketAddress((InetSocketAddress)this.socket.getLocalSocketAddress(), buffer, 0);
		return ISocketError.EOK;
	}

	@Override
	public int close(){
		if (this.socket != null)
		{
			this.socket.close();
			this.socket = null;
		}
		return ISocketError.EOK;
	}

	@Override
	public int recvFrom(byte[] buffer, int maxCount, int userWord) {
		if (this.noSocket()) { return ISocketError.EUNSPEC; }

		int rc = ISocketError.ECONNRESET;

		int bufLen = Math.min(maxCount, buffer.length - 16);
		if (bufLen < 1) { return ISocketError.EINVAL; }
		try {
			DatagramPacket recvPacket = new DatagramPacket(buffer, 16, bufLen);
			this.socket.receive(recvPacket);
			int len = recvPacket.getLength();
			if (len >= 0) {
				rc = ISocketError.EOK + ((len + 16) & 0x0FFF);
			} else {
				return ISocketError.ECONNRESET;
			}
			
			InetAddress senderAddress = recvPacket.getAddress();
            int senderPort = recvPacket.getPort();
            this.putInetSocketAddress(senderAddress, senderPort, buffer, 0);
		} catch(IOException exc) {
			return ISocketError.ECONNRESET;
		}
		
		return rc;
	}

	@Override
	public int sendTo(byte[] buffer, int bufferLength, int userWord) {
		if (this.noSocket()) { return ISocketError.EUNSPEC; }
		
		if (bufferLength < 17) { return ISocketError.EINVAL; }
		
		int saFamily = this.getUShort(buffer, 0);
		if (saFamily != ISocketProxy.AF_INET) { return ISocketError.EAFNOSUPPORT; }
		
		int port = this.getUShort(buffer, 2);
		InetAddress ia = this.getInetAddress(buffer, 4);
		
		DatagramPacket sendPacket = new DatagramPacket(buffer, 16, bufferLength - 16, ia, port);
		try {
			this.socket.send(sendPacket);
			return ISocketError.EOK + ((sendPacket.getLength()) & 0x0FFF);
		} catch(IOException exc) {
			return ISocketError.ECONNRESET;
		}
	}

	@Override
	public int connect(byte[] buffer, int bufferLength) {
		int saFamily = this.getUShort(buffer, 0);
		if (saFamily != ISocketProxy.AF_INET) { return ISocketError.EAFNOSUPPORT; }

		int port = this.getUShort(buffer, 2);		
		InetAddress ia = this.getInetAddress(buffer, 4);
		if (ia == null || port < 0) { return ISocketError.EINVAL; }

		this.connPort = port;
		this.connAddress = ia;
		return ISocketError.EOK;
	}

	@Override
	public int send(byte[] buffer, int bufferLength, int userWord) {
		if (this.connPort < -1 || this.connAddress == null) {
			return ISocketError.ENOTCONN; // where to send to if not "connected"?
		}
		
		if (this.noSocket()) { return ISocketError.EUNSPEC; }
		
		DatagramPacket sendPacket = new DatagramPacket(buffer, bufferLength, this.connAddress, this.connPort);
		try {
			this.socket.send(sendPacket);
		} catch(IOException exc) {
			return ISocketError.ECONNRESET;
		}
		
		return ISocketError.EOK;
	}

	@Override
	public int recv(byte[] buffer, int maxCount, int userWord) {		
		if (this.noSocket()) { return ISocketError.EUNSPEC; }

		int rc = ISocketError.ECONNRESET;

		int bufLen = Math.min(maxCount, buffer.length);
		if (bufLen < 1) { return ISocketError.EINVAL; }
		try {
			boolean isFromConnHost = false;
			while (!isFromConnHost) {
				// receive a datagram packet
				DatagramPacket recvPacket = new DatagramPacket(buffer, bufLen);
				this.socket.receive(recvPacket);
				int len = recvPacket.getLength();
				if (len >= 0) {
					rc = ISocketError.EOK + (len & 0x0FFF);
				} else {
					return ISocketError.ECONNRESET;
				}
				
				// if this socket is un-connected: each packet is a good packet
				if (this.connPort < -1 || this.connAddress == null) {
					return rc;
				}
				
				// this socket is "connected": accept packets only from the correct "connection"
				InetAddress senderAddress = recvPacket.getAddress();
	            int senderPort = recvPacket.getPort();
	            isFromConnHost = (senderPort == this.connPort && this.connAddress.equals(senderAddress));
			}
		} catch(IOException exc) {
			return ISocketError.ECONNRESET;
		}
		
		return rc;
	}

	@Override
	public int shutdown(byte[] buffer, int bufferLength) {
		return ISocketError.EOK; // successfully ignored this operation
	}
	
	/*
	 * unsupported operations on DatagramSockets
	 */

	@Override
	public SocketProxyAcceptResult accept(byte[] buffer) {
		return new SocketProxyAcceptResult(ISocketError.EOPNOTSUPP);
	}

	@Override
	public int listen(byte[] buffer, int bufferLength) {
		return ISocketError.EOPNOTSUPP;
	}

	@Override
	public int getpeername(byte[] buffer) {
		return ISocketError.EOPNOTSUPP;
	}
}

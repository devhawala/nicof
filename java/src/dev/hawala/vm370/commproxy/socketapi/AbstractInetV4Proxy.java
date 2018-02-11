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
import java.net.InetSocketAddress;
import java.net.UnknownHostException;

import static dev.hawala.vm370.ebcdic.PlainHex.*;

/**
 * Base Implementation of a ISocketProxy for a "IPv4" type socket, providing the
 * common functionality for encoding/decoding IPv4-addresses from a Level-0 transmission
 * buffer and the basic mapping of the "any" IP address (0.0.0.0) either to a specific
 * address configured for the client-VM or 127.0.0.1 as default.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2014
 *
 */
public abstract class AbstractInetV4Proxy {
	
	//private static Log logger = Log.getLogger();
	
	protected final InetAddress defaultAddress;
	
	protected AbstractInetV4Proxy(InetAddress defaultAddress) {
		this.defaultAddress = defaultAddress;
	}
	
	protected InetAddress mapAllInetAddress(InetAddress addr) {
		byte[] rawAddr = addr.getAddress();
		if (this.defaultAddress != null
			&& rawAddr.length >= 4
			&& rawAddr[0] == _00
			&& rawAddr[1] == _00
			&& rawAddr[2] == _00
			&& rawAddr[3] == _00) {
			//logger.info("mapAllInetAddress(",rawAddr[0],".",rawAddr[1],".",rawAddr[2],".",rawAddr[3],")",
			//		"\n ==> ", (this.defaultAddress != null) ? this.defaultAddress.toString() : "**NULL**");
			return this.defaultAddress;
		}
		//logger.info("mapAllInetAddress(",rawAddr[0],".",rawAddr[1],".",rawAddr[2],".",rawAddr[3],") => unchanged");
		return addr;
	}
	
	protected InetAddress getInetAddress(byte[] buffer, int at) {
		if ((buffer.length-at) < 4) { return null; }
		byte[] rawIa = new byte[4];
		System.arraycopy(buffer, at, rawIa, 0, 4);
		try {
			InetAddress ia = InetAddress.getByAddress(rawIa);
			return ia;
		} catch (UnknownHostException e) {
			return null;
		}
	}
	
	protected void putInetAddress(byte[] buffer, int at, InetAddress ia) {
		byte[] rawIa = ia.getAddress();
		if ((buffer.length-at) < 4 || rawIa.length != 4) { return; }
		System.arraycopy(rawIa, 0, buffer, at, 4);
	}
	
	protected int getUShort(byte[] buffer, int at) {
		if ((buffer.length-at) < 2) { return -1; }
		short s1 = (short)((short)buffer[at] & (short)0x00FF);
		short s2 = (short)((short)buffer[at+1] & (short)0x00FF);
		int value = (int)((s1 << 8) | s2);
		return value;
	}
	
	protected void putUShort(byte[] buffer, int at, int value) {
		if ((buffer.length-at) < 2) { return; }
		int valHi = (value & 0xFF00) >> 8;
		int valLo = value & 0xFF;
		buffer[at] = (byte)valHi;
		buffer[at+1] = (byte)valLo;
	}
	
	protected void putInetSocketAddress(InetSocketAddress sockAddr, byte[] buffer, int at) {
		this.putInetSocketAddress(sockAddr.getAddress(), sockAddr.getPort(), buffer, at);
	}
	
	protected void putInetSocketAddress(InetAddress ia, int port, byte[] buffer, int at) {
		this.putUShort(buffer, at, ISocketProxy.AF_INET);
		this.putUShort(buffer, at + 2, port);
		this.putInetAddress(buffer, at + 4, ia);
		this.putUShort(buffer, at + 8, 0);
		this.putUShort(buffer, at + 10, 0);
		this.putUShort(buffer, at + 12, 0);
		this.putUShort(buffer, at + 14, 0);
	}
}

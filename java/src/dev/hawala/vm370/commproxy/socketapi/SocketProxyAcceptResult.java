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

/**
 * Helper class to transport the result of an accept() operation on a socket.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2014
 *
 */
public class SocketProxyAcceptResult {

	private final int rc;
	private final ISocketProxy socketProxy;
	
	public SocketProxyAcceptResult(int rc) {
		this.rc = rc;
		this.socketProxy = null;
	}
	
	public SocketProxyAcceptResult(ISocketProxy socketProxy) {
		this.rc = ISocketError.EOK;
		this.socketProxy = socketProxy;
	}
	
	public SocketProxyAcceptResult(int rc, ISocketProxy socketProxy) {
		this.rc = rc;
		this.socketProxy = socketProxy;
	}
	
	public int getRc() { return this.rc; }
	
	public ISocketProxy getProxy() { return this.socketProxy; }
}

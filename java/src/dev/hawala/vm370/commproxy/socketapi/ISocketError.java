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
 * Constants for the socket API 'errno'/'h_errno' error codes sent
 * back to the client.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2014
 *
 */
public interface ISocketError {

	public final int RCBASE = 0x01000000;
	
	public final int EOK = RCBASE + 0; // operation successfull
	
	public final int EAFNOSUPPORT = RCBASE + 0x010000; // address family not supported
	
	public final int EPROTONOSUPPORT = RCBASE + 0x020000; // protocol(type) not supported or not matching socket type 
	
	public final int EMFILE = RCBASE + 0x030000; // process file table overflow (no free fileno available!)
	
	public final int ENOTSOCK = RCBASE + 0x040000; // not a valid socket (fileno not valid!)
	
	public final int EUNSPEC = RCBASE + 0x050000; // unspecified error
	
	public final int EINVAL = RCBASE + 0x070000;
	
	public final int EACCES = RCBASE + 0x080000;
	
	public final int EADDRINUSE = RCBASE + 0x090000;
	
	public final int ENOTCONN = RCBASE + 0x0A0000;
	
	public final int EOPNOTSUPP = RCBASE + 0x0B0000;
	
	public final int ECONNRESET = RCBASE + 0x0C0000;
	
	public final int EDESTADDRREQ = RCBASE + 0x0D0000;
	
	public final int EISCONN = RCBASE + 0x0E0000;
	
	public final int ECONNABORTED = RCBASE + 0x0F0000;
	
	public final int ECONNREFUSED = RCBASE + 0x100000;
	
	public final int HOST_NOT_FOUND = RCBASE + 0x200000; // from gethostbyname() or gethostbyaddr()
	
	public final int NO_ADDRESS = RCBASE + 0x210000; // from gethostbyname() or gethostbyaddr()

	
}

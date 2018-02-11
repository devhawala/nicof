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
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2014
** Released to the public domain.
*/

package dev.hawala.vm370.commproxy.socketapi;

/**
 * Interface defining the functionality to be provided by a single proxy
 * providing a single socket accessible form the inside.
 * 
 * An instance of the interface can implement an arbitrary socket type
 * (i.e. stream or datagram, address family).
 * 
 * The specific class to be instantiated is known to the Socket-API handler
 * at level 0 when a socket is created by the client. 
 * Any socket usage afterwards goes exclusively through the functionality
 * defined in this interface, allowing to handle all operations on arbitrary
 * sockets in a unified way.
 * 
 * Each socket currently allocated by a CMS client is represented by an instance
 * of this interface. 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2014
 *
 */
public interface ISocketProxy {
	
	/* support address families */
	
	public final int AF_INET  = 2;
	

	// rc <- bind(local_address) 
	// buffer usage:
	//   offset 0 -> 16 bytes :: raw local address (sockaddr) to bind to
	// rc:
	// - EACCES -- The address is protected, and the user is not the superuser
	// - EADDRINUSE -- The given address is already in use
	// - EINVAL -- Invalid argument (e.g. port not 0..65535, not AF_INET)
	public int bind(byte[] buffer, int bufferLength);
	
	// rc <- connect(remote_address)
	// buffer usage:
	//   offset 0 -> 16 bytes :: raw remote address (sockaddr) to connect to
	// rc:
	// - (to-be-defined)
	public int connect(byte[] buffer, int bufferLength);
	
	// rc <- listen(backlog)
	// open listening socket 
	// buffer usage:
	//   offset 0 -> 1 byte :: backlog (unsigned byte: 1..255)
	// rc:
	// - EADDRINUSE -- The given address is already in use
	public int listen(byte[] buffer, int bufferLength);
	
	// {rc,proxy} <- accept(buffer)
	// wait for incoming connection
	// buffer usage OUT):
	//   offset 0 -> 16 bytes :: src_addr (where the connection comes from)
	// rc:
	// - EINVAL -- Socket is not listening for connections
	// - ECONNABORTED -- A connection has been aborted
	// - EINTR -- interrupted by a signal that was caught before a valid connection arrived
	public SocketProxyAcceptResult accept(byte[] buffer);
	
	// rc <- getpeername()
	// get the address of the remote endpoint of the connected socket
	// buffer usage (OUT):
	//   offset 0 -> 16 bytes :: src_addr (where the connection comes from)
	// rc:
	// - ENOTCONN -- The socket is not connected
	public int getpeername(byte[] buffer);
	
	// rc <- getsockname()
	// get the local address of the socket (resp. 0.0.0.0:0 if not connected and not bound)
	// buffer usage (OUT):
	//   offset 0 -> 16 bytes :: src_addr (where the connection comes from)
	// rc:
	// - ...?
	public int getsockname(byte[] buffer);
	
	// {rc+len} <- send(data, flags)
	// send data-packet over stream connection
	// buffer usage:
	//   offset 0 -> 1..2048 bytes :: data to send
	// userWord: flags (currently unused)
	// rc:
	// - ENOTCONN -- The socket is not connected.
	// - EOPNOTSUPP -- Some bit in the flags argument is inappropriate for the socket type.
	// - ECONNRESET  -- Connection reset by peer
	// - EDESTADDRREQ  -- The socket is not connection-mode.
	public int send(byte[] buffer, int bufferLength, int userWord);
	
	// {rc+len} <- sendTo(data, flags, dest_addr)
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
	public int sendTo(byte[] buffer, int bufferLength, int userWord);
	
	// {rc+len} <- recv(buffer, maxCount, flags)
	// receive data-packet over stream connection
	// buffer usage:
	//   offset 0 -> 1..2048 bytes :: data received
	// userWord: flags (currently unused)
	// len: byte count received (bytes in buffer used)
	// rc:
	// - ENOTCONN -- The socket is associated with a connection-oriented protocol and has not been connected.
	// - others...?
	public int recv(byte[] buffer, int maxCount, int userWord);
	
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
	public int recvFrom(byte[] buffer, int maxCount, int userWord);
	
	// rc <- shutdown(how) 
	// buffer usage:
	//   offset 0 :: how (0 = in channel, 1 = out channel, 2 = both channels)
	// rc:
	// - ENOTCONN -- not a connected client socket
	// - EINVAL -- missing argument 'how'
	public int shutdown(byte[] buffer, int bufferLength);
	
	// rc <- close()
	// rc:
	// - ENOTCONN -- The socket is associated with a connection-oriented protocol and has not been connected.
	public int close();
}

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

import java.io.IOException;

/**
 * Interface for a connector to the VM/370 Host, managing the connection
 * and the bidirectional data transfer of single data blocks at Level-Zero.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 * 
 */
public interface IHostConnector {
	
	/**
	 * Check if a working connection is opened to the host.
	 * 
	 * @return the connection state.
	 */
	public boolean isConnected();
	
	/**
	 * Open the connection if currently not connected.
	 * 
	 * @throws CommProxyStateException the connection cannot be build up
	 * 	correctly.
	 */
	public void connect() throws CommProxyStateException;

	/**
	 * The maximal packet length in bytes.
	 */
	public final static int MAX_PACKET_LEN = 2048;

	/**
	 * Wait for a data packet to process, handling any handshake communication
	 * transparently. 
	 * @return the data packet received or <tt>null</tt> if there was only handshaking
	 *   with the host.
	 * @throws CommProxyStateException the connection was not operational on invocation
	 *   or became unusable while receiving the data packet. 
	 * @throws IOException general java exception indicating the device specific 
	 *   communication problem.
	 */
	public IRequestResponse receiveRecord() throws CommProxyStateException, IOException;

	/**
	 * Initiate the transmission of the passed response to a received data packet, possibly
	 * enqueuing the packet for deferred transmission.
	 * @param resp the data packet to transmit.
	 * @throws CommProxyStateException the connection was not operational on invocation
	 *   or became unusable while attempting to transmit the data packet.
	 * @throws IOException general java exception indicating the device specific 
	 *   communicatio problem.
	 */
	public void sendResponse(IRequestResponse resp) throws CommProxyStateException, IOException;

}
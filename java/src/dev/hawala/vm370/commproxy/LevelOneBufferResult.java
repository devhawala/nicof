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

/**
 * Result of handling a service request at Level-One, defining a "standard" result consisting
 * in a returncode, a controlword and optionally in a response data buffer.   
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public class LevelOneBufferResult implements ILevelOneResult {

	private final int rc;
	private final int controlData;
	private final int bufferLength;
	
	/**
	 * Constructor for a service request result.
	 * @param rc the returncode for the request.
	 * @param controlData the controlword for the response.
	 * @param bufferLength the amount of bytes in the request-response object to be
	 *   used as response data packet. 
	 */
	public LevelOneBufferResult(int rc, int controlData, int bufferLength) {
		this.rc = rc;
		this.controlData = controlData;
		this.bufferLength = Math.max(0, Math.min(IHostConnector.MAX_PACKET_LEN, bufferLength));
	}
	
	/**
	 * Get the returncode for the request.
	 * @return the returncode for the request.
	 */
	public int getRc() { return this.rc; }
	
	/**
	 * Get the controlword for the response.
	 * @return the controlword for the response.
	 */
	public int getControlData() { return this.controlData; }
	
	/**
	 * Get the amount of bytes in the request-response object to be used
	 * as response data packet.
	 * @return the amount of bytes in the request-response object to be
	 *   used as response data packet.
	 */
	public int getBufferLength() { return this.bufferLength; }
}

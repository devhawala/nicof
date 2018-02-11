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
 * Representation of a data packet at Level-Zero.
 * 
 *  An instance of the interface contains the data packet with the request
 *  from the host and takes the response data for the transmission back to
 *  the host.  
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public interface IRequestResponse {

	/**
	 * Get the slot id assigned to the request at the host (inside) host proxy.  
	 *  
	 * @return the slot id.
	 */
	public short getSlot();
	
	/**
	 * Get the username (VM-name) at the VM/370 host which sent the request.
	 *  
	 * @return the name of the  client-VM which sent the request.
	 */
	public byte[] getReqUser();

	/**
	 * Get the request user word 1.
	 * 
	 * @return the first user word of the request.
	 */
	public int getReqUserWord1();

	/**
	 * Get the request user word 2.
	 * 
	 * @return the second user word of the request.
	 */
	public int getReqUserWord2();

	/**
	 * Get the buffer containing the data packet of the request.
	 * 
	 * @return the data buffer.
	 */
	public byte[] getReqData();

	/**
	 * Get the length of the request data packet transmitted with the
	 * request.
	 * 
	 * @return the data packet length.
	 */
	public int getReqDataLen();

	/**
	 * Get the user word 1 for the response to the host.
	 * 
	 * @return the first user word to be sent with the request.
	 */
	public int getRespUserWord1();

	/**
	 * Set the user word 1 for the response to the host.
	 * 
	 * @param userWord1 the first user word to send with the request.
	 */
	public void setRespUserWord1(int userWord1);

	/**
	 * Get the user word 2 for the response to the host.
	 * 
	 * @return the second user word to be sent with the request.
	 */
	public int getRespUserWord2();

	/**
	 * Set the user word 2 for the response to the host.
	 * 
	 * @param userWord2 the second user word to send with the request.
	 */
	public void setRespUserWord2(int userWord2);

	/**
	 * Get the data buffer where to put the response data packet.
	 * 
	 * @return the response data buffer.
	 */
	public byte[] getRespData();

	/**
	 * Get the current length assigned for the response data packet.
	 * @return the response packet length.
	 */
	public int getRespDataLen();

	/**
	 * Set the length of the data packet stored in the response buffer.
	 * @param len the new length of the response data packet.
	 */
	public void setRespDataLen(int len);

}
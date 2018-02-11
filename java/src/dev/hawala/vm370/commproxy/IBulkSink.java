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
 * Interface for a bulk sink data stream from the VM/370 host to a external service
 * (at Level-One).
 * <p>
 * If a Level-One service returns an instance of this interface, the Level-One
 * base services can automatically handle the data transfer with a unidirectional
 * metaphor (write-only-stream) at the VM/370 side similarly to <tt>fwrite()</tt> 
 * or <tt>fputs()</tt> when using file streams.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public interface IBulkSink extends ILevelOneResult {
	
	/**
	 * State (see <tt>getState()</tt>) indicating that the sink is
	 * in usable state.
	 */
	public final static int STATE_OK = 0;
	
	/**
	 * State (see <tt>getState()</tt>) indicating that the stream is (already)
	 * closed.
	 */
	public final static int STATE_TARGET_CLOSED = -1;
	
	/**
	 * State (see <tt>getState()</tt>) indicating that the last operation
	 * failed because the target was not able to receive more data to handle
	 * (e.g. when the disk to write to is full). 
	 */
	public final static int STATE_TARGET_MEDIA_FULL = -2;
	
	/**
	 * State (see <tt>getState()</tt>) indicating that the last operation
	 * failed (general write error).
	 */
	public final static int STATE_WRITE_ERROR = -3;
	
	/**
	 * Write a chunk of byte to the sink.
	 * @param buffer the byte buffer containing the data to be written.
	 * @param length the number of valid bytes in the buffer. 
	 */
	public void putBlock(byte[] buffer, int length);
	
	/**
	 * Close the stream.
	 */
	public void close();
	
	/**
	 * Get the current state of the sink, either one of the predefined states
	 * in this interface or an implementation specific code known to the client.
	 * @return the state of the bulk sink.
	 */
	public int getState();
}

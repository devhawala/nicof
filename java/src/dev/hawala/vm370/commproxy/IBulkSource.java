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
 * Interface for a bulk source data stream from an external service to the VM/370 host
 * (at Level-One).
 * <p>
 * If a Level-One service returns an instance of this interface, the Level-One
 * base services can automatically handle the data transfer with a unidirectional
 * metaphor (read-only stream) at the VM/370 side similarly to <tt>fread()</tt> 
 * or <tt>fgets()</tt> when using file streams.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public interface IBulkSource extends ILevelOneResult {
	
	/**
	 * State (see <tt>getState()</tt>) indicating that the source is
	 * in usable state.
	 */
	public final static int STATE_OK = 0;
	
	/**
	 * State (see <tt>getState()</tt>) indicating that the stream is (already)
	 * closed.
	 */
	public final static int STATE_SOURCE_CLOSED = -1;
	
	/**
	 * State (see <tt>getState()</tt>) indicating that the stream has no more
	 * data to read (~ End-of-Stream).
	 */
	public final static int STATE_SOURCE_ENDED = -2;
	
	/**
	 * State (see <tt>getState()</tt>) indicating that the last operation
	 * failed (general read error).
	 */
	public final static int STATE_READ_ERROR = -3;

	/**
	 * Get the total remaining number of bytes that can be currently read from the source.
	 * @return the remaining number of bytes in the source or -1 if unknown.
	 */
	public int getRemainingCount();
	
	/**
	 * Get the number of bytes that can be read before blocking.
	 * @return the number of bytes available until the source will block or -1 if unknown.
	 */
	public int getAvailableCount();
	
	/**
	 * Read the next chunk of data into the passed buffer.
	 * @param buffer the buffer to fill, the length of the array determines the maximum length to be read.
	 * @param availableOnly restrict reading to the number of bytes available before blocking.
	 * @return the number of bytes filled into the buffer, possibly 0 if 'availableOnly' is true and 
	 *   the stream would block. 
	 */
	public int getNextBlock(byte[] buffer, boolean availableOnly);
	
	/**
	 * Close the stream.
	 */
	public void close();
	
	/**
	 * Get the current state of the source, either one of the predefined states
	 * in this interface or an implementation specific code known to the client.
	 * @return the state of the bulk source.
	 */
	public int getState();
}

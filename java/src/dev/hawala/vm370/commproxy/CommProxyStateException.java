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
 * Exception signaling that the communication with the inside proxy failed (connection
 * could not be established, no longer dialed, ...) 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public class CommProxyStateException extends Exception {
	
	private static final long serialVersionUID = -9113266937372781046L;
	
	private final boolean unrecoverable;

	/**
	 * Constructor.
	 */
	public CommProxyStateException() {
		super();
		this.unrecoverable = false;
	}

	/**
	 * Constructor.
	 * @param message Message text.
	 * @param innerException Exception caught.
	 */
	public CommProxyStateException(String message, Throwable innerException) {
		super(message, innerException);
		this.unrecoverable = false;
	}

	/**
	 * Constructor.
	 * @param message Message text.
	 */
	public CommProxyStateException(String message) {
		super(message);
		this.unrecoverable = false;
	}

	/**
	 * Constructor.
	 * @param message Message text.
	 * @param unrecoverable state information about the error
	 */
	public CommProxyStateException(boolean unrecoverable, String message) {
		super(message);
		this.unrecoverable = unrecoverable;
	}

	/**
	 * Constuctor.
	 * @param innerException Exception caught.
	 */
	public CommProxyStateException(Throwable innerException) {
		super(innerException);
		this.unrecoverable = false;
	}

	/**
	 * 
	 * @return whether it is meaning full to continue working with this communication link
	 */
	public boolean isUnrecoverable() {
		return this.unrecoverable;
	}
}

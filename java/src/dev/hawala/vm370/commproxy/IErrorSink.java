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
 * Interface defining the functionality for request handler to report an
 * exception to the main thread of the outside proxy.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public interface IErrorSink {

	/**
	 * Pass a caught exception to the main processing thread from an asynchronous
	 * request processing.
	 * @param exc the exception to be transmitted to the main processing thread.
	 */
	public void consumeException(CommProxyStateException exc);
}

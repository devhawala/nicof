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
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2011,2012
** Released to the public domain.
*/

package dev.hawala.vm370.commproxy;

import dev.hawala.vm370.ebcdic.EbcdicHandler;

/**
 * Interface that a Level-Zero service must implement to be usable by the proxy
 * infrastructure. There can be only one Level-Zero handler for a proxy, i.e.
 * this handler must process all requests coming from the host over this 
 * "inside-outside" proxy connection.
 * <p>
 * The proxy will allocate one instance of the class for the service (which must
 * implement this interface) for each client-VM (VM/370 user) wishing to use 
 * the service.
 * <p> 
 * After initialization, each incoming request from this user is processed 
 * asynchronously. For this, the infrastructure invokes the <tt>getRequestHandler()</tt>
 * method to get the <tt>Runnable</tt>-instance which will process the request
 * in a separate thread.
 * <p>
 * This <tt>Runnable</tt>-object must send back the response using the 
 * <tt>IHostProxy</tt>-instance passed at initialization.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public interface ILevelZeroHandler extends Cloneable {
	
	/**
	 * Initialize the Level-Zero handler with the data and communication objects that
	 * will be used for all requests.
	 * @param configuration the properties-bag to use for further configuration
	 *   of the service.
	 * @param clientVm the username of the VM/370-VM for which this instance will
	 *   work. 
	 * @param hostConnection the host connection to use to sending the response.
	 * @param errorSink the sink where to direct errors caught.
	 */
	public void initalize(PropertiesExt configuration, EbcdicHandler clientVm, IHostConnector hostConnection, IErrorSink errorSink);
	
	/**
	 * Free all resources bound to this handler;  
	 */
	public void deinitialize();

	/**
	 * Return the handler for a single host packet, dispatchable in a thread.
	 * @param request object having the data sent from the host to be processed.
	 * @return the dispatchable object, which will create the response and transmit it with
	 *   the passed host connection.
	 */
	public Runnable getRequestHandler(IRequestResponse request) throws CommProxyStateException;
}

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
 * Interface that a Level-One service must implement to be usable by the
 * Level-One infrastructure.
 * <p>
 * One instance of a Level-One service is allocated for each client-VM 
 * (VM/370 user) wishing to use the service.
 * <p>
 * In difference to a Level-Zero service, more than one Level-One service can
 * be configured for a "inside-outside" proxy connection, as a dispatching will
 * be done by the Level-One infrastructure (which is in fact a Level-Zero service
 * which implements the communication at Level-One). As a small drawback, only 
 * one of the 2 userwords provided by the NICOF communication is available to
 * a Level-One service, as the other is used to transport the dispatching and
 * returncode information. The remaining usable userword is called "controlData"
 * for Level-One services.
 * <p>
 * A Level-One service can provide "higher level" services (above mere data packet
 * exchange) more easier than at Level-Zero, as the Level-One infrastructure provides
 * the notion of data streams between the "inside" (VM/370) and "outside" (here)
 * and of commands to selects sub-services sent by the client.  
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 * @see dev.hawala.vm370.commproxy.IBulkSink
 * @see dev.hawala.vm370.commproxy.IBulkSource
 */
public interface ILevelOneHandler {
	
	/**
	 * Initialize the Level-One handler  with the data and communication objects that
	 * will be used for all requests.
	 * @param name the configured name of the service for which this handler
	 *   is allocated and has to provide the service; this name should be used
	 *   when reading the own configuration data from the parameter 
	 *   <tt>configuration</tt>.
	 * @param clientVm the username of the VM/370-VM for which this instance will
	 *   work.
	 * @param configuration the properties-bag to use for further configuration
	 *   of the service.
	 */
	public void initialize(String name, EbcdicHandler clientVm, PropertiesExt configuration);
	
	/**
	 * Free all resources bound to this handler;  
	 */
	public void deinitialize();

	/**
	 * Process a Level-One request from the host.
	 * @param cmd the command id identifying the sub-service to be provided in
	 *   this request.
	 * @param controlData the additional (input) 32-bit control value provided 
	 *   by the caller for this request. 
	 * @param requestData the buffer containing the data block received for
	 *   this request.
	 * @param requestDataLength the length of the data block received (only the
	 *   first <tt>requestDataLength</tt> bytes in <tt>requestData</tt> may be
	 *   used when processing the request).
	 * @param responseBuffer the buffer where the response data block has to
	 *   be put. 
	 * @return the outcome of the request. Depending on the subtype of the returned
	 *   object, different result types can be encoded:
	 *   <p>
	 *   <i>{@link dev.hawala.vm370.commproxy.LevelOneBufferResult}</i>:
	 *   the result consists of a returncode, a data block and a (output)
	 *   32-bit control value.
	 *   <p>
	 *   <i>{@link dev.hawala.vm370.commproxy.IBulkSink}</i>:
	 *   the result is a data stream from the host to the outside service.
	 *   <p>
	 *   <i>{@link dev.hawala.vm370.commproxy.IBulkSource}</i>:
	 *   the result is a data stream from the outside service to the host.
	 */
	public ILevelOneResult processRequest(
			short cmd, 
			int controlData,
			byte[] requestData, 
			int requestDataLength,
			byte[] responseBuffer);
}

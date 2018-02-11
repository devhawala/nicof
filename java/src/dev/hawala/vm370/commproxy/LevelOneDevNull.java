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

import dev.hawala.vm370.ebcdic.EbcdicHandler;

/**
 * Test Level-One service implementing a /dev/null, i.e. swallowing all requests
 * and always returning "success".
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public class LevelOneDevNull implements ILevelOneHandler {

	@Override
	public void initialize(String name, EbcdicHandler clientVm, PropertiesExt configuration) {
		// TODO Auto-generated method stub
	}

	@Override
	public void deinitialize() {
		// TODO Auto-generated method stub
	}

	@Override
	public ILevelOneResult processRequest(
			short cmd,
			int controlData,
			byte[] requestData,
			int requestDataLength,
			byte[] responseBuffer) {		
		return new LevelOneBufferResult((short)0, 0, 0); // RC = 0, controlword = 0, no data
	}
}

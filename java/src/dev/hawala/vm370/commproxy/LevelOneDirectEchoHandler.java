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

import dev.hawala.vm370.ebcdic.Ebcdic;
import dev.hawala.vm370.ebcdic.EbcdicHandler;

/**
 * Echo service working like the corresponding Level-Zero handler (so only the controlword
 * is echoed at Level-One instead of both userwords available at Level-Zero).
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public class LevelOneDirectEchoHandler implements ILevelOneHandler {

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
		
		if (requestDataLength > 0) {
			System.arraycopy(requestData, 0, responseBuffer, 0, requestDataLength);
			responseBuffer[0] = Ebcdic._A;
			responseBuffer[17] = Ebcdic._A;
			responseBuffer[253] = Ebcdic._A;
			responseBuffer[2047] = Ebcdic._A;
		}
		
		return new LevelOneBufferResult(cmd, controlData, requestDataLength); // cmd is echoed as RC
	}
}

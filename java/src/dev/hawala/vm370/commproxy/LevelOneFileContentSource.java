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

import java.io.IOException;
import java.io.InputStream;

/**
 * IBulkSource-Wrapper for a standard InputStream. 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012,2014
 *
 */

public class LevelOneFileContentSource implements IBulkSource {
	
	private int state = IBulkSource.STATE_OK;
	
	private InputStream is = null;
	private int remainingBytes = 0;

	public LevelOneFileContentSource(InputStream fis, int fileLength) {
		this.remainingBytes = fileLength;
		this.is = fis;
	}
	
	@Override
	public void close() {
		if (this.is != null) {
			try {
				this.is.close();
			} catch (IOException e) {
				// ignored
			}
			this.is = null;
		}
		this.state = IBulkSource.STATE_SOURCE_CLOSED;
	}

	@Override
	public int getNextBlock(byte[] buffer, boolean availableOnly) {
		// check if there is nothing to transmit
		if (this.state != IBulkSource.STATE_OK) { return 0; }
		if (this.is == null) {
			this.state = IBulkSource.STATE_SOURCE_ENDED;
			return 0;
		}
		
		int count = 0;
		try {
			count = this.is.read(buffer);
		} catch (Exception e) {
			this.state = IBulkSource.STATE_READ_ERROR;
			return 0;
		}
		
		if (count < 0) {
			this.state = IBulkSource.STATE_SOURCE_ENDED;
			return 0;
		}
		
		return count;
	}

	@Override
	public int getAvailableCount() { return Math.min(2048, this.remainingBytes); }

	@Override
	public int getRemainingCount() { return this.remainingBytes; }

	@Override
	public int getState() { return this.state; }
}

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
import java.io.OutputStream;

import dev.hawala.vm370.Log;

/**
 * IBulkSink-Wrapper for a standard OutputStream. 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012,2014
 *
 */

public class LevelOneFileContentSink implements IBulkSink {
	
	private static Log logger = Log.getLogger();
	
	private final String filename;
	private OutputStream os;
	
	private int state = IBulkSink.STATE_OK;
	
	public LevelOneFileContentSink(String filename, OutputStream fos) {
		this.filename = filename;
		this.os = fos;
	}

	@Override
	public void close() {
		if (this.os != null) {
			try {
				this.os.close();
			} catch (IOException e) {
				// ignored
			}
			this.os = null;
		}
		this.state = IBulkSink.STATE_TARGET_CLOSED; 
	}

	@Override
	public int getState() { return this.state; }

	@Override
	public void putBlock(byte[] buffer, int length) {
		// check if we are still able to write
		if (this.state != IBulkSource.STATE_OK) {
			logger.warn("putBlock: this.state != IBulkSource.STATE_OK");
			return;
		}
		if (this.os == null) {
			logger.warn("putBlock: this.fos == null");
			this.state = IBulkSink.STATE_TARGET_CLOSED;
			return;
		}
		
		// write this block away
		if (length <= 0) { return; }
		try {
			this.os.write(buffer, 0, length);
		} catch (Exception exc) {
			logger.error("** Error writing to file '", filename, "', Exception: ", exc);
			this.state = IBulkSink.STATE_WRITE_ERROR;
		}
	}
}

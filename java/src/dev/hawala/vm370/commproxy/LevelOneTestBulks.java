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

import static dev.hawala.vm370.ebcdic.Ebcdic.*;
import dev.hawala.vm370.ebcdic.EbcdicHandler;

/**
 * Level-One test service returning different kinds of Level-One bulk streams.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public class LevelOneTestBulks implements ILevelOneHandler {

	@Override
	public void deinitialize() {}

	@Override
	public void initialize(String name, EbcdicHandler clientVm, PropertiesExt configuration) {}

	@Override
	public ILevelOneResult processRequest(
			short cmd,
			int controlData,
			byte[] requestData,
			int requestDataLength,
			byte[] responseBuffer) {
		if (cmd == 1) {
			return new BulkSourceText(controlData);
		} else if (cmd == 2) {
			return new BulkSourceBin(controlData);
		} else if (cmd == 3) {
			return new BulkSinkText(controlData);
		} else if (cmd == 4) {
			return new BulkSinkBin(controlData);
		}
		
		return new LevelOneProtErrResult(42); // invalid command
	}
	
	static private class BulkSourceText implements IBulkSource {
		
		private int remainingLines;
		
		private final static String[] lines = {
		   //12345678901234567890123456789012345678901234567890         -> 128 bytes in total
			"Line 1: .....|"                                 + "\r\n",  //  16 bytes
			"Line 2: .....................|"                 + "\r\n",  //  32 bytes
			"Line 3: .....................................|" + "\r\n",  //  48 bytes
			"Line 4: .....................|"                 + "\r\n"   //  32 bytes
		};
		
		private int state = IBulkSource.STATE_OK;
		
		private int remainingBytes = 0;
		
		private StringBuffer sb = new StringBuffer();
		
		public BulkSourceText(int linesToReturn) {
			this.remainingLines = Math.abs(linesToReturn);
			
			for (int i = 0; i < linesToReturn; i++) {
				this.remainingBytes += lines[i % 4].length();
			}
			
			if (this.remainingLines == 0) { this.state = IBulkSource.STATE_SOURCE_ENDED; }
		}

		@Override
		public void close() { this.state = IBulkSource.STATE_SOURCE_CLOSED; }

		@Override
		public int getNextBlock(byte[] buffer, boolean availableOnly) {
			// check if there is nothing to transmit
			if (this.state != IBulkSource.STATE_OK) { return 0; }
			
			// we assume the the buffer has always 2048 bytes...
			this.sb.setLength(0);
			int i = 0;
			while(this.sb.length() < 2048 && this.remainingLines-- > 0) {
				this.sb.append(lines[i++ % 4]);
			}
			int len = this.sb.length();
			System.arraycopy(this.sb.toString().getBytes(), 0, buffer, 0, len);
			this.remainingBytes -= len;
			if (this.remainingBytes <= 0) { this.state = IBulkSource.STATE_SOURCE_ENDED; }
			return len;
		}

		@Override
		public int getAvailableCount() { return Math.min(2048, this.remainingBytes); }

		@Override
		public int getRemainingCount() { return this.remainingBytes; }

		@Override
		public int getState() { return this.state; }
	}
	
	private static class BulkSourceBin implements IBulkSource {
		
		private final int lrecl;
		
		private int remainingRecords = 0;
		private int remainingBytes = 0;
		
		private int state = IBulkSource.STATE_OK;
		
		private static byte[] fillChars = { _1, _2, _3, _4, _5, _6, _7, _8, _9, _A };
		private int currFillChar = 0;
		private int currRecPos = 0;
		
		public BulkSourceBin(int controlWord) {
			this.lrecl = Math.max(1, controlWord & 0xFF);
			this.remainingRecords = controlWord >> 8;
			this.remainingBytes = this.lrecl * this.remainingRecords;
			if (this.remainingRecords == 0) { this.state = IBulkSource.STATE_SOURCE_ENDED; }
		}

		@Override
		public void close() { this.state = IBulkSource.STATE_SOURCE_CLOSED; }

		@Override
		public int getNextBlock(byte[] buffer, boolean availableOnly) {
			// check if there is nothing to transmit
			if (this.state != IBulkSource.STATE_OK) { return 0; }
		
			int count = 0;
			while (count < 2048 && this.remainingRecords >= 0) {
				buffer[count++] = fillChars[this.currFillChar];
				this.currRecPos++;
				if (this.currRecPos >= this.lrecl) {
					this.currRecPos = 0;
					this.currFillChar = (this.currFillChar + 1) % 10;
					this.remainingRecords--;
				}
			}
			this.remainingBytes -= count;
			if (this.remainingBytes <= 0) { this.state = IBulkSource.STATE_SOURCE_ENDED; }
			return count;
		}

		@Override
		public int getAvailableCount() { return Math.min(2048, this.remainingBytes); }

		@Override
		public int getRemainingCount() { return this.remainingBytes; }

		@Override
		public int getState() { return this.state; }
	}
	
	private static class BulkSinkText implements IBulkSink {
		
		private int linesToAccept = 0;
		
		private int state = IBulkSink.STATE_OK;
		
		private StringBuilder sb = new StringBuilder();
		
		public BulkSinkText(int linesToMediaFull) {
			this.linesToAccept = linesToMediaFull;
			if (linesToMediaFull <= 0) { this.state = IBulkSink.STATE_TARGET_MEDIA_FULL; }
		}

		@Override
		public void close() { this.state = IBulkSink.STATE_TARGET_CLOSED; }

		@Override
		public int getState() { return this.state; }

		@Override
		public void putBlock(byte[] buffer, int length) {
			int pos = 0;
			while (pos < length) {
				if (buffer[pos] == (byte)0x0A) {
					System.out.println(this.sb.toString());
					this.sb.setLength(0);
					this.linesToAccept--;
					if (this.linesToAccept <= 0) { 
						this.state = IBulkSink.STATE_TARGET_MEDIA_FULL;
						System.out.println(".. STATE_TARGET_MEDIA_FULL reached");
					}
				} else if (buffer[pos] != (byte)0x0D) {
					this.sb.append((char)buffer[pos]);
				}
				pos++;
			}
		}
	}
	
	private static class BulkSinkBin implements IBulkSink {
		
		private int recsToAccept = 0;
		
		private final int lrecl;
		private int recPos = 0;
		
		private int state = IBulkSink.STATE_OK;
		
		private StringBuilder sb = new StringBuilder();
		
		public BulkSinkBin(int controlWord) {
			this.lrecl = Math.max(1, controlWord & 0xFF);
			this.recsToAccept = controlWord >> 8;
			if (this.recsToAccept <= 0) { this.state = IBulkSink.STATE_TARGET_MEDIA_FULL; }
		}

		@Override
		public void close() { this.state = IBulkSink.STATE_TARGET_CLOSED; }

		@Override
		public int getState() { return this.state; }

		@Override
		public void putBlock(byte[] buffer, int length) {
			for (int i = 0; i < length; i++) {
				this.sb.append((char)buffer[i]);
				this.recPos++;
				if (this.recPos >= this.lrecl) {
					System.out.println(this.sb.toString());
					this.sb.setLength(0);
					this.recPos = 0;
					this.recsToAccept--;
					if (this.recsToAccept <= 0) { this.state = IBulkSink.STATE_TARGET_MEDIA_FULL; }
				} 
			}
		}
	}
}

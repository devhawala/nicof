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
 * Representation of a protocol error at Level-One, e.g. accessing an not existing bulk stream
 * or the like.
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public class LevelOneProtErrResult implements ILevelOneResult {
	
	private final int errCode;
	
	/**
	 * Constructor.
	 * 
	 * @param errCode the error code to be returned.
	 */
	public LevelOneProtErrResult(int errCode) {
		this.errCode = errCode;
	}
	
	/**
	 * Get the error code to be returned.
	 * @return the error code to be returned.
	 */
	public int getErrCode() { return this.errCode; }
}

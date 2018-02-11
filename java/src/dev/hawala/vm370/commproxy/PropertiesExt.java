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

import java.util.Properties;

/**
 * Extension of the standard Java Properties class, allowing to get String and Integer
 * values with default values. 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012
 *
 */
public class PropertiesExt extends Properties {
	private static final long serialVersionUID = -2593299943684111928L;
	
	public String getString(String name, String defValue) {
		if (!this.containsKey(name)) { return defValue; }
		String val = this.getProperty(name);
		if (val == null || val.length() == 0) { return null; }
		return val;
	}
	
	public String getString(String name) {
		return this.getString(name, "");
	}
	
	public int getInt(String name, int defValue) {
		if (!this.containsKey(name)) { return defValue; }
		try {
			return Integer.parseInt(this.getProperty(name));
		} catch (NumberFormatException exc) {
			return defValue;
		}
	}
	
	public int getInt(String name) {
		return this.getInt(name, 0);
	}
	
	public boolean getBoolean(String name, boolean defValue) {
		if (!this.containsKey(name)) { return defValue; }
		String val = this.getProperty(name);
		if (val == null || val.length() == 0) { return false; }
		val = val.toLowerCase();
		return val.equals("true") || val.equals("yes") || val.equals("y");
	}
	
	public boolean getBoolean(String name) {
		return this.getBoolean(name, false);
	}
}

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

import java.io.File;
import java.io.FileFilter;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.regex.Pattern;

import dev.hawala.vm370.Log;
import dev.hawala.vm370.ebcdic.EbcdicHandler;

/**
 * Simple file service at Level-One used by the NHFS CMS command to access files and
 * directories on the OS where the outside NICOF proxy runs.
 * <p>
 * For security reasons, only the items below a base path defined in the configuration
 * can be accessed at all (if the service works well). Under this base path, each VM-user
 * invoking the file service has an own subdirectory (named like the VM-user) to which
 * the operations from this user are restricted.
 * <p>
 * Only files and directories compatible with the CMS naming conventions are visible, this
 * means that local files must be named like <tt><i>filename</i>.<i>filetype</i></tt> to be
 * accessible, with <i>filename</i> and <i>filetype</i> consisting of up to 8 characters (letters,
 * digits or <tt>_</tt> <tt>-</tt> <tt>+</tt> characters. Directory names must consist of
 * a single 8-character token (with the mentioned allowed characters).
 * <p>
 * In general, tokens coming from the VM/370-host are lowercased, so only files following
 * this convention can be accessed if the service is run on a platform with case sensitive
 * file naming. However, when listing the content of a directory, all files matching the 
 * 8-characters convention will be listed.
 * <p>
 * File identifications (for files to be read or written) are transmitted to the service
 * as 2 tokens <i>filename</i> and <i>filetype</i> (separated by a blank), this CMS-like
 * file id matches the file <tt><i>filename</i>.<i>filetype</i></tt> in the specified
 * sub-directory in the user's base path.
 * <p>
 * <b>Remark</b>: creating (hard- or soft-) links following the 8-character naming rules may allow
 * to access directories and files outside the user's base path, so using this functionality
 * of the underlying OS where the outside NICOF proxy (including this file service) runs
 * is at own risks!   
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012,2014
 *
 */
public class LevelOneFileService implements ILevelOneHandler {
	
	private static Log logger = Log.getLogger();
	
	// the accessible base directory of the VM-user to which this file service is bound. 
	private File userDir = null; // if null => service is misconfigured and not usable
	
	// the error-returncodes of the file service
	private static final int ERR_NOT_USABLE = 4050;          // service is misconfigured
	private static final int ERR_INVALID_COMMAND = 4051;
	private static final int ERR_INV_NAME_TOKEN = 4052;      // name token not a..z A..Z 0..9 - _ +
	private static final int ERR_MISSING_FNFT_TOKENS = 4060; // missing filename or filetype resp. direcrory name token
	private static final int ERR_DIRPATH_NOT_PRESENT = 4061; // specified path in user's dir does not exist
	private static final int ERR_FILE_NOT_FOUND = 4062;
	private static final int ERR_FILE_READ_ERROR = 4063;
	private static final int ERR_FILE_EXISTS = 4070;
	private static final int ERR_FILE_NOT_CREATED = 4071;
	private static final int ERR_DIR_ALREADY_EXISTS = 4072;
	private static final int ERR_DIR_NOT_CREATED = 4073;
	
	// the pattern to verify the validity of a filename, filetype or directory token
	private final Pattern p = Pattern.compile("[a-zA-Z0-9$_+-]+");
	
	/*
	 * Config-Parameter:
	 *   <name>.basepath => path where to allocate the client-vm specific directories (default: userbase)
	 * 
	 * Tokens in Buffer: 
	 * -> Blank-separated 1-8 character tokens [a-z0-9]
	 * -> will be lowercased when coming from VM/370
	 * 
	 * Command codes:
	 * 1 => List files
	 *   => Buffer: tokens as directory path to the user's directory to list
	 *   ==> result: bulk text source, with one line per file/directory:
	 *       - directory: D <name>
	 *       - file     : F <filename> <filetype> <bytecount> <date> <time>
	 *   
	 * 2 => read file
	 *   => Buffer: 1. token: filename, 2. token: extension (filetype), others: directory path to the file
	 *      
	 * 3 => write file
	 *   => controlData: 1: overwrite if exists, others: don't overwrite
	 *   => Buffer: 1. token: filename, 2. token: extension (filetype), others: directory path to the file
	 *   
	 * 4 => create dir
	 *   => Buffer: 1. token: new directory name, others: directory path where the new directory is to be created
	 *   
	 * Errorcodes: see ERR_* constants
	 */
	
	@Override
	public void deinitialize() { }

	@Override
	public void initialize(String name, EbcdicHandler clientVm, PropertiesExt configuration) {
		String basepath = configuration.getString(name + ".basepath", "userbase");
		File basedir = new File(basepath);
		File userDir = new File(basedir, clientVm.toString().trim().toLowerCase());
		if (userDir.exists() && !userDir.isDirectory()) {
			return; // service misconfigured
		}
		if (!userDir.exists()) {
			userDir.mkdirs();
			if (!userDir.exists()) {
				return;  // service misconfigured
			}
		}
		
		this.userDir = userDir;
	}

	@Override
	public ILevelOneResult processRequest(
			short cmd,
			int controlData,
			byte[] requestData,
			int requestDataLength,
			byte[] responseBuffer) {
		if (this.userDir == null) { return new LevelOneProtErrResult(ERR_NOT_USABLE); }
		
		String parameters = new String(requestData, 0, requestDataLength).toLowerCase();
		String[] paramToks = parameters.split(" ");
		ArrayList<String> tokens = new ArrayList<String>();  
		for (String s : paramToks) {
			if (s.length() == 0) { continue; }
			if (!p.matcher(s).matches()) {
				return new LevelOneProtErrResult(ERR_INV_NAME_TOKEN);
			}
			if (s.length() > 8) {
				tokens.add(s.substring(0, 8));
			} else {
				tokens.add(s);
			}
		}
		
		if (cmd == 1) { return this.createDirectoryLister(tokens); }
		if (cmd == 2) { return this.createFileReader(tokens); }
		if (cmd == 3) { return this.createFileWriter(tokens, controlData); }
		if (cmd == 4) { return this.createDirectory(tokens); }
		
		return new LevelOneProtErrResult(ERR_INVALID_COMMAND);
	}
	
	/*
	 * directory listing
	 */
	
	// try to access the specified sub-directory and return a Level-One bulk-stream sending
	// the directory content if the directory exists.
	private ILevelOneResult createDirectoryLister(ArrayList<String> tokens) {
		File dirToList = this.userDir;
		if (tokens.size() > 0) {
			for (String tok : tokens) {
				dirToList = new File(dirToList, tok);
			}
		}
		if (!dirToList.exists() || !dirToList.isDirectory()) {
			return new LevelOneProtErrResult(ERR_DIRPATH_NOT_PRESENT);
		}
		
		return new DirectoryListSource(dirToList);
	}
	
	// Level-One Bulk-stream writing the directory content (files and directories
	// complying to the CMS naming rules) as ASCII data.
	static private class DirectoryListSource implements IBulkSource {
		
		private final File dirToList;
		
		private int state = IBulkSource.STATE_OK;
		
		private byte[] content = null;
		private int currOffset = 0;
		private int remainingBytes = 0;

		public DirectoryListSource(File dirToList) {
			// just save the name of the directory to list
			this.dirToList = dirToList;
		}
		
		@Override
		public void close() { 
			this.state = IBulkSource.STATE_SOURCE_CLOSED;
		}

		@Override
		public int getNextBlock(byte[] buffer, boolean availableOnly) {
			// check if there is nothing to transmit
			if (this.state != IBulkSource.STATE_OK) { return 0; }
			
			// create the content on first get block request
			if (this.content == null) {
				StringBuffer sb = new StringBuffer();
				File[] theFiles = this.dirToList.listFiles(new FileFilter() {
					public boolean accept(File f) {
						String[] parts = f.getName().split(Pattern.quote("."));
						if (f.isDirectory()) {
							return (parts.length == 1 && parts[0].length() > 0 && parts[0].length() <= 8);
						}
						return (parts.length == 2
								&& parts[0].length() > 0 && parts[0].length() <= 8
								&& parts[1].length() > 0 && parts[1].length() <= 8);
					}
				});
				
				SimpleDateFormat sdfmt = new SimpleDateFormat();
				sdfmt.applyPattern("yyyy-MM-dd hh:mm:ss");

				for (File f : theFiles) {
					if (f.isDirectory()) {
						sb.append("D ").append(f.getName());
					} else if (f.isFile()) {
						String[] parts = f.getName().split(Pattern.quote("."));
						Date ts = new Date(f.lastModified());
						String line = String.format(
								"F %-8s %-8s %8d %s", 
								parts[0], parts[1], f.length(), sdfmt.format(ts));
						sb.append(line);
					}
					sb.append("\r\n");
				}
				
				this.content = sb.toString().getBytes(); 
				this.remainingBytes = this.content.length;
			}
			
			// pass back the next chunk of bytes
			int count = Math.min(2048, this.remainingBytes);
			if (count == 0) { 
				this.state = IBulkSource.STATE_SOURCE_ENDED; // why isn't this one already set?
				return 0; 
			}
			System.arraycopy(this.content, this.currOffset, buffer, 0, count);
			this.currOffset += count;
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

	/*
	** read a file
	*/
	
	// create a Level-One bulk-stream to the file specified by the tokens in the request
	// if it exists in the user's base path.
	private ILevelOneResult createFileReader(ArrayList<String> tokens) {
		if (tokens.size() < 2) {
			return new LevelOneProtErrResult(ERR_MISSING_FNFT_TOKENS);
		}
		
		String filename = tokens.get(0) + "." + tokens.get(1);
		File dir = this.userDir;
		for (int i = 2; i < tokens.size(); i++) {
			dir = new File(dir, tokens.get(i));
		}
		if (!dir.exists() || !dir.isDirectory()) {
			return new LevelOneProtErrResult(ERR_DIRPATH_NOT_PRESENT);
		} 
		
		File f = new File(dir, filename);
		if (!f.exists() || !f.isFile() || !f.canRead()) {
			return new LevelOneProtErrResult(ERR_FILE_NOT_FOUND);
		}
		
		FileInputStream fis = null;
		try {
			fis = new FileInputStream(f);
		} catch (Exception e) {
			logger.error("** Error creating FileInputStream on existing file '", f.getPath(), "', Exception: ", e);
			return new LevelOneProtErrResult(ERR_FILE_READ_ERROR);
		}
		
		return new LevelOneFileContentSource(fis, (int)(f.length() & 0xFFFFFFFF));
	}
	
	/*
	** write a file
	*/
	
	// Create a Level-One writing bulk stream to the file specified by the request
	// tokens. If the file already exists and the replace flag is not set in the 
	// controldata or if the file cannot be created for others reasons, an error
	// is returned instead of the stream.
	private ILevelOneResult createFileWriter(
				ArrayList<String> tokens, 
				int controlData) {
		if (tokens.size() < 2) {
			return new LevelOneProtErrResult(ERR_MISSING_FNFT_TOKENS);
		}
		
		boolean overwriteIfExists = (controlData == 1);
		
		String filename = tokens.get(0) + "." + tokens.get(1);
		File dir = this.userDir;
		for (int i = 2; i < tokens.size(); i++) {
			dir = new File(dir, tokens.get(i));
		}
		if (!dir.exists() || !dir.isDirectory()) {
			return new LevelOneProtErrResult(ERR_DIRPATH_NOT_PRESENT);
		} 
		
		File f = new File(dir, filename);
		if (f.exists() && !overwriteIfExists) {
			return new LevelOneProtErrResult(ERR_FILE_EXISTS);
		}
		
		FileOutputStream fos = null;
		try {
			fos = new FileOutputStream(f);
		} catch (Exception e) {
			logger.error("** Error creating FileOutputStream to file '", f.getPath(), "', Exception: ", e);
			return new LevelOneProtErrResult(ERR_FILE_NOT_CREATED);
		}
		
		return new LevelOneFileContentSink(f.getPath(), fos);
	}
	
	/*
	** create sub-directory
	*/
	
	// Create a new subdirectory in the specified subdirectory path under the user's
	// base path.
	private ILevelOneResult createDirectory(ArrayList<String> tokens) {
		if (tokens.size() < 1) {
			return new LevelOneProtErrResult(ERR_MISSING_FNFT_TOKENS);
		}
		
		String dirname = tokens.get(0);
		File parentDir = this.userDir;
		for (int i = 1; i < tokens.size(); i++) {
			parentDir = new File(parentDir, tokens.get(i));
		}
		if (!parentDir.exists() || !parentDir.isDirectory()) {
			return new LevelOneProtErrResult(ERR_DIRPATH_NOT_PRESENT);
		}
		
		File newDir = new File(parentDir, dirname);
		if (newDir.exists()) {
			return new LevelOneProtErrResult(ERR_DIR_ALREADY_EXISTS);
		}
		
		newDir.mkdir();
		if (!newDir.isDirectory()) {
			return new LevelOneProtErrResult(ERR_DIR_NOT_CREATED);			
		}
		
		return new LevelOneProtErrResult(0);	
	}
}

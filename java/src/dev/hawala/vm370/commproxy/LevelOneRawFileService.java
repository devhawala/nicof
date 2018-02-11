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
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2014
** Released to the public domain.
*/

package dev.hawala.vm370.commproxy;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FilenameFilter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.regex.Pattern;

import dev.hawala.vm370.Log;
import dev.hawala.vm370.ebcdic.EbcdicHandler;

/**
 * Extended file service at Level-One used by the RNHFS CMS command to access files and
 * directories on the OS where the outside NICOF proxy runs.
 * 
 * This NICOF service is stateful in the sense that it has a current directory that
 * can be changed, queried and listed by the client. All file transfer operations
 * specify a file relative to the current directory. 
 * 
 * In contrast to the NHFS LevelOneFileService, this service allows access to ANY file
 * in ANY directory on the host. 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2014
 *
 */

public class LevelOneRawFileService implements ILevelOneHandler {

	// the error-returncodes of the file service
	private static final int ERR_NOT_USABLE = 5050;          // service is misconfigured
	private static final int ERR_INVALID_COMMAND = 5051;
	private static final int ERR_NO_FILENAME = 5052;
	private static final int ERR_CWD_FAILED = 5060;
	private static final int ERR_FILENAME_NOT_FOUND = 5070;
	private static final int ERR_FILENAME_IS_DIR = 5071;
	private static final int ERR_FILE_NOT_READABLE = 5072;
	private static final int ERR_FILE_NOT_WRITABLE = 5073;
	private static final int ERR_DIR_IS_READONLY = 5074;
	private static final int ERR_FILE_ACCESS_ERROR = 5075;
	private static final int ERR_FILE_EXISTS = 5076;

	// access class to the file system with navigation, file enumeration and access.  
	static class Path {
		
		// is the underlying filesystem case-sensitive?
		private final Boolean ignoreCase = (File.separatorChar == '\\');
		
		// this is the current directory
		private File wd;
		
		public Path() {
			this.wd = new File(".");
			
			String currDir = this.getWD();
			if (currDir != null) { 
				this.cd(currDir);
			}
		}
		
		public String getWD() {
			try {
				return this.wd.getCanonicalPath();
			} catch (IOException e) {
				System.out.printf("** ERROR: %s\n", e.getMessage()); 
				return null;
			}
		}
		
		public Boolean cd(String d) {
			if (d == null || d.length() < 1) { return true; }
			
			char c0 = d.charAt(0);
			char c1 = (d.length() > 1) ? d.charAt(1) : (char)0;
			
			File newWd;
			if (c0 == '/') {
				// UNIX-oid absolute path 
				newWd = new File(d);
			} else if (File.separatorChar == '\\' && c0 == '\\') {
				// UNC filename
				newWd = new File(d);
			} else if (c1 == ':' && ((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z'))) {
				// MS-DOS/Windows absolute path (starting with a letter)
				newWd = new File(d);
			} else {
				// anything else should be a relative path
				newWd = new File(this.wd, d);
			}
			if (!newWd.exists() || !newWd.isDirectory()) { return false; }
			this.wd = newWd;
			return true;
		}
		
		public String getFilelist(String pat) {
			if (pat != null && this.ignoreCase) { pat = pat.toLowerCase(); }
			
			StringBuffer sb = new StringBuffer();
			final Pattern p = (pat != null && pat.length()> 0)
					? Pattern.compile(
							pat.replace("\\", "/")
							   .replace("?", ".?")
							   .replace("*", ".*?")
							   .replace("[", "\\[")
							   .replace("]", "\\]")
							   .replace("+", "\\+"))
					: null;
			File[] theFiles = (p == null) 
				? this.wd.listFiles()
				: this.wd.listFiles(new FilenameFilter() {
					@Override
					public boolean accept(File f, String name) {
						if (ignoreCase) { name = name.toLowerCase(); }
						return p.matcher(name).matches();
					}
				});
			if (theFiles == null) { return sb.toString(); }
				
			SimpleDateFormat sdfmt = new SimpleDateFormat();
			sdfmt.applyPattern("yyyy-MM-dd hh:mm:ss");

			for (File f : theFiles) {
				Date ts = new Date(f.lastModified());
				String tsString = sdfmt.format(ts);
				if (f.isDirectory()) {
					String line = String.format(
							"%s <subdir> %s", 
							tsString, f.getName());
					sb.append(line).append("\r\n");
				} else if (f.isFile()) {
					String line = String.format(
							"%s %8d %s", 
							tsString, f.length(), f.getName());
					sb.append(line).append("\r\n");
				}
			}
			
		return sb.toString();
		}
		
		public Boolean dirIsWritable() {
			return this.wd.canWrite();
		}
		
		public Element checkElement(String fn) {
			return new Element(this.wd, fn);
		}
		
		static class Element {
			private final File f;
			
			public Element(File parent, String fn) {
				this.f = new File(parent, fn); 
			}
			
			public Boolean exists() {
				return this.f.exists();
			}
			
			public Boolean isDir() {
				return this.f.isDirectory();
			}
			
			public Boolean isReadable() {
				return this.f.canRead();
			}
			
			public Boolean isWritable() {
				return this.f.canWrite();
			}
			
			public long fileLength() {
				if (!this.f.exists() || !this.f.canRead()) { return -1; }
				return this.f.length();
			}
			
			public FileInputStream readFile() {
				try {
					return new FileInputStream(this.f);
				} catch (Exception e) {
					return null;
				}
			}
			
			public FileOutputStream createFile() {
				try {
					return new FileOutputStream(this.f);
				} catch (Exception e) {
					return null;
				}	
			}
		}
	}
	
	private static Log logger = Log.getLogger();
	
	private final Path path = new Path();  
	
	@Override
	public void deinitialize() { }

	@Override
	public void initialize(String name, EbcdicHandler clientVm, PropertiesExt configuration) {
		logger.info("new raw file service for: ", clientVm);
		if (this.path == null) { 
			logger.info("**** ERROR: no current directory !!!!");
		} else {
			logger.info("... current WD: ", this.path.getWD());
		}
	}

	@Override
	public ILevelOneResult processRequest(
			short cmd,
			int controlData,
			byte[] requestData,
			int requestDataLength,
			byte[] responseBuffer) {
		
		// sanity check
		if (this.path == null) { return new LevelOneProtErrResult(ERR_NOT_USABLE); }
		
		logger.debug("processRequest(cmd = ", cmd, ")");
		
		// command PWD: return current directory in 'responseBuffer'
		//  -> controlData is the max. length the client can handle
		if (cmd == 1) {
			String cwd = this.path.getWD();
			byte[] cwdBytes = cwd.getBytes();
			System.arraycopy(cwdBytes, 0, responseBuffer, 0, Math.min(controlData, cwd.length()));
			return new LevelOneBufferResult(0, 0, cwd.length());
		}
		
		// command CWD: change working directory
		if (cmd == 2) {
			String newDirName = new String(requestData, 0, requestDataLength);
			logger.debug("Changing directory to: ", newDirName);
			if (this.path.cd(newDirName)) {
				return new LevelOneProtErrResult(0); // rc = 0
			} else {
				logger.debug("... change directory FAILED");
				return new LevelOneProtErrResult(ERR_CWD_FAILED);
			}
		}
		
		// command LIST: list files in current directory
		if (cmd == 3) {
			String pat = null;
			if (requestDataLength > 0) {
				pat = new String(requestData, 0, requestDataLength);
			}
			String res = this.path.getFilelist(pat);
			ByteArrayInputStream bis = new ByteArrayInputStream(res.getBytes());
			return new LevelOneFileContentSource(bis, res.length());
		}
		
		// command READ: read a file
		if (cmd == 4) {
			if (requestDataLength <= 0) { return new LevelOneProtErrResult(ERR_NO_FILENAME); }
			String fn = new String(requestData, 0, requestDataLength);
			Path.Element e = this.path.checkElement(fn);
			if (!e.exists()) { return new LevelOneProtErrResult(ERR_FILENAME_NOT_FOUND); }
			if (e.isDir()) { return new LevelOneProtErrResult(ERR_FILENAME_IS_DIR); }
			if (!e.isReadable()) { return new LevelOneProtErrResult(ERR_FILE_NOT_READABLE); }
			int fileLen = (int)Math.min((long)Integer.MAX_VALUE, e.fileLength());
			if (fileLen < 0) { return new LevelOneProtErrResult(ERR_FILE_ACCESS_ERROR); }
			InputStream is = e.readFile();
			if (is == null) { return new LevelOneProtErrResult(ERR_FILE_ACCESS_ERROR); }
			return new LevelOneFileContentSource(is, fileLen);
		}
		
		// command WRITE: create or overwrite a file
		if (cmd == 5) {
			if (requestDataLength <= 0) { return new LevelOneProtErrResult(ERR_NO_FILENAME); }
			if (!this.path.dirIsWritable()) { return new LevelOneProtErrResult(ERR_DIR_IS_READONLY); }
			boolean overwrite = (controlData != 0);
			String fn = new String(requestData, 0, requestDataLength);
			Path.Element e = this.path.checkElement(fn);
			if (e.isDir()) { return new LevelOneProtErrResult(ERR_FILENAME_IS_DIR); }
			if (e.exists() && !overwrite) { return new LevelOneProtErrResult(ERR_FILE_EXISTS); }
			if (e.exists() && !e.isWritable()) { return new LevelOneProtErrResult(ERR_FILE_NOT_WRITABLE); }
			OutputStream os = e.createFile();
			if (os == null) { return new LevelOneProtErrResult(ERR_FILE_ACCESS_ERROR); }
			return new LevelOneFileContentSink(fn, os);
		}
		
		// unknown command...
		return new LevelOneProtErrResult(ERR_INVALID_COMMAND);
	}	
}

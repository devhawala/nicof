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
** Written by Dr. Hans-Walter Latz, Berlin (Germany), 2012,2014
** Released to the public domain.
*/

package dev.hawala.vm370.commproxy;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;

import dev.hawala.vm370.Log;
import static dev.hawala.vm370.ebcdic.PlainHex.*;
import static dev.hawala.vm370.ebcdic.Ebcdic.*;
import dev.hawala.vm370.ebcdic.EbcdicHandler;

/**
 * Host connector to a VM/370 inside proxy using a dialed 3270 terminal connection.
 * This connector handles both the low level details (telnet 3270 protocol) of the
 * connection with the host and the high level handshaking (through WCC and AID bytes)
 * with the inside proxy. 
 * 
 * @author Dr. Hans-Walter Latz, Berlin (Germany), 2012,2014
 *
 */
public class Dialed3270HostConnector implements IHostConnector {
	
	// the length of VM/user names under VM/370
	private final static int REQ_USER_LEN = 8;
	
	// our logger
	private Log logger = Log.getLogger();

	// construction values
	private final String hostName;
	private final int hostPort;
	private final String dialVm;
	private final String cfgLuName;
	
	// the streams to communicate with the host
	private Socket socket = null;
	protected InputStream isFromHost = null;
	protected OutputStream osToHost = null;
	protected boolean useBinaryTransfer = true;
	
	// state information:
	// - is true from transmission-start until receive of EW(0x04)
	// - data transmission to host is delayed until 'sending' gets false again  
	private boolean sending = true; // delay any transmissions until protocol is synchronized with host
	
	// are we currently connected and dialed?
	// (the tcp/ip connection is held after being established, even if the DIAL failed)
	private boolean isDialedToProxy = false;
	
	/**
	 * Construct the connector and initialize the connection parameters
	 * from the properties, but don't start connecting.
	 *  
	 * @param cfg the properties to initialize with.
	 */
	public Dialed3270HostConnector(PropertiesExt cfg) {
		this.hostName = cfg.getString("host");
		this.hostPort = cfg.getInt("port");
		this.dialVm = cfg.getString("vm").toUpperCase();
		this.cfgLuName = this.dialVm;
		this.useBinaryTransfer = cfg.getBoolean("usebinarytransfer", true);
		
		this.initScreenPos();
		String cpreadPositions = cfg.getString("cpreadpositions", null);
		if (cpreadPositions != null && cpreadPositions.length() > 0) {
			this.addCPREADPositions(cpreadPositions);
		}
	}
	
	/*
	 * (non-Javadoc)
	 * @see dev.hawala.vm370.commproxy.IHostProxy#isConnected()
	 */
	public boolean isConnected() { return this.isDialedToProxy; }
	
	/**
	 * Connect with the host and DIAL to the inside proxy VM. 
	 * 
	 * @throws CommProxyStateException connecting or DIALing failed.
	 */
	public void connect() throws CommProxyStateException {
		this.connectToHost();
		this.dialToProxy();
	}
	
	/**
	 * Connect to the host via telnet and handle 3270 negociations to enter the
	 * 3270 connection mode.
	 * 
	 * @throws CommProxyStateException connecting or 3270 negociation failed. 
	 */
	private void connectToHost() throws CommProxyStateException {	
		if (this.socket != null) { return; }
		
		// connect to Hercules
		Socket hostSideSocket;
		try {
			// open a new connection to the VM/370 host
			hostSideSocket = new Socket(hostName, hostPort);
			hostSideSocket.setTcpNoDelay(true);
		} catch (IOException exc) {
			this.shutdown(true, "Unable to setup Proxy connection to VM/370-Host '" + hostName + "', Port " + hostPort);
			return; // keep the compiler happy
		}
		
		this.socket = hostSideSocket;
		
		try {
			this.isFromHost = this.socket.getInputStream();
			this.osToHost = this.socket.getOutputStream();
		} catch (Exception e) {
			this.shutdown(true, "Unable to open streams from socket to VM/370 host");
			return; // keep the compiler happy
		}
		
		// enter binary 3270 transmission mode
		try {
			this.negotiateHost3270Mode();
		} catch (Exception e) {
			this.shutdown(true, "Unable to negociate 3270 mode");
			return; // keep the compiler happy
		}
		
		this.logger.debug("connectToHost(): skipping initial start screens");
		
		// bring the console in the state to enter a command
		try {
			this.checkStartScreens();
		} catch (IOException e) {
			this.shutdown(true, true, e.getMessage());
			return; // keep the compiler happy
		}
	}
	
	// possible states of the CP console we are connected to
	private enum State { 
		none,     // initial (no confirmed state)
		running,  // CP console state RUNNING
		cpread,   // CP console state CP READ (or a CP READ simulated by the internal proxy!)
		proxy     // DIALed to the proxy
	};
	
	/**
	 * inner class implementing the asynchronous thread to clear the 3270 screen at most
	 * twice, positively ending as soon as the receiving thread received the CP READ state
	 * or negatively ending (closing the connection) when CP READ is not reached after the
	 * 2 CLEAR AIDs.
	 * 
	 * The thread first waits 1 second, then sends a CLEAR AID in 2 second distance, shutting
	 * down the thread as soon as CP READ is reached (positive end) and finally closing the
	 * connection after the 5 seconds (negative end).
	 */
	static class HostScreenClearer implements Runnable {
		
		private Log logger = Log.getLogger();
		
		private final Socket sock;
		private final OutputStream os;
		
		private int clearCount = 0; 
		
		private volatile State state = State.none;
		
		public HostScreenClearer(Socket socket, OutputStream stream) {
			this.sock = socket;
			this.os = stream;
		}
		
		public void setState(State newState) { this.state = newState; }
		
		public int getClearCount() { return this.clearCount; }

		@Override
		public void run() {
			int ticks = 0;
			try {
				
				this.logger.debug("HostScreenClearer: waiting for state <> none [1]");
				while(ticks < 10 && state == State.none) {
					Thread.yield();
					Thread.sleep(100);
					ticks++;
				}
				if (state != State.none) {
					this.logger.debug("HostScreenClearer: state is now: ", this.state);
					return;
				}

				os.write(CMDLINE_CLEAR);
				os.write(TN_EOR);
				os.flush();
				this.logger.debug("HostScreenClearer: sent CLEAR[1]");
				this.clearCount++;
				
				this.logger.debug("HostScreenClearer: waiting for state <> none [2]");
				ticks = 0;
				while(ticks < 10 && state == State.none) {
					Thread.yield();
					Thread.sleep(200);
					ticks++;
				}
				if (state != State.none) {
					this.logger.debug("HostScreenClearer: state is now: ", this.state);
					return;
				}
				
				os.write(CMDLINE_CLEAR);
				os.write(TN_EOR);
				os.flush();
				this.logger.debug("HostScreenClearer: sent CLEAR[2]");
				this.clearCount++;
				
				this.logger.debug("HostScreenClearer: waiting for state <> none [3]");
				ticks = 0;
				while(ticks < 10 && state == State.none) {
					Thread.yield();
					Thread.sleep(200);
					ticks++;
				}
				if (state == State.none) {
					this.logger.debug("HostScreenClearer: interrupting parent thread");
					// this.parentThread.interrupt();
					this.sock.close();
				}
				
			} catch(Exception exc) {
				if (state == State.none) {
					// this.parentThread.interrupt();
					try { this.sock.close(); } catch(Exception e) {}
				}
			}
		}
	}
	
	/**
	 * Asynchronously clear the 3270 screen using a HostScreenClearer instance
	 * and check the resulting screen to switch into CP READ state.
	 * If this does not happen after clearing the screen twice, the HostScreenClearer
	 * closes the connection to the host and so aborts this Connect/DIAL attempt.  
	 * 
	 * @throws IOException
	 * @throws CommProxyStateException
	 */
	private void checkStartScreens() throws IOException, CommProxyStateException {
		this.logger.debug("\n**\n** start checkStartScreens()\n**");
		HostScreenClearer cl = new HostScreenClearer(this.socket, this.osToHost);
		Thread thr = new Thread(cl);
		thr.start();
		
		try {
			this.logger.debug("...waiting for state CP READ");
			this.waitForCpReadState(); // wait for the host to write the CP READ status
			cl.setState(State.cpread); // tell the async host clearer that we are at CP READ
		}
		catch (IOException e) {
			boolean isFinal = (cl.getClearCount() == 0);
			this.shutdown(isFinal, isFinal, e.getMessage());
		}
		finally {		
			try { thr.join(); } catch (InterruptedException e) {}
		}
		this.logger.debug("\n**\n** end checkStartScreens()\n**");
	}
	
	/**
	 * DIAL to the inside-proxy VM.
	 * 
	 * @throws CommProxyStateException DIAling to the inside-proxy VM failed.
	 */
	private void dialToProxy() throws CommProxyStateException {
		if (this.isDialedToProxy) { return; }
		this.connectToHost();
		try {
			this.logger.debug("dialToProxy(): clearing screen");
			this.clearCurrentScreen();
			this.logger.debug("dialToProxy(): dialing to virtual machine: ", dialVm);
			this.dialToVm();
			this.logger.debug("dialToProxy(): connected to virtual machine: ", dialVm);
		} catch(CommProxyStateException e) {
			throw e;
		} catch (Exception e) {
			this.shutdown(true, "Unable to DIAL to VM '" + dialVm + "'");
		}
	}
	
	/**
	 * Signal the lost DIAL to the inside-proxy VM and optionally closing the connection
	 * to the host is requested.
	 * 
	 * @param close request closing the TCP/IP connection to the host.
	 * @param unrecoverable state information about the error
	 * @param msg the message text for the exception to throw
	 * 
	 * @throws CommProxyStateException signals the reason for the lost connection
	 *   to the inside-proxy VM.
	 */
	private void shutdown(boolean close, boolean unrecoverable, String msg) throws CommProxyStateException {
		if (close) {
			try { if (this.osToHost != null) { this.osToHost.close(); } } catch(Exception exc) {}
			this.osToHost = null;
			try { if (this.isFromHost != null) { this.isFromHost.close(); } } catch(Exception exc) {}
			this.isFromHost = null;
			try { if (this.socket != null) { this.socket.close(); } } catch(Exception exc) {}
			this.socket = null;
		}
		this.isDialedToProxy = false;
		throw new CommProxyStateException(unrecoverable, msg);
	}
	
	/**
	 * Signal the lost DIAL to the inside-proxy VM and optionally closing the connection
	 * to the host is requested.
	 * 
	 * @param close request closing the TCP/IP connection to the host.
	 * @param msg the message text for the exception to throw
	 * 
	 * @throws CommProxyStateException signals the reason for the lost connection
	 *   to the inside-proxy VM.
	 */
	private void shutdown(boolean close, String msg) throws CommProxyStateException {
		this.shutdown(close, false, msg);
	}
	
	/*
	** Constants for 3270 negociation 
	*/
	
	private static byte[] TN_EOR = { (byte)0xFF, (byte)0xEF };
	
	private static byte[] TN_FF = { (byte)0xFF, (byte)0xFF };
	
	private static byte[] TN_DO_TERMINAL_TYPE 
							= { (byte)0xFF, (byte)0xFD, (byte)0x18 };
	private static byte[] TN_WILL_TERMINAL_TYPE 
							= { (byte)0xFF, (byte)0xFB, (byte)0x18 };
	
	//private static byte[] TN_SE 
	//					= { (byte)0xFF, (byte)0xF0 };
	private static byte[] TN_SB_SEND_TERMINAL_TYPE 
							= { (byte)0xFF, (byte)0xFA, (byte)0x18, (byte)0x01, (byte)0xFF, (byte)0xF0 }; // incl. TN_SE

	private static byte[] TN_SB_TERMINAL_TYPE_IS_3270_4_E
							= { (byte)0xFF, (byte)0xFA, (byte)0x18, (byte)0x00,
								(byte)0x49, (byte)0x42, (byte)0x4d, (byte)0x2d, // IBM-
								(byte)0x33, (byte)0x32, (byte)0x37, (byte)0x38, // 3278 
								(byte)0x2d, (byte)0x34, (byte)0x2d, (byte)0x45, // -4-E
								(byte)0xFF, (byte)0xF0
								};

	private static byte[] TN_DO_END_OF_RECORD 
							= { (byte)0xFF, (byte)0xFD, (byte)0x19 };
	private static byte[] TN_WILL_END_OF_RECORD 
							= { (byte)0xFF, (byte)0xFB, (byte)0x19 };
	
	private static byte[] TN_DO_BINARY 
							= { (byte)0xFF, (byte)0xFD, (byte)0x00 };
	private static byte[] TN_WILL_BINARY 
							= { (byte)0xFF, (byte)0xFB, (byte)0x00 };
	
	/*
	** Constants for handshaking with the inside-proxy VM through the DIALed 3270 connection  
	*/
	
	private static byte CCW_WRITE = (byte)0x01;
	//private static byte CCW_ERASEWRITE = (byte)0x05;
	
	private static byte AID_ENTER = (byte)0x7D;
	private static byte AID_CLEAR = (byte)0x6D;
	private static byte AID_PF1   = (byte)0xF1;
	private static byte AID_PF2   = (byte)0xF2;
	private static byte AID_PF3   = (byte)0xF3;
	private static byte AID_PF4   = (byte)0xF4;
	private static byte AID_PF5   = (byte)0xF5;
	private static byte AID_PF9   = (byte)0xF9;
	
	// NICOF-handshake [ java -> vm/370 ] : java proxy is started up
	private static final byte[] HANDSHAKE_WELCOME 
							= { AID_PF2, _40, _40, (byte)0xFF, (byte)0xEF };
	
	// NICOF-handshake [ java -> vm/370 ] : java proxy is started up and wants binary transfer instead of 7/8 encoding
	private static final byte[] HANDSHAKE_WELCOME_BINARY 
							= { AID_PF9, _40, _40, (byte)0xFF, (byte)0xEF };
	
	// NICOF-handshake [ java -> vm/370 ] : java proxy has data to send
	private static final byte[] HANDSHAKE_WANT_SEND
							= { AID_PF5, _40, _40, (byte)0xFF, (byte)0xEF };
	
	// NICOF-handshake [ java -> vm/370 ] : last request from vm/370 accepted
	private static final byte[] HANDSHAKE_ACK
							= { AID_PF1, _40, _40, (byte)0xFF, (byte)0xEF };
	
	// NICOF-handshake [ java -> vm/370 ] : last request from vm/370 accepted, java proxy has data to send 
	private static final byte[] HANDSHAKE_ACK_WANT_SEND
							= { AID_PF3, _40, _40, (byte)0xFF, (byte)0xEF };
	
	// NICOF-handshake [ java -> vm/370 ] : here comes the data packet from java proxy 
	private static final byte HANDSHAKE_DATA = AID_ENTER;
	
	// NICOF-handshake [ java -> vm/370 ] : here comes the data packet from java proxy and there is another in the queue 
	private static final byte HANDSHAKE_DATA_AND_WANT_SEND = AID_PF4;
	
	/*
	** Constants for the simulated user interaction with CP to DIAL to the inside-proxy VM 
	*/
		
	// prefix when sending a CP command at the VM/370 prompt
	private static byte[] CMDLINE_PREFIX = { 
		AID_ENTER,                          // Enter
		(byte)0x40, (byte)0x40,             // CursorPosition(0,0) (CP ignores the position)
		(byte)0x11, (byte)0x40, (byte)0x40  // SBA(0,0) (CP ignores the position)
	};
	
	// clear screen command at the VM/370 prompt 
	private static byte[] CMDLINE_CLEAR = { 
		AID_CLEAR,                          // Clear
		(byte)0x40, (byte)0x40              // CursorPosition(0,0) (CP ignores the position)
	};
	
	/**
	 * Perform the complete telnet-negotiation with the host to enter the TN3270 binary
	 * transmission mode, claiming to be an IBM-3278-4-E terminal emulation.
	 * @throws IOException
	 */
	protected void negotiateHost3270Mode() throws IOException {
		byte[] buffer = new byte[256];
		boolean sentTerminal = false;
		boolean meBinary = false;
		boolean hostBinary = false;
		boolean meEOR = false;
		boolean hostEOR = false;
		String luName = this.cfgLuName;
		
		this.logger.debug("Begin of TN3270 mode negotiation with Host");
		while(!(sentTerminal && meBinary && hostBinary && meEOR && hostEOR)) {
			int rcvd = this.isFromHost.read(buffer);
			int offset = 0;
			while (offset < rcvd) {
				if (this.isPresent(TN_DO_TERMINAL_TYPE, buffer, offset)) {
					this.osToHost.write(TN_WILL_TERMINAL_TYPE);
					this.osToHost.flush();
					this.logger.debug(" Rcvd: DO TERMINAL TYPE    =>   Sent: WILL TERMINAL TYPE");
					offset += TN_DO_TERMINAL_TYPE.length;
				} else if (this.isPresent(TN_SB_SEND_TERMINAL_TYPE, buffer, offset)) {
					byte[] termSeq = TN_SB_TERMINAL_TYPE_IS_3270_4_E;
					if (luName != null && !luName.isEmpty()) {
						// insert @<luname> into the predefined terminal name sequence
						if (luName.length() > 32) { luName = luName.substring(0, 32); }
						int oldLen = TN_SB_TERMINAL_TYPE_IS_3270_4_E.length;
						byte[] oldSeq = TN_SB_TERMINAL_TYPE_IS_3270_4_E;
						int newLen = oldLen + luName.length() + 1;
						termSeq = new byte[newLen];
						int pos = 0;
						for (int i = 0; i < oldLen - 2; i++) { termSeq[pos++] = oldSeq[i]; }
						termSeq[pos++] = '@';
						for (int i = 0; i < luName.length(); i++) { termSeq[pos++] = (byte)luName.charAt(i); }
						for (int i = oldLen - 2; i < oldLen; i++) { termSeq[pos++] = oldSeq[i]; }
					}
					this.osToHost.write(termSeq);
					this.osToHost.flush();
					this.logger.debug(" Rcvd: SEND TERMINAL TYPE  =>   Sent: TERMINAL TYPE IS IBM3278-2-E");
					offset += TN_SB_SEND_TERMINAL_TYPE.length;
					sentTerminal = true;
				} else if (this.isPresent(TN_DO_END_OF_RECORD, buffer, offset)) {
					this.osToHost.write(TN_WILL_END_OF_RECORD);
					this.osToHost.flush();
					this.logger.debug(" Rcvd: DO END OF RECORD    =>   Sent: WILL END OF RECORD");
					offset += TN_DO_END_OF_RECORD.length;
					meEOR = true;
				} else if (this.isPresent(TN_WILL_END_OF_RECORD, buffer, offset)) {
					this.osToHost.write(TN_DO_END_OF_RECORD);
					this.osToHost.flush();
					this.logger.debug(" Rcvd: WILL END OF RECORD  =>   Sent: DO END OF RECORD");
					offset += TN_WILL_END_OF_RECORD.length;
					hostEOR = true;
				} else if (this.isPresent(TN_DO_BINARY, buffer, offset)) {
					this.osToHost.write(TN_WILL_BINARY);
					this.osToHost.flush();
					this.logger.debug(" Rcvd: DO BINARY           =>   Sent: WILL BINARY");
					offset += TN_DO_BINARY.length;
					meBinary = true;
				} else if (this.isPresent(TN_WILL_BINARY, buffer, offset)) {
					this.osToHost.write(TN_DO_BINARY);
					this.osToHost.flush();
					this.logger.debug(" Rcvd: WILL BINARY         =>   Sent: DO BINARY");
					offset += TN_WILL_BINARY.length;
					hostBinary = true;
				} else {
					this.logger.error("Received invalid data while negocating into TN3270 binary mode");
					throw new IOException("Received invalid data while negocating into TN3270 binary mode");
				}
			}
		}
		this.logger.debug("Done successfull TN3270 mode negotiation with Host");
	}
	
	private boolean isPresent(byte[] what, byte[] in, int at) {
		return this.isPresent(what, in, at, null);
	}
	
	private boolean isPresent(byte[] what, byte[] in, int at, String failMsg) {
		if (in.length < at || in.length < (at + what.length - 1)) { 
			if (failMsg != null) { this.logger.error(failMsg + " - to short"); }
			return false; 
		}
		for (int i = 0; i < what.length; i++) {
			if (what[i] != in[at+i]) { 
				if (failMsg != null) { this.logger.error(failMsg + " - different at src[" + i + "]"); }
				return false; 
			}
		}
		if (failMsg != null) { this.logger.debug(failMsg + " - present in response at offset " + at); }
		return true;
	}
	
	private void doSleep(int msecs) throws IOException {
		try {
			this.logger.trace("...waiting ", msecs," msecs");
			Thread.sleep(msecs);
		} catch (InterruptedException e) {
			throw new IOException("Interrupted doSleep( " + msecs + " ms )", e);
		}
	}
	
	// try to dial to the inside proxy user on the VM/370 machine 
	private void dialToVm() throws CommProxyStateException, IOException {
		// do the initial DIAL command
		this.logger.debug("... sending the DIAL command");
		EbcdicHandler cmdText = new EbcdicHandler("DIAL " + this.dialVm);
		this.osToHost.write(CMDLINE_PREFIX);
		this.osToHost.write(cmdText.getRawBytes(), 0, cmdText.getLength());
		this.osToHost.write(TN_EOR);
		this.osToHost.flush();
		
		// verify that we are dialed (failing to dial throws a CommProxyStateException) 
		while (!this.isDialedTo()) {}
		this.isDialedToProxy = true;
		
		// wait a little
		this.doSleep(100);
		
		// send WELCOME handshake (F2/F9 : external proxy starting up)
		this.sending = false;
		if (this.useBinaryTransfer) {
			this.logger.debug("... sending handshake-welcome-binary to host");
			this.osToHost.write(HANDSHAKE_WELCOME_BINARY);
		} else {
			this.logger.debug("... sending handshake-welcome to host");
			this.osToHost.write(HANDSHAKE_WELCOME);
		}
		this.osToHost.flush();
		this.doSleep(100);
	}
	
	private int dropRestOfRecord() throws IOException {
		boolean lastEor0 = false;
		boolean hadEor = false;
		int skippedBytes = 0;
		while(!hadEor) {
			byte b = this.rcvByte();
			skippedBytes++;
			if ((b == TN_EOR[1]) && lastEor0) { hadEor = true; }
			lastEor0 = (b == TN_EOR[0]);
		}
		return skippedBytes;
	}
	
	// check if the 3270 response of VM/370 indicates the we are (positively) DIALed to the proxy VM 
	private boolean isDialedTo() throws CommProxyStateException, IOException {
		boolean isDialed = false;
		
		// skip the 3270 output stream intro
		/*byte ccwByte =*/ this.rcvByte();
		/*byte wccByte =*/ this.rcvByte();
		
		// verify that SBA x,y comes next
		byte sba = this.rcvByte();
		if (sba != _11) { this.dropRestOfRecord(); return false; }
		this.rcvByte();
		this.rcvByte();
		
		// check the first 6 bytes written on the screen
		byte b0 = this.rcvByte();
		byte b1 = this.rcvByte();
		byte b2 = this.rcvByte();
		byte b3 = this.rcvByte();
		byte b4 = this.rcvByte();
		byte b5 = this.rcvByte();
		
		// if the lines starts with a DMKDIAxx error message, then the DIAL was rejected
		// and we must abort this connection attempt
		if (b0 == _D && b1 == _M && b2 == _K && b3 == _D && b4 == _I && b5 == _A) {
			this.logger.debug("...waiting for state CP READ before disconnecting");
			this.waitForCpReadState();
			this.shutdown(false, "Proxy-VM '" + this.dialVm + "' not present or not accepting DIALs");
			return false; // keep the compiler happy
		}
		
		// if the first 6 bytes are DIALED, then we are positively connected to the
		// proxy-VM, else this is some other message written to the screen and we 
		// we are not DIALED, but this may be decidable with the next screen message...
		isDialed = (b0 == _D && b1 == _I && b2 == _A && b3 == _L && b4 == _E && b5 == _D);
		this.dropRestOfRecord();
		
		return isDialed;
	}
	
	/*
	 * reading from the 3270 binary telnet stream, handling the 0xFF escaping
	 */
	
	// the read buffer for the 3270 stream
	private final byte[] rcvBuffer = new byte[MAX_PACKET_LEN * 4];
	private int rcvLen = 0;
	private int rcvPos = 0;
	
	// get the next byte from the underlying tcp/ip stream 
	private byte rcvByteRaw() throws IOException {
		if (this.rcvPos >= this.rcvLen) {
			this.rcvLen = 0;
			this.rcvPos = 0;
			while (this.rcvLen == 0) {
				this.rcvLen = this.isFromHost.read(this.rcvBuffer);
				if (this.rcvLen < 0) {
					if (this.isDialedToProxy) {
						throw new IOException("Connection closed by host");
					} else {
						throw new IOException("Connection closed by host (is LUNAME '" + this.cfgLuName + "' configured?)");
					}
				}
			}
			this.logger.logHexBuffer("rcvByte => new block from host", "<====", this.rcvBuffer, this.rcvLen);
		}
		byte currByte = this.rcvBuffer[this.rcvPos++];
		return currByte;
	}
	
	// read a telnet byte (un-escaping a 0xFF byte)
	private byte rcvByte() throws IOException {
		byte currByte = this.rcvByteRaw();
		if (currByte == (byte)0xFF) {
			// telnet escape char (we ignore negotiations at this point!)
			byte nextByte = this.rcvByteRaw();
			if (nextByte != (byte)0xFF) {
				// not an escaped 0xFF => unreceive last char
				this.rcvPos--;
			} 
		}
		return currByte;
	}
	
	// read a 16 bit binary value 
	private short rcvHalfWord() throws IOException {
		short s1 = (short)((short)rcvByte() & (short)0x00FF);
		short s2 = (short)((short)rcvByte() & (short)0x00FF);
		short value = (short)((s1 << 8) | s2);
		return value;
	}
	
	// read a 32 bit binary value 
	private int rcvFullWord() throws IOException {
		int i1 = ((int)rcvByte()) & 0x000000FF;
		int i2 = ((int)rcvByte()) & 0x000000FF;
		int i3 = ((int)rcvByte()) & 0x000000FF;
		int i4 = ((int)rcvByte()) & 0x000000FF;
		int value = ((((((i1 << 8) | i2) << 8) | i3) << 8) | i4);
		return value;
	}
	
	/*
	 * interactions with VM/370 at the CP level before logging on
	 */
	
	// representation of a screen position as the 2 parameter bytes of the SBA command
	private static class ScreenPos {
		public final byte b1;
		public final byte b2;
		
		public ScreenPos(byte b1, byte b2) {
			this.b1 = b1;
			this.b2 = b2;
		}
	}
	
	// string CP READ
	private static byte[] CPREAD = { _C, _P, _Blank, _R, _E, _A, _D };
	
	// list of known SBA paramerters used by CP before writing the console state at the lower right
	private ArrayList<ScreenPos> cpReadPositions = new ArrayList<ScreenPos>();
	
	// fill the list with 4 "usual" sscreen positions
	private void initScreenPos() {
		this.cpReadPositions.add(new ScreenPos(_5D, _6B)); // VM/370-R6 Sixpack 1.2, screen layout for 3270-2
		this.cpReadPositions.add(new ScreenPos(_F5, _5B)); // VM/370-R6 Sixpack 1.2, screen layout for 3270-4
		this.cpReadPositions.add(new ScreenPos(_E7, _6B)); // VM/ESA-P370 1. position
		this.cpReadPositions.add(new ScreenPos(_E7, _6C)); // VM/ESA-P370 2. position
	}
	
	// add additional SBA parameter bytes from the property value in the proxy properties files
	// (each non-blank char must be a hex digit, 4 digits making up 1 SBA parameter)
	// (invalid chars abort interpretation)
	private void addCPREADPositions(String v) {
		int s = 0;
		int[] n = new int[4];
		int d = 0;
		
		while (s < v.length()) {
			char c = v.charAt(s++);
			if (c == ' ') { continue; }
			if (c >= '0' && c <= '9') { n[d] = c - '0'; d++; }
			else if (c >= 'a' && c <= 'f') { n[d] = (c - 'a') + 10; d++; }
			else if (c >= 'A' && c <= 'F') { n[d] = (c - 'A') + 10; d++; }
			else {
				this.logger.error("addCPREADPositions(): invalid hex char in option, option interpretation aborted");
				return;
			}
			if (d >= 4) {
				byte b1 = (byte)((n[0] << 4) + n[1]);
				byte b2 = (byte)((n[2] << 4) + n[3]);
				this.cpReadPositions.add(new ScreenPos(b1, b2));
				if (this.logger.isInfo()) {
					System.out.printf("+++ added screen position for 'CP READ': %02X %02X\n", b1, b2);
				}
			}
		}
		
	}
	
	// wait for the string CP READ to arrive after the buffer address was set to a known
	// screen position for the console state
	// if CP READ if found but the previous SBA parameter is unknown, a warning is issued
	// with the hex codes for the 2 parameter bytes, which should be added to the "cpreadposition"
	// property for the proxy.
	private void waitForCpReadState() throws IOException {
		byte lastPosB1 = 0x40;
		byte lastPosB2 = 0x40;
		boolean lastPosIsCPREAD = false;
		
		int spos = 0;
		while(true) {
			byte b = this.rcvByte();
			if (b == CPREAD[spos]) {
				spos++;
				if (spos >= CPREAD.length) {
					if (lastPosIsCPREAD) { 
						if (this.logger.isTrace()) {
							System.out.printf(
									"### waitForCpReadState(): found CP READ at screen position %02X %02X\n",
									lastPosB1, lastPosB2);
						}
						return;
					}
					if (this.logger.isWarn()) {
						System.out.printf(
								"\n###\n" +
						        "### waitForCpReadState(): found CP READ at unexpected screen position %02X %02X\n" +
								"###  -> add this position to proxy definition\n" +
						        "###\n\n",
								lastPosB1, lastPosB2);
						spos = 0;
					}
				}
			} else {
				spos = 0;
				if (b == _11) { // SBA command (2 parameter bytes)
					lastPosB1 = this.rcvByte();
					lastPosB2 = this.rcvByte();
					lastPosIsCPREAD = false;
					for(ScreenPos pos : this.cpReadPositions) {
						if (lastPosB1 == pos.b1 && lastPosB2 == pos.b2) {
							lastPosIsCPREAD = true;
							break;
						}
					}
				}
			}
		}
	}
	
	// clear the 3270 terminal screen
	private void clearCurrentScreen() throws IOException {
		// wait a little
		this.doSleep(100);
		
		// do a terminal-side CLEAR
		this.logger.debug("... doing CLEAR");
		this.osToHost.write(CMDLINE_CLEAR);
		this.osToHost.write(TN_EOR);
		this.osToHost.flush();
		this.doSleep(100);
		
		// CP answers with:
		// 1. enter RUNNING-state
		// 2. enter CP READ state
		
		// wait for state to enter CP READ
		this.logger.debug("...waiting for state CP READ");
		this.waitForCpReadState();
		this.doSleep(100);
	}
	
	/*
	 * the NICOF outside proxy communication engine
	 */
	
	// the NICOF host proxy sends all SBA to the last possible 12-bit buffer position (encoded as 0x7F7F),
	// so receiving this position indicates a valid connection to the host proxy
	private boolean isConnected = false;
	
	// our own state
	private boolean mayRequestSend = false;     // true if host is known to be idle
	private boolean pendingRequestSend = false; // true if we are waiting for permission to send
	private boolean lastWasWantSend = false;    // true if our last handshake was "want send"
	private long lastHandshakeTS = 0;           // timestamp of our last handshake sent 
	private static long MIN_TTW_MS = 0;         // time to wait between consecutive sends to host
	
	// the handshake host -> java proxy is encoded in the WCC byte (2. byte) of a WRITE-ccw
	// (a request itself (with data) is transmitted with an ERASEWRITE-ccw having the data as
	//  "screen" content)
	private static final byte H_welcome = _00;      // host welcomes us
	private static final byte H_welcome_bin = _0D;  // host welcomes us as a binary client
	private static final byte H_will_send = _01;    // host tells us that it will now send a data packet
	private static final byte H_send_packet = _05;  // host tells us to send out next data packet (after we requested so)
	private static final byte H_ack = _04;          // host acknowledges our last data packet sent
	private static final byte H_reset = _0F;        // host wants us to reset all states as there was a handshake error
	private static final byte H_state = _0E;        // dump request state (for testing/debugging)
	
	// process the handshaking initiated by the host (i.e. interpret the WCC-byte and 
	// act accordingly)
	// this routine must be called in synchronized(this)
	private void handleHandshake(byte wccCode) throws IOException {
		// (currently) drop packet content
		int skippedBytes = this.dropRestOfRecord();
		this.logger.trace("handleHandshake(), skipped content bytes: ", skippedBytes);
		this.logger.debug("handleHandshake(): wccCode = ", wccCode);
	
		// interpret  WCC-code to determine the packet type
		if (wccCode == H_will_send) {
			this.logger.debug("handleHandshake() => WILL-SEND from host");
			this.sending = false;
			this.mayRequestSend = false;
			this.pendingRequestSend = false;
			if (this.lastWasWantSend) {
				long pauseTime = System.currentTimeMillis() - this.lastHandshakeTS;
				if (pauseTime < MIN_TTW_MS) {
					long pauseMs = MIN_TTW_MS - pauseTime; 
					this.logger.debug("Pausing ", pauseMs, "ms before sending ACK");
					try {
						Thread.sleep(pauseMs);
					} catch (Exception exc) {}
				}
			}
			this.logger.debug("handleHandshake(): ACK >>> host");
			this.osToHost.write(HANDSHAKE_ACK);
			this.osToHost.flush();
			this.lastHandshakeTS = System.currentTimeMillis();
			this.lastWasWantSend = false;
		} else if (wccCode == H_ack) {
			this.logger.debug("handleHandshake() => ACK from host");
			this.finishFirstQueued(); // the last data packet is now acknowledged (i.e. successfully transmitted to the host) 
			this.sending = false;
			this.mayRequestSend = true; // we can only assume the host is now idle
			this.pendingRequestSend = false;
		} else if (wccCode == H_send_packet) {
			this.logger.debug("handleHandshake() => SEND-PACKET from host");
			this.finishFirstQueued(); // if we were sending, SEND-PACKET is also an ACK for the last data packet
			this.sending = false; 
			this.sendFirstQueued();
		} else if (wccCode == H_welcome) {
			this.logger.debug("handleHandshake() => WELCOME from host");
			this.sending = false;
			this.mayRequestSend = false;
			this.pendingRequestSend = false;
		} else if (wccCode == H_welcome_bin) {
			this.logger.debug("handleHandshake() => WELCOME-BINARY from host");
			this.sending = false;
			this.mayRequestSend = false;
			this.pendingRequestSend = false;
		} else if (wccCode == H_reset) {
			this.logger.debug("handleHandshake() => RESET from host");
			this.sending = false;
			this.mayRequestSend = true;
			if (this.firstWaitingRequest != null) {
				this.pendingRequestSend = true;
				this.logger.debug("handleHandshake(): ACK_WANT_SEND >>> host");
				this.osToHost.write(HANDSHAKE_ACK_WANT_SEND);
			} else {
				this.pendingRequestSend = false;
				this.logger.debug("handleHandshake(): ACK >>> host");
				this.osToHost.write(HANDSHAKE_ACK);
			}
			this.osToHost.flush();
			this.lastHandshakeTS = System.currentTimeMillis();
			this.lastWasWantSend = false;
		} else if (wccCode == H_state) {
			this.DumpState();
		} else {
			// any other WCC-byte is a protocol error
			// => try to re-synchronize
			this.logger.debug("handleHandshake() => protocol error => resynchronizing");
			if (this.useBinaryTransfer) {
				this.logger.debug("handleHandshake(): WELCOME_BINARY >>> host");
				this.osToHost.write(HANDSHAKE_WELCOME_BINARY);
			} else {
				this.logger.debug("handleHandshake(): WELCOME >>> host");
				this.osToHost.write(HANDSHAKE_WELCOME);
			}			
			this.osToHost.flush();
			this.lastHandshakeTS = System.currentTimeMillis();
			this.lastWasWantSend = false;
			this.sending = false;
		}
	}
	
	/*
	 * handling of arriving transmissions from the host 
	 */
	
	// the pipeline of processed requests (with a response) waiting for their response
	// to be sent to the host  
	private RequestResponse firstWaitingRequest = null;
	private RequestResponse lastWaitingRequest = null;
	
	// the request objects not currently in use
	private RequestResponse freeRequest = null;
	
	// debugging support: data about request processing (local and on the VM/370-side)
	private ArrayList<RequestResponse> workingRequests = new ArrayList<RequestResponse>(); // requests received but not already sent back
	private HashMap<Long, Long> lastProcessedId = new HashMap<Long, Long>(); // VM-name => UserWord2
	ArrayList<String> slotsInUse = new ArrayList<String>(); // slots in the working requests

	// receive the next packet (transmitted as 3270 screen writing operation), interpret the operation 
	// sent and dispatch the packet to handshake handling (returning null to indicate no request
	// was received) or return it as request object.
	private IRequestResponse innerReceiveRecord() throws CommProxyStateException, IOException {
		// 3270 start bytes of the input stream
		byte ccwCode = (byte)(this.rcvByte() & 0x0F);
		synchronized(this) {
			byte wccCode = (byte)(this.rcvByte() & 0x0F);
	
			// verify the SBA x,y part for "proxy connection lost"
			byte sbaCmd = this.rcvByte();
			byte sba0 = this.rcvByte();
			byte sba1 = this.rcvByte();
			if (this.isConnected && sbaCmd == _11 && sba0 != _7F && sba1 != _7F) {
				// wait a little
				this.doSleep(100);
				
				// clear the screen (get back to initial VM/370 logo screen)
				this.logger.debug("innerReceiveRecord(): non-proxy-SBA ... doing CLEAR");
				this.osToHost.write(CMDLINE_CLEAR);
				this.osToHost.write(TN_EOR);
				this.osToHost.flush();
				this.doSleep(100);
				
				// signal end of DIAL-ed connection
				this.shutdown(false, "Proxy connection to " + this.dialVm + " lost (no longer DIALed)");
			} else if (!this.isConnected && sbaCmd == _11 && sba0 == _7F && sba1 == _7F) {
				this.isConnected = true;
			} else if (!this.isConnected) {
				return null;
			}
			
			// check for internal communication packet (anything else is interpreted as data packet)
			if (ccwCode == CCW_WRITE) {
				this.logger.debug("innerReceiveRecord() => handshake packet , WCC = ", wccCode);
				this.handleHandshake(wccCode);
				return null;
			}
			
			this.logger.debug("innerReceiveRecord() => data packet");
			
			// allocate the request instance
			RequestResponse request = this.freeRequest;
			if (request == null) { 
				request = (this.useBinaryTransfer) 
						? new RequestResponseBinaryEncoded()
						: new RequestResponse7to8Encoded();
			} else {
				this.freeRequest = request.getNext();
				request.reset();
			}
			
			// get the request header
			long userLong = 0;
			byte[] reqUser = request.getReqUser();
			for (int i = 0; i < REQ_USER_LEN; i++) {
				byte b = this.rcvByte();
				reqUser[i] = b;
				userLong = (userLong << 8) | b; 
			}
			int userWord1 = this.rcvFullWord();
			int userWord2 = this.rcvFullWord();
			short reqSlot = this.rcvHalfWord();
			logger.debug("packet: slot = ", reqSlot, ", uw1 = ", userWord1, ", uw2 = ", userWord2);
			request.setReqInfos(reqSlot, userWord1, userWord2);
			
			// save debugging information 
			String slotKey = "S"+reqSlot;
			if (this.slotsInUse.contains(slotKey)) {
				logger.error("packet: slot = ", reqSlot, ", uw1 = ", userWord1, ", uw2 = ", userWord2);
				logger.error("****** duplicate slot usage: ", reqSlot, "*******");
			} else {
				this.slotsInUse.add(slotKey);
			}
			this.lastProcessedId.put(userLong, (long)((long)userWord1 << 16) + reqSlot);
			
			// get the packet data
			byte[] dest = request.getReqData();
			int rcvCount = 0;
			boolean lastEor0 = false;
			boolean hadEor = false;
			while(!hadEor) {
				byte b = this.rcvByteRaw();
				if (b == TN_EOR[1] && lastEor0) {
					hadEor = true;
					continue;
				} 
				if (lastEor0) {
					// here we ignore telnet negotiations, so IAC is simple interpreted as escape
					if (rcvCount < dest.length) { dest[rcvCount++] = TN_EOR[0]; }
					lastEor0 = false;
					continue;
				}
				lastEor0 = (b == TN_EOR[0]);
				if (!lastEor0) {
					if (rcvCount < dest.length) { dest[rcvCount++] = b; }
				}
			}
			request.setReqDataLen(rcvCount);
			
			// we now have all packet data, so do the acknowledge handshake, possibly indicating
			// that we wish to send our own data
			this.sending = false;
			if (this.firstWaitingRequest != null) {
				this.logger.debug("innerReceiveRecord(): HANDSHAKE_ACK_WANT_SEND >>> host");
				this.osToHost.write(HANDSHAKE_ACK_WANT_SEND);
				this.pendingRequestSend = true;
				this.lastWasWantSend = true;
				this.mayRequestSend = false;
			} else {
				this.pendingRequestSend = false;
				this.logger.debug("innerReceiveRecord(): HANDSHAKE_ACK >>> host");
				this.osToHost.write(HANDSHAKE_ACK);
				this.lastWasWantSend = false;
				this.mayRequestSend = true;
			}
			this.osToHost.flush();
			this.lastHandshakeTS = System.currentTimeMillis();
			
			// return the data packet for processing
			this.workingRequests.add(request);
			return request;
		}
	}
	
	/**
	 * Receive the next data packet or handshaking packet, shutting down the proxy-connection
	 * on our side if there is an I/O problem.  
	 */
	public IRequestResponse receiveRecord() throws CommProxyStateException, IOException {
		try {
			return this.innerReceiveRecord();
		} catch (IOException exc) {
			this.shutdown(false, "Unable to receive/send handshake/data packet, shutting down");
		}
		return null; // keep the compiler happy, as "shutdown" never returns 
	}
	
	// dump the "last known packet information" to the logger (this is initiated by the host
	// with an handshake packet).
	private void DumpState() {
		this.logger.warn("+++++++++++++++++++++++ Proxy state at ", new Date());
		
		this.logger.warn("------ requests waiting for transmission to host:");
		RequestResponse curr = this.firstWaitingRequest;
		while(curr != null) {
			this.logger.warn("Slot ", curr.getSlot() , ", uw1 = ", curr.getReqUserWord1(), ", uw2 = ", curr.getReqUserWord2());
			curr = curr.getNext();
		}
		
		this.logger.warn("------ requests received from host in working state:");
		for (RequestResponse r : this.workingRequests) {
			this.logger.warn("Slot ", r.getSlot() , ", uw1 = ", r.getReqUserWord1(), ", uw2 = ", r.getReqUserWord2());
		}
		
		this.logger.warn("------ last request-Ids received from host:");
		for (long userLong : this.lastProcessedId.keySet()) {
			long val = this.lastProcessedId.get(userLong);
			short slot = (short)(val & 0xFFFF);
			int uw1 = (int)(val >> 16);
			this.logger.warn("UserLong ", userLong ,", slot ", slot , ", uw1 = ", uw1);
		}
		
		this.logger.warn("+++++++++++++++++++++++ end Proxy state");
	}
	
	/* (non-Javadoc)
	 * @see dev.hawala.vm370.commproxy.IHostProxy#sendResponse(dev.hawala.vm370.commproxy.IRequestResponse)
	 */
	public void sendResponse(IRequestResponse resp) throws CommProxyStateException, IOException {
		try {
			if (resp instanceof RequestResponse) {
				synchronized(this) {
					RequestResponse r = (RequestResponse)resp;
					
					if (this.lastWaitingRequest != null) {
						// this is not the first in the queue, so some handshake with the host is already
						// under way to transmit the queue head and this one will automatically be sent 
						// subsequently
						// (so just enqueue it)
						this.logger.debug("sendResponse(): enqueuing a new response as LAST");
						this.lastWaitingRequest.setNext(r);
						this.lastWaitingRequest = r;
						return;
					}
					
					// this is the new head of the queue, so the transmission must be requested
					// if the current handshake does not forbid to transmit a WANT_SENT handshake 
					this.logger.debug("sendResponse(): enqueuing a new response as FIRST");
					this.firstWaitingRequest = r;
					this.lastWaitingRequest = r;
					if (this.mayRequestSend && !this.pendingRequestSend) {
						long pauseTime = System.currentTimeMillis() - this.lastHandshakeTS;
						if (pauseTime < MIN_TTW_MS) {
							long pauseMs = MIN_TTW_MS - pauseTime; 
							this.logger.debug("Pausing ", pauseMs, " ms before sending WANT_SEND");
							try {
								Thread.sleep(pauseMs);
							} catch (Exception exc) {}
						}
						this.logger.debug("sendResponse(): WANT_SEND >>> host");
						this.osToHost.write(HANDSHAKE_WANT_SEND);
						this.osToHost.flush();
						this.lastHandshakeTS = System.currentTimeMillis();
						this.pendingRequestSend = true;
						this.lastWasWantSend = true;
					} else {
						this.logger.debug("sendResponse(): WANT_SEND not allowed for new enqueued head");
					}
				}
			} else {
				this.logger.error("sendReponse(): called with wrong (foreign) implementation of IRequestResponse !!!!");
			}
		} catch (IOException exc) {
			this.shutdown(false, "Unable to send handshake packet to proxy VM, shutting down");
		}
	}
	
	/**
	 * Internal class representing a Level-Zero request from the host 
	 * and the corresponding response.
	 * <p>
	 * ...
	 */
	private abstract class RequestResponse implements IRequestResponse {
		
		// the enqueing information
		private RequestResponse next = null;
		
		// identification data for the request/response
		private short reqSlot;
		private final byte[] reqUser;
		
		// the request data
		private int reqUserWord1;
		private int reqUserWord2;
		private final byte[] reqData;
		private int reqLength;
		
		// the response data
		private int respUserWord1;
		private int respUserWord2;
		private final byte[] respData;
		private int respLength;
		private byte aid;
		
		// construction: allocate the buffers
		public RequestResponse() {
			this.reqUser = new byte[REQ_USER_LEN];
			this.reqData = new byte[MAX_PACKET_LEN];
			this.respData = new byte[MAX_PACKET_LEN];
			this.reset();
		}
		
		// re-initialize the request-response for next use.
		protected void reset() {
			for (int i = 0; i < REQ_USER_LEN; i++) { this.reqUser[i] = _Blank; }
			this.reqSlot = 0;
			
			this.reqUserWord1= 0;
			this.reqUserWord2 = 0;
			this.reqLength = 0;
			
			this.respUserWord1= 0;
			this.respUserWord2 = 0;
			this.respLength = 0;
			
			this.aid = (byte)0x00;
			this.next = null;
		}
		
		// queue property
		private RequestResponse getNext() { return this.next; }
		private void setNext(RequestResponse next) { this.next = next; }

		/**
		 * Get the slot in which the inside (host) proxy manages this request.
		 */
		public short getSlot() {
			return this.reqSlot;
		}
		
		/**
		 * Get the user (VM-name) which issued this request.
		 */
		public byte[] getReqUser() { return this.reqUser; }
		
		// Set the request metadata.  
		private void setReqInfos(short slot, int userWord1, int userWord2) {
			this.reqSlot = slot;
			this.reqUserWord1 = userWord1;
			this.reqUserWord2 = userWord2;
			this.respUserWord1 = userWord1;
			this.respUserWord2 = userWord2;
		}
		
		/**
		 * Get the first user word of the request.
		 */
		public int getReqUserWord1() { return this.reqUserWord1; }
		
		/**
		 * Get the second user word of the request.
		 */
		public int getReqUserWord2() { return this.reqUserWord2; }
		
		/**
		 * Get the buffer for the request data. 
		 */
		public byte[] getReqData() { return this.reqData; }
		
		/**
		 * Get the length of the request data (transmitted by the host).
		 */
		public int getReqDataLen() { return this.reqLength; }
		
		/**
		 * Set the length of the request data transmitted by the host.
		 * @param len
		 */
		public void setReqDataLen(int len) { this.reqLength = Math.min(Math.max(0, len), MAX_PACKET_LEN); }
		
		/**
		 * Get the first user word for the response.
		 */
		public int getRespUserWord1() { return this.respUserWord1; }
		
		/**
		 * Set the first user word for the response.
		 */
		public void setRespUserWord1(int userWord1) { this.respUserWord1 = userWord1; }
		
		/**
		 * Get the second user word for the response.
		 */
		public int getRespUserWord2() { return this.respUserWord2; }
		
		/**
		 * Set the second user word for the response.
		 */
		public void setRespUserWord2(int userWord2) { this.respUserWord2 = userWord2; }
		
		/**
		 * Get the buffer for the response data.
		 */
		public byte[] getRespData() { return this.respData; }
		
		/**
		 * Get the currently defined response data length.
		 */
		public int getRespDataLen() { return this.respLength; }
		
		/**
		 * Set the response data length.
		 */
		public void setRespDataLen(int len) { this.respLength = Math.min(Math.max(0, len), MAX_PACKET_LEN); }
		
		// Get the (handshake-) Aid-code that will be used to send the response. 
		public byte getAid() { return this.aid; }
		
		// Set the (handshake-) Aid-Code to be used to send the response.
		private void setAid(byte aid) { this.aid = aid; }
		
		// encode a block of bytes and write the result to the OutputStream. 
		abstract protected void write(OutputStream os, byte[] src, int first, int count) throws IOException;
		
		// encode a single byte and use the OutputStream to transmit it
		abstract protected void write(OutputStream os, byte b) throws IOException;
		
		// encode the lower 8 bit (as "unsigned byte") of an integer 
		private void write(OutputStream os, int i) throws IOException {
			this.write(os, (byte)(i & 0x000000FF));
		}
		
		// finish an encoded stream by closing an possibly open 7-byte chunk (filling it up
		// to 7 bytes) and write the encoded block to the OutputStream.
		abstract protected void flush(OutputStream os) throws IOException;
		
		// Send the response part of this object to the OutputStream, with the Aid-code,
		// the "cursor position" and the username being sent in original encoding and
		// the rest of the response is in 7-to-8 encoding (slot, userword1, userword2, 
		// data length and data block).  
		private void transmit(OutputStream os) throws IOException {
			
			byte[] data = this.respData;
			int count = this.respLength;
			
			logger.debug("++ RequestResponse.transmit(len=", count, "): begin");
			
			os.flush();
			
			// send AID 
			os.write(this.aid);
			os.write((byte)0x40); // 2 blanks for cursor screen position
			os.write((byte)0x40);
			
			// send identifying data 
			os.write(this.reqUser);
			
			this.write(os, this.reqSlot >> 8);
			this.write(os, this.reqSlot);
			
			this.write(os, this.respUserWord1 >> 24);
			this.write(os, this.respUserWord1 >> 16);
			this.write(os, this.respUserWord1 >> 8);
			this.write(os, this.respUserWord1);
			
			this.write(os, this.respUserWord2 >> 24);
			this.write(os, this.respUserWord2 >> 16);
			this.write(os, this.respUserWord2 >> 8);
			this.write(os, this.respUserWord2);
			
			this.write(os, this.respLength >> 8);
			this.write(os, this.respLength);
			
			// send packet content, doing the 7-to-8 encoding
			this.write(os, data, 0, count);
			//for (int i = 0; i < count; i++) {
			//	this.write(os, data[i]);
			//}
			this.flush(os);
			
			// send binary EOR and transmit all buffered bytes
			os.write(TN_EOR);
			os.flush();
			logger.debug("++ RequestResponse.transmit(): end");
		}
	}
	
	/**
	 * Internal class representing a Level-Zero request from the host 
	 * and the corresponding response.
	 * <p>
	 * In addition to the <tt>IRequestResponse</tt> interface and the methods used internally
	 * to manage the request data itself, this class implements sending the response to a raw
	 * output stream including the 7-to-8 bytes encoding scheme to circumvent some strange behaviour
	 * of Hercules 3.07 (VM/370R6?), involving rarely but wrongly unescaped 0XFF-0xFF sequences,
	 * which seem to be interpreted as record end, leading to sporadically aborted blocks
	 * (as terminated prematurely) and handshake-failures (as some arbitrary characters  
	 * in the middle of a packet are promoted to AID-codes).
	 */
	private class RequestResponse7to8Encoded extends RequestResponse {
		
		/*
		 * 7-to-8 encoding: the data to be transmitted in encoded form is sent in chunks of
		 * 7 bytes which have to highest bit reset, followed by a byte having the collected high-bits
		 * in the lower 7 bits. This results in a byte stream garanteed to be free of 0xFF bytes
		 * to be escaped on the sending side an risking to be misinterpreted sporadically on the
		 * receiving (Hercules?) side.
		 */
		
		// the current state of the encoder engine.
		private int encodePos = 0;
		private byte[] encodedBytes = new byte[8];
		private byte escapeByte;
		private byte escapeMask;
		
		// reset the encoder engine to "start of the encoded block".
		private void resetEncoder() {
			this.encodePos = 0;
			this.escapeByte = 0;
			this.escapeMask = 0x40;
		}
		
		protected void reset() {
			super.reset();			
			this.resetEncoder();
		}
		
		protected void flush(OutputStream os) throws IOException {
			while(this.encodePos < 7) {
				this.encodedBytes[this.encodePos++] = (byte)0x00;
			}
			this.encodedBytes[7] = (byte)(this.escapeByte /*+ 0x40*/);
			os.write(this.encodedBytes, 0, 8);
			this.resetEncoder();
		}
		
		// encode a single byte and use the OutputStream if a 7-byte chunk is finished.
		protected void write(OutputStream os, byte b) throws IOException {
			if ((b & 0x80) != 0) { this.escapeByte |= escapeMask; }
			this.escapeMask >>= 1;
			this.encodedBytes[this.encodePos++] = (byte)((b & 0x7F) /*+ 0x40*/);
			if (this.encodePos >= 7) {
				this.encodedBytes[7] = (byte)(this.escapeByte /*+ 0x40*/);
				os.write(this.encodedBytes, 0, 8);
				this.resetEncoder();
			}
		}
		
		// encode a block of bytes and write the result to the OutputStream. 
		protected void write(OutputStream os, byte[] src, int first, int count) throws IOException {
			int limit = first + count;
			byte[] encBytes = this.encodedBytes;
			byte escByte = this.escapeByte;
			byte escMask = this.escapeMask;
			int encPos = this.encodePos;
			while(first < limit) {
				byte b = src[first++];
				if ((b & 0x80) != 0) { escByte |= escMask; }
				escMask >>= 1;
				encBytes[encPos++] = (byte)(b & 0x7F);
				if (encPos >= 7) {
					encBytes[7] = escByte;
					os.write(encBytes, 0, 8);
					encPos = 0;
					escByte = 0;
					escMask = 0x40;
				}	
			}
			this.encodePos = encPos;
			this.escapeByte = escByte;
			this.escapeMask = escMask;
		}
	}
	
	private  class RequestResponseBinaryEncoded extends RequestResponse {
		
		protected void flush(OutputStream os) throws IOException {
			os.flush();
		}
		
		// encode a single byte and use the OutputStream if a 7-byte chunk is finished.
		protected void write(OutputStream os, byte b) throws IOException {
			if (b == (byte)0xFF) {
				os.write((byte)0xFF);
				os.write((byte)0xFF);
			} else {
				os.write(b);
			}
		}
		
		// encode a block of bytes and write the result to the OutputStream. 
		protected void write(OutputStream os, byte[] src, int first, int count) throws IOException {
			int segStart = 0;
			int segLen = 0;		
			int pos = first;
			int limit = first + count;
			
			// send packet content, telnet-escaping 0xFF-codes
			while(pos < limit) {
				if (src[pos++] != (byte)0xFF) {
					segLen++;
				} else {
					if (segLen > 0) {
						logger.trace("++ RequestResponse.transmit(): segLen = ", segLen);
						os.write(src, segStart, segLen);
					}
					logger.trace("++ RequestResponse.transmit(): 0xFF");
					os.write(TN_FF, 0, TN_FF.length);
					segStart = pos;
					segLen = 0;
				}
			}
			if (segLen > 0) {
				logger.trace("++ RequestResponse.transmit(): segLen = ", segLen);
				os.write(src, segStart, segLen);
			}
		}
	}
	
	// send the response of the first enqueued request to the host
	// (requires to be called in synchronized(this) !)
	private void sendFirstQueued() throws IOException {
		if (this.sending) { return; } // already transmitting to host, currently not allowed
		
		RequestResponse curr = this.firstWaitingRequest;
		if (curr == null) { return; } // nothing queued ?
		
		this.logger.debug("++ sendNextQueue(): begin sending packet");
		
		if (curr.getNext() == null) { 
			this.logger.debug("   queue will be drained => HANDSHAKE_DATA");
			curr.setAid(HANDSHAKE_DATA);
		} else {
			this.logger.debug("   further requests queued  => HANDSHAKE_DATA_AND_WANT_SEND");
			curr.setAid(HANDSHAKE_DATA_AND_WANT_SEND);
		}
		
		this.sending = true;
		curr.transmit(this.osToHost);
	
		this.logger.debug("++ sendNextQueue(): end sending packet");
	}
	
	// remove the request element from the send queue.
	// (requires to be called in synchronized(this) !)
	private void finishFirstQueued() {
		if (!this.sending) { return; } // drop the queue head only if it was sent!
		
		RequestResponse curr = this.firstWaitingRequest;
		if (curr == null) { return; } // nothing queued ?
		
		this.firstWaitingRequest = curr.getNext();
		if (this.firstWaitingRequest == null) { 
			this.lastWaitingRequest = null;
			this.logger.debug("++ finishFirstQueued(): queue drained");
		} else {
			this.logger.debug("++ finishFirstQueued(): queue head finished");
		}
		
		this.slotsInUse.remove("S"+curr.getSlot());
		this.workingRequests.remove(curr);
		
		curr.setNext(this.freeRequest);
		this.freeRequest = curr;
	}
}

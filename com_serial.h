/***********************************************************************************
* File    : com_serial.h
*
* Legacy COM driver for Windows (Win95-Win10)
*
* (C)JoEmbedded.de - Version 25.06.2021
*
* Should work with als standard C compilers.
* Tested with Visual Studio and Embarcadero RAD Studio.
*
***********************************************************************************/


#ifdef __cplusplus
extern "C"{
#endif

// Parameters
#define COM_CB_XL       // Wenn definiet, "Block"-Callback verwenden, neu seit 3/18
#define DEFAULT_BAUDRATE 	115200

#define RTS_HANDSHAKE_OFF 0 // Default
#define RTS_CTS_HANDSHAKE 1 // For Modems, etc..
#define S_SETDTR        1
#define S_CLRDTR        0

// This structure describes a serial Port
typedef struct serial_port_info {
	int com_nr;                      // 1-xx
	int baudrate;
	int flags;                      // Spezialeinstellungen . Bisher Bit0: HardwareHS

	// General mainanace Data			   P: Private, *: Default, generally: write access only by functions allowed!
	HANDLE hPortId;						// P: Handle of the Port
	HANDLE hThread;         			// P: Event watch thread handle
	DWORD  dwThreadId;      			// P: Event watch thread ID
	DWORD  dwLastError;     			// Last error code from GetLastError()
	DWORD  dwCommErrors;    			// Last comm error flags from ClearCommError()

	// Specific Data for Events
	DWORD      dwEventMask;     		// P: Read Event mask for WaitCommEvent()
										// (+): Maybe malfunction on early W95 (?)
	// The Async burdon...
	OVERLAPPED olWait;
	OVERLAPPED olRead, olWrite;			// P: Overlapped I/O structures for read and write

	CRITICAL_SECTION ComCritical;

} SERIAL_PORT_INFO;


// nRTSControl. Note: Due to a BUG in W9x, the Auto-Toggle for RTS doesn't work.t
// dwSetEventMask:			*EV_RXCHAR | EV_BREAK | EV_CTS | EV_DSR | EV_ERR | EV_RING(+) | EV_RLSD | EV_TXEMPTY
//	nBaudRate				baudrate of the Port CB_110, ..., *CB_9600 , CB_115200, ...
//	nParity					*NOPARITY, EVENPARITY, ODDPARITY
//	nDataBits				4.. *8
//	nStopBits				*ONSTOPBIT, ONE5STOPBITS, TWOSTOPVBITS
//	nInputBufferSize		*4096, Size for Input Buffer
//	nOutputBufferSize		*4096, same for Output
//	nInputInterval			*MAXDWORT
//	nInputMultiplier		*0
//	nInputConstant			*0
//	nOutputMultiplier		*0
//	nOutputConstant			*1000

extern int SerialTest(int com_nr);
extern  int SerialOpen(SERIAL_PORT_INFO* spi);
extern  void SerialClose(SERIAL_PORT_INFO* spi);
extern  int SerialEscapeCommFunction(SERIAL_PORT_INFO* spi ,int dwFunc); // SETDTR CLRDTR SETRTS CLR RTS (Defines fuer RS232 in WiniocCTRL)

extern void SerialEnterCritical(SERIAL_PORT_INFO* spi);
extern void SerialLeaveCritical(SERIAL_PORT_INFO* spi);

#ifdef COM_CB_XL
 extern  void ext_xl_SerialReaderCallback(unsigned char *pc, unsigned int anz);    // ODER Aufruf fuer Alle auf einmal!
#else
 extern  void ext_SerialReaderCallback(unsigned char c);    // Aufruf fuer jedes Zeichen
#endif
extern  int SerialWriteCommBlock(SERIAL_PORT_INFO* spi,unsigned char* lpBytes, int nBytesToWrite);


// --- Hilfsfunktionen Seriell ---
extern  int SerialSetCommBreak(SERIAL_PORT_INFO* spi);
extern  int SerialClearCommBreak(SERIAL_PORT_INFO* spi);
extern  int SerialSetBaudRate(SERIAL_PORT_INFO* spi,int nBaudRate);
extern  int SerialSetParityDataStop(SERIAL_PORT_INFO* spi, int nParity, int nDataBits, int nStopBits);
extern  int SerialSetFlowControl(SERIAL_PORT_INFO* spi,int nFlowCtrl);
extern  int SerialSetBufferSizes(SERIAL_PORT_INFO* spi,int nInBufSize, int nOutBufSize);
extern  int SerialSetReadTimeouts(SERIAL_PORT_INFO* spi,int nInterval, int nMultiplier,  int nConstant);
extern  int SerialSetWriteTimeouts(SERIAL_PORT_INFO* spi,int nMultiplier, int nConstant);
extern  int SerialStartCommThread(SERIAL_PORT_INFO* spi,LPTHREAD_START_ROUTINE CommProc, void *pvData);
extern  DWORD SerialStopCommThread(SERIAL_PORT_INFO* spi);
extern  int SerialReadCommBlock(SERIAL_PORT_INFO* spi,LPSTR lpBytes, int nBytesToRead,	 int bExactSize);
extern  int SerialWaitCommEvent(SERIAL_PORT_INFO* spi);
extern  DWORD SerialCheckForCommEvent(SERIAL_PORT_INFO* spi,int bWait);
extern  int SerialGetCommModemStatus(SERIAL_PORT_INFO* spi,LPDWORD lpModemStat);
extern  int SerialSetCommMask(SERIAL_PORT_INFO* spi,DWORD fdwEvtMask);
extern  int SerialPurgeCommAll(SERIAL_PORT_INFO* spi);

#ifdef __cplusplus
}
#endif


// END

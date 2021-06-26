/***********************************************************************************
* File    : com_serial.c
*
* Legacy COM driver for Windows (Win95-Win10)
*
* (C)JoEmbedded.de - Version 25.06.2021
*
* Should work with als standard C compilers. 
* Tested with Visual Studio and Embarcadero RAD Studio.
*
***********************************************************************************/

#define _CRT_SECURE_NO_WARNINGS // For VisualStudio

#include <windows.h>
#include <winioctl.h>
#include <string.h>
#include <stdio.h>

#include "com_serial.h"

/*---------------------------------------------------------------------
* Return codes:
* 0: OK
* -1: OPEN
* -2: Create Event
* -3: Start Thread
* -4: SetBaudrate
* -5: Init Critical Section
*--------------------------------------------------------------------*/

#ifdef UNICODE
 #undef CreateFile
 #define CreateFile CreateFileA // 8-Bit Style!
#endif

/**********************************************************************
* Reader TASK
* We often have blocks of 4k and some additionals dara.
* It seems necessary to set all buffers to >4k (here 4k+100 bytes)
**********************************************************************/
#define MAX_SIZE 4096+100	// Maximum Buffer to read as one Block
static unsigned char  byBuffer[MAX_SIZE+1];
DWORD WINAPI SerialCommReader(void *pvData){
	DWORD dwEventMask;
	int   nSize;
	unsigned char *pb;
#ifndef COM_CB_XL
	int i;
#endif
	SERIAL_PORT_INFO* spi = pvData;	// MUST be spi...

	do{
		SerialWaitCommEvent(spi);
		dwEventMask = SerialCheckForCommEvent(spi,TRUE); // Wait until...
		// dwEvent-Maske now holding the Event, see USP3TERM for more...
			// like' if(dwEventMask & EV_BREAK) ... <Break>'
		if(dwEventMask & EV_RXCHAR){
			// Read as much as possible and display it on the screen
			nSize = SerialReadCommBlock(spi,(char*)byBuffer, MAX_SIZE, FALSE);
			if(nSize) {
						pb=byBuffer;
#ifdef COM_CB_XL
						ext_xl_SerialReaderCallback(pb, nSize);
#else
						for(i=0;i<nSize;i++) ext_SerialReaderCallback(*pb);
#endif
			}
			// Set it up to wait for the next event
		}
	}while(dwEventMask);	// No-Event: Process shutting down?
	return 0;
}

/*******************************************************************************
* try if COM com_nr is available: 0: OK, else not available
*******************************************************************************/
int SerialTest(int com_nr){
	HANDLE hPortHandle;
	char buf[24];

	sprintf(buf,"\\\\.\\COM%d",com_nr);	// Testet DIESE Schnittstelle
	hPortHandle = CreateFile(buf, GENERIC_READ | GENERIC_WRITE,
				 0, NULL, OPEN_EXISTING,
				 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH |
				 FILE_FLAG_OVERLAPPED , NULL);

	if(hPortHandle == INVALID_HANDLE_VALUE) {
		return -1; // Not Available
	}else{
		 CloseHandle(hPortHandle);
		 return 0;
	}
}


/*******************************************************************************
* Create a internal handle to a serial port 1..x and open it with default params
* Die ganz korrekte Version fuer einen Namen ist:
* #define COM_NAME "\\\\.\\COM1"        (2BS-DotBS) im String
* Am PC muss man wissen, was man aufmachen moechte. Bassta!
*******************************************************************************/
int SerialOpen(SERIAL_PORT_INFO* spi){
	HANDLE hPortHandle;
	char buf[24];

	// Schauen, ob dieses interface schlecht ist, wenn ja: res=1;
	hPortHandle=INVALID_HANDLE_VALUE;
	sprintf(buf,"\\\\.\\COM%d",spi->com_nr);	// Testet DIESE Schnittstelle
	// Open the port.  The device must exist and must be opened for
	// exclusive access (no sharing).  No security is used, overlapped
	// I/O is enabled.
	hPortHandle = CreateFile(buf, GENERIC_READ | GENERIC_WRITE,
				 0, NULL, OPEN_EXISTING,
				 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH |
				 FILE_FLAG_OVERLAPPED , NULL);

	if(hPortHandle == INVALID_HANDLE_VALUE) return -1; // Open

	// Create the overlapped event structures.  No security, explicit
	// reset request, initial event reset, no name.
	spi->olRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	spi->olWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	spi->olWait.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if(!spi->olRead.hEvent || !spi->olWrite.hEvent || !spi->olWait.hEvent) {
			return -2;        // Event
	}

	spi->hPortId = hPortHandle;

	// Okay, everything is open, set up some default parameters...
	if(!spi->baudrate) spi->baudrate=DEFAULT_BAUDRATE;
	if(!SerialSetBaudRate(spi,spi->baudrate)){
			return -4;        // SetBaudrate
	}

	SerialSetParityDataStop(spi,NOPARITY, 8, ONESTOPBIT);

	// Flow-Control handshake via Bit 0; Bit 1 gesetzt: Handshake AUS
	if(!(spi->flags&1)) {
		SerialSetFlowControl(spi,RTS_HANDSHAKE_OFF);
	}else{
		SerialSetFlowControl(spi,RTS_CTS_HANDSHAKE);
	}

    // Often we have buffers of 4k with little Overhead
	SerialSetBufferSizes(spi,4096+100,4096+100);
	SerialSetReadTimeouts(spi,MAXDWORD,0,0);
	SerialSetWriteTimeouts(spi,0,3000);
	// Set it up to receive data by default and clear any
	// stray data that may be hanging around.
	SerialSetCommMask(spi, EV_RXCHAR);
	SerialPurgeCommAll(spi);

	SerialEscapeCommFunction(spi,SETDTR);	// Power DTR/RTS-Hardware...
	SerialEscapeCommFunction(spi,SETRTS);

	if(!InitializeCriticalSectionAndSpinCount(&spi->ComCritical,1)){
				return -5;   // CreateThread
	}

	if(!SerialStartCommThread(spi,SerialCommReader,spi)) {
				return -3;   // CreateThread
	 }
	return 0;   // OK
}

/* Close an opened serial handle */
void SerialClose(SERIAL_PORT_INFO* spi){

	  // Stop any running comm thread
	  SerialStopCommThread(spi);
	  SerialEscapeCommFunction(spi,CLRDTR);	// Power DTR/RTS-Hardware...
	  SerialEscapeCommFunction(spi,CLRRTS);

	  // Purge any outstanding reads/writes
	  SerialPurgeCommAll(spi);

	  // Dispose of the overlapped I/O structures and close the port
	  CloseHandle(spi->olRead.hEvent); // Die behalten wir alle
	  CloseHandle(spi->olWrite.hEvent);
	  CloseHandle(spi->olWait.hEvent);
	  CloseHandle(spi->hPortId);

	  DeleteCriticalSection(&spi->ComCritical);

}

void SerialEnterCritical(SERIAL_PORT_INFO* spi){
	EnterCriticalSection(&spi->ComCritical);
}
void SerialLeaveCritical(SERIAL_PORT_INFO* spi){
	LeaveCriticalSection(&spi->ComCritical);
}

/* Use Constants CBR_xxxx  - Wird nicht in SERIAL_INIT_PORTDESC eingetragen... */
int SerialSetBaudRate(SERIAL_PORT_INFO* spi,int nBaudRate){
	 DCB dcb;
	 int bRetVal;

	 spi->dwLastError = ERROR_SUCCESS;
	 // Always retrieve the current state before modifying it
	 dcb.DCBlength = sizeof(DCB);
	 bRetVal = GetCommState(spi->hPortId, &dcb);
	 if(bRetVal){
		 dcb.BaudRate = nBaudRate;
		 bRetVal = SetCommState(spi->hPortId, &dcb);
	 }
	 if(!bRetVal) spi->dwLastError = GetLastError();
	 return bRetVal;
}

int SerialSetParityDataStop(SERIAL_PORT_INFO* spi, int nParity, int nDataBits, int nStopBits){
	 DCB dcb;
	 int bRetVal;

	 spi->dwLastError = ERROR_SUCCESS;
	 dcb.DCBlength = sizeof(DCB);
	 bRetVal = GetCommState(spi->hPortId, &dcb);

	 if(bRetVal)
	 {
		 dcb.Parity   = (unsigned char)nParity;
		 dcb.ByteSize = (unsigned char)nDataBits;
		 dcb.StopBits = (unsigned char)nStopBits;
		 dcb.fParity  = TRUE;        // Parity errors are reported
		 dcb.fErrorChar = FALSE;

		 // Enable the options
		 bRetVal = SetCommState(spi->hPortId, &dcb);
	 }

	 if(!bRetVal) spi->dwLastError = GetLastError();

	 return bRetVal;
}

/* Type of RTS Flow Control
* BUG: support.microsoft says, that there is a BUG in the RTS_CONTROL-TOGGLE
* implementation. So we can't use it... */
int SerialSetFlowControl(SERIAL_PORT_INFO* spi,int nFlowCtrl){
	 DCB  dcb;
	 int bRetVal;

	 spi->dwLastError = ERROR_SUCCESS;

	 dcb.DCBlength = sizeof(DCB);
	 bRetVal = GetCommState(spi->hPortId, &dcb);

	 if(bRetVal){
		 // Set hardware flow control options
		 switch(nFlowCtrl){
		 case RTS_HANDSHAKE_OFF:	// No Handshake at all
			dcb.fOutxCtsFlow = FALSE;
			// Note: If trying to access a device which usese handshake with RTS_HANDSHAKE_OFF,
         // the device will not respond with RTS_CONTROL_DISABLE...
			//dcb.fRtsControl = RTS_CONTROL_DISABLE; // Initial State: RTS is OFF
			dcb.fRtsControl = RTS_CONTROL_ENABLE; // Initial State: RTS is ON
			break;
		 case RTS_CTS_HANDSHAKE:	// Standard RTS-CTS
			dcb.fOutxCtsFlow = TRUE;
			dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
			break;
		 default:
			return 0;
		 }
	 }
	 if(bRetVal){
		 // Enable DTR-Signal, but don't use it..
		 dcb.fOutxDsrFlow = 0;
		 dcb.fDtrControl = DTR_CONTROL_ENABLE; // DTR for our Purpose always on...
		 dcb.fDsrSensitivity=FALSE;

		 // Disable software flow control options
		 dcb.fInX = dcb.fOutX = 0;
		 dcb.fTXContinueOnXoff = TRUE;
		 // Miscellaneous
		 dcb.fBinary = TRUE;     // Must always be TRUE for NT
		 dcb.fNull = FALSE;      // Keep received null bytes

		 // Enable the options
		 bRetVal = SetCommState(spi->hPortId, &dcb);
	 }

	 if(!bRetVal) spi->dwLastError = GetLastError();

	 return bRetVal;
}

int SerialSetBufferSizes(SERIAL_PORT_INFO* spi,int nInBufSize, int nOutBufSize){
	int bRetVal;
	spi->dwLastError = ERROR_SUCCESS;

	bRetVal = SetupComm(spi->hPortId, nInBufSize, nOutBufSize);
	if(!bRetVal)spi-> dwLastError = GetLastError();

	return bRetVal;
}

int SerialSetReadTimeouts(SERIAL_PORT_INFO* spi,int nInterval, int nMultiplier,  int nConstant){
	 COMMTIMEOUTS  CommTimeouts;
	 int bRetVal;

	 spi->dwLastError = ERROR_SUCCESS;

	 // Set the read timeout values
	 bRetVal = GetCommTimeouts(spi->hPortId, &CommTimeouts);
	 if(bRetVal){
		 CommTimeouts.ReadIntervalTimeout = nInterval;
		 CommTimeouts.ReadTotalTimeoutMultiplier = nMultiplier;
		 CommTimeouts.ReadTotalTimeoutConstant = nConstant;
		 bRetVal = SetCommTimeouts(spi->hPortId, &CommTimeouts);
	 }

	 if(!bRetVal) spi->dwLastError = GetLastError();

	 return bRetVal;
}

int SerialSetWriteTimeouts(SERIAL_PORT_INFO* spi,int nMultiplier, int nConstant)
{
	 COMMTIMEOUTS  CommTimeouts;
	 int bRetVal;

	 spi->dwLastError = ERROR_SUCCESS;

	 // Set the write timeout values
	 bRetVal = GetCommTimeouts(spi->hPortId, &CommTimeouts);
	 if(bRetVal){
		 CommTimeouts.WriteTotalTimeoutMultiplier = nMultiplier;
		 CommTimeouts.WriteTotalTimeoutConstant = nConstant;
		 bRetVal = SetCommTimeouts(spi->hPortId, &CommTimeouts);
	 }

	 if(!bRetVal) spi->dwLastError = GetLastError();

	 return bRetVal;
}

int SerialStartCommThread(SERIAL_PORT_INFO* spi,LPTHREAD_START_ROUTINE CommProc, void *pvData){
	 if(spi->hThread){
		 // A thread is already active for this object
		 spi->dwLastError = ERROR_ALREADY_EXISTS;
		 return FALSE;
	 }

	 spi->dwLastError = ERROR_SUCCESS;

	 // Create a separate thread to send and/or receive data
	 spi->hThread = CreateThread(NULL, 0, CommProc, pvData, NORMAL_PRIORITY_CLASS, &spi->dwThreadId);
	 if(!spi->hThread) return FALSE;
	 return TRUE;
}

DWORD SerialStopCommThread(SERIAL_PORT_INFO* spi){
	 DWORD dwExitCode;

	 spi->dwLastError = dwExitCode = ERROR_SUCCESS;
	 spi->dwThreadId = 0;

	 // Disable event notification and wait for the thread to halt.
	 // Clearing the comm mask will generate a comm event that
	 // receives a mask value equal to zero.  A thread can use
	 // that as an indicator that the port is closing.
	 SetCommMask(spi->hPortId, 0);

	 // Wait for it to exit.
	 while(GetExitCodeThread(spi->hThread, &dwExitCode) &&  dwExitCode == STILL_ACTIVE) Sleep(0L);

	 CloseHandle(spi->hThread);
	 spi->hThread = NULL;

	 return dwExitCode;
}


// Read a block of data from the port.  If bExactSize is TRUE, it will
// attempt to read the full byte count.  If bExactSize is FALSE, it will
// read whatever is available and return immediately.  Note that read timeouts
// must be in effect for bExactSize = TRUE to work properly.
int SerialReadCommBlock(SERIAL_PORT_INFO* spi,LPSTR lpBytes, int nBytesToRead,	 int bExactSize){
	 COMSTAT ComStat;
	 DWORD dwBytesRead = 0;

	 // Return zero if handle is invalid
	 if(spi->hPortId == INVALID_HANDLE_VALUE) return 0;

	 // Read max length or only what is available
	 ClearCommError(spi->hPortId, &spi->dwCommErrors, &ComStat);

	 // If not requiring an exact byte count, get whatever is available
	 if(!bExactSize && nBytesToRead > (int)ComStat.cbInQue)	 nBytesToRead = ComStat.cbInQue;

	 if(nBytesToRead > 0)
		 if(!ReadFile(spi->hPortId, lpBytes, nBytesToRead, &dwBytesRead, &spi->olRead)){
			 // Did it fail because I/O is still pending?
			 spi->dwLastError = GetLastError();
			 if(spi->dwLastError == ERROR_IO_PENDING){
				 // Wait for read to complete or a timeout.
				 // NOTE: You can set the last parameter to FALSE for no
				 //       waiting, but it tends to consume a lot of CPU
				 //       cycles which isn't very friendly.
				 while(!GetOverlappedResult(spi->hPortId, &spi->olRead, &dwBytesRead, TRUE)){
					 spi->dwLastError = GetLastError();
					 if(spi->dwLastError != ERROR_IO_INCOMPLETE){
						 // An error occurred
						 ClearCommError(spi->hPortId, &spi->dwCommErrors, &ComStat);
						 break;
					 }
					 // Not finished, wait for it
				 }
			 }else ClearCommError(spi->hPortId, &spi->dwCommErrors, &ComStat);
		 }
	 return (int)dwBytesRead;
}

// Write a block of data to the port. Return the Nr. of Bytes written...
int SerialWriteCommBlock(SERIAL_PORT_INFO* spi,unsigned char* lpBytes, int nBytesToWrite){
	 COMSTAT ComStat;
	 DWORD dwBytesWritten = 0;

	 // Return zero if handle is invalid
	 if(spi->hPortId == INVALID_HANDLE_VALUE) return 0;

	 if(!WriteFile(spi->hPortId, lpBytes, nBytesToWrite, &dwBytesWritten, &spi->olWrite)){
		 // Did it fail because I/O is still pending?
		 spi->dwLastError = GetLastError();
		 if(spi->dwLastError == ERROR_IO_PENDING){
			 // Wait for write to complete or a timeout.
			 // NOTE: You can set the last parameter to FALSE for no
			 //       waiting, but it tends to consume a lot of CPU
			 //       cycles which isn't very friendly.
			 while(!GetOverlappedResult(spi->hPortId, &spi->olWrite, &dwBytesWritten, TRUE)){
				 spi->dwLastError = GetLastError();
				 if(spi->dwLastError != ERROR_IO_INCOMPLETE){
					 // An error occurred
					 ClearCommError(spi->hPortId, &spi->dwCommErrors, &ComStat);
					 break;
				 }
				 // Not finished, wait for it
			 }
		 }else    // An error occurred
			 ClearCommError(spi->hPortId, &spi->dwCommErrors, &ComStat);
	 }
	 return (int)dwBytesWritten;
}

// Setup a wait event for the port.  CheckForCommEvent() can be used to
// see if any events have occurred yet.
//
// NOTE: MSKB Article ID: Q137862 - Unless it has been fixed, a bug in
//       Win95 prevents EV_RING from being seen.
//#include <stdio.h>

int SerialWaitCommEvent(SERIAL_PORT_INFO* spi){
	int bRetVal;
	spi->dwLastError = ERROR_SUCCESS;
	spi->dwEventMask = 0;

	 bRetVal = WaitCommEvent(spi->hPortId, &spi->dwEventMask, &spi->olWait);
	 if(!bRetVal){
		 spi->dwLastError = GetLastError();
		 if(spi->dwLastError == ERROR_IO_PENDING) {
       	bRetVal = TRUE;
		}
	 }
	 return bRetVal;
}

// Check to see if any comm events are available.  If so, return what
// has occurred.
// Return 0 for CLOSED SPIs, because of 0 program EXITS (JW-2007)
// dwThreadID is set to 0 on exit
DWORD SerialCheckForCommEvent(SERIAL_PORT_INFO* spi,int bWait){
	 DWORD   dwBytesRead;
	 COMSTAT ComStat;
    DWORD res;

	 // If we don't want to wait here, return immediately.
	 if(!GetOverlappedResult(spi->hPortId, &spi->olWait, &dwBytesRead, bWait)){
		 if(GetLastError() != ERROR_IO_INCOMPLETE){
			 // An error occurred
			 ClearCommError(spi->hPortId, &spi->dwCommErrors, &ComStat);
			 SerialWaitCommEvent(spi);
		 }
		 // Else not finished, so nothing is pending
		 res = 0;
	 }else  res= spi->dwEventMask;

	// Result =0 but thread still active: Return DUMMY (see win32/winbase.h)
	 if(!res && spi->dwThreadId) res= EV_EVENT1;
    return res;
}


int SerialSetCommBreak(SERIAL_PORT_INFO* spi){
	 int bRetVal;

	 spi->dwLastError = ERROR_SUCCESS;

	 bRetVal = SetCommBreak(spi->hPortId);
	 if(!bRetVal) spi->dwLastError = GetLastError();

	 return bRetVal;
}


int SerialClearCommBreak(SERIAL_PORT_INFO* spi){
	int bRetVal;

	spi->dwLastError = ERROR_SUCCESS;

	 bRetVal = ClearCommBreak(spi->hPortId);
	 if(!bRetVal) spi->dwLastError = GetLastError();

	 return bRetVal;
}

int SerialEscapeCommFunction(SERIAL_PORT_INFO* spi,int dwFunc){
	 DCB  dcb;
	 int bRetVal;

	 spi->dwLastError = ERROR_SUCCESS;

	 dcb.DCBlength = sizeof(DCB);
	 GetCommState(spi->hPortId, &dcb);

	 // The EscapeComm() function should not be used to adjust line
	 // settings if dcb.fDtrControl is set to DTR_CONTROL_HANDSHAKE
	 // or dcb.fRtsControl is set to RTS_CONTROL_HANDSHAKE or
	 // RTS_CONTROL_TOGGLE (that's what is says in the online help
	 // for the DCB structure).
	 if((dcb.fDtrControl == DTR_CONTROL_HANDSHAKE) &&  (dwFunc == CLRDTR || dwFunc == SETDTR)) return FALSE;

	 if((dcb.fRtsControl == RTS_CONTROL_HANDSHAKE || dcb.fRtsControl == RTS_CONTROL_TOGGLE) &&   (dwFunc == CLRRTS || dwFunc == SETRTS))	 return FALSE;

	 bRetVal = EscapeCommFunction(spi->hPortId, dwFunc);
	 if(!bRetVal) spi->dwLastError = GetLastError();
	 return bRetVal;
}

int SerialGetCommModemStatus(SERIAL_PORT_INFO* spi,LPDWORD lpModemStat){
	 int bRetVal;
	 spi->dwLastError = ERROR_SUCCESS;

	 bRetVal = GetCommModemStatus(spi->hPortId, lpModemStat); // OK: <>0 Error: 0
	 if(!bRetVal) spi->dwLastError = GetLastError();

	 return bRetVal;
}

// NOTE: MSKB Article ID: Q137862 - Unless it has been fixed, a bug in
//       Win95 prevents EV_RING from being seen.
int SerialSetCommMask(SERIAL_PORT_INFO* spi,DWORD fdwEvtMask){
	 int bRetVal;
	 spi->dwLastError = ERROR_SUCCESS;

	 bRetVal = SetCommMask(spi->hPortId, fdwEvtMask);
	 if(!bRetVal) spi->dwLastError = GetLastError();

	 return bRetVal;
}

// Simply Purge ALL...
int SerialPurgeCommAll(SERIAL_PORT_INFO* spi){
	 int bRetVal;
	 spi->dwLastError = ERROR_SUCCESS;
	 bRetVal = 	PurgeComm(spi->hPortId, PURGE_TXABORT | PURGE_RXABORT |	 PURGE_TXCLEAR | PURGE_RXCLEAR);
	 if(!bRetVal) spi->dwLastError = GetLastError();

	 return bRetVal;
}
// END

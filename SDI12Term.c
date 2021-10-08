/***********************************************************************************
* File    : SDI12Term.c
*
* SDI12Term - Simple SDI12 Console for Windows (Win95-Win10)
*
* (C)JoEmbedded.de - Version (see below)
*
* Should work with all standard C compilers. Tested with
* - Microsoft Visual Studio Community 2019 Version 16.4.2
* - Embarcadero(R) C++Builder 10.3 (Community Edition)
*
* Verions:
* 1.01 - Detect CRC16 in Replies
* 1.02 - Bug in Scan
* 1.03 - CRC fix
* 1.04 - Bug with negative Values
* 1.05 - Cosmetics
*
* todo: 
* - Add "Retries" for sensors with slow wakup ( item with low priority, until 
*                 now no sensor with slow wakeup found)
***********************************************************************************/

#define _CRT_SECURE_NO_WARNINGS // For VisualStudio

#include <conio.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <windows.h>
#include <time.h>
#pragma hdrstop

#undef errno
#define errno err_no
#undef NO_ERROR
#undef ERROR_BUSY

#include "com_serial.h"

//---------------------------------------------------------------------------
// Globals
#define VERSION "1.06 / 08.10.2021"
int comnr=1;
/* Serial Port */
SERIAL_PORT_INFO mspi;

#define PROMPT_MS	3000
#define LOOP_MS		10
#define COMMAND_MS	100

#define MAX_CMDLEN	80
unsigned char cmd_buf[MAX_CMDLEN + 1]; // for 0
volatile int cmd_idx = -1;	// If >=0: In Command
volatile int cmd_prompt_cnt;

volatile int reply_cnt = 0;
volatile int reply_char = 0;

#define REPLY_LEN	80
unsigned char reply_buf[REPLY_LEN + 1]; // for 0
volatile int reply_idx = -1;	// If >=0: Reply found

//---------------------------------------------------------------------------
// Calculate SDI12 CRC16 (using Standard Polynom A001)
unsigned int calc_sdi12_crc16(unsigned char* pc, int len) {
	unsigned int crc = 0;
	while (len--) {
		crc ^= *pc++;
		for (int i = 0; i < 8; i++) {
			if (crc & 1) {
				crc >>= 1;
				crc ^= 0xA001;
			}else {
				crc >>= 1;
			}
		}
	}
	return crc;
}

// Extern: Read incomming characters from COM
volatile static bool lf_on_break = true;
void ext_xl_SerialReaderCallback(unsigned char* pc, unsigned int anz) {
	unsigned int i;
	unsigned int rcrc,scrc;
	unsigned char c;

	if (cmd_prompt_cnt > 0 ) printf("("); // Detect incomming chars while entering command
	for (i = 0; i < anz; i++) {
		c = *pc++;
		// Show what is comming in.
		if(c >= ' ' && c <= 126) printf("%c", c);
		else if (!c) {
			if (lf_on_break) printf("\n<BREAK>");
			else printf("<BREAK>");
			lf_on_break = true;
		}
		else if (c == 13) printf("<CR>");
		else if (c == 10) printf("<LF>");
		else printf("<%d>\a", c); // Something Strange?

		// Optionaly record Replies for CRC
		if (reply_idx >= 0) {
			if (reply_idx < REPLY_LEN) reply_buf[reply_idx++] = c;
			// Check if Reply has CRC:   a+xx...xxCCC<CR><LF> (Data) or aCCC<CR><LF> (No Data)
		
			if (c == 10 && reply_idx >= 6 && reply_buf[reply_idx - 2] == 13) {
				reply_idx -= 2;
				reply_buf[reply_idx--] = 0; // Delete <CR><LF> and check for possible CRC
				if (reply_buf[reply_idx] >= 64 && reply_buf[reply_idx] <= 127 &&
					reply_buf[reply_idx - 1] >= 64 && reply_buf[reply_idx - 1] <= 127 &&
					reply_buf[reply_idx - 2] >= 64 && reply_buf[reply_idx - 2] <= 127 &&
					(reply_buf[1] == '+' || reply_buf[1] == '-' || reply_idx == 3) ) {

					scrc = calc_sdi12_crc16(reply_buf, reply_idx - 2);
					rcrc = ((reply_buf[reply_idx-2] - 64) << 12) + ((reply_buf[reply_idx-1] - 64) << 6) + ((reply_buf[reply_idx] - 64));

					if (scrc == rcrc) printf(" => [CRC OK] ");
					else printf(" => [CRC ERROR]\a ");

				}
			}
		}
		if (c == '!') reply_idx = 0;

	}
	if (cmd_prompt_cnt > 0) printf(")");
	reply_cnt += anz;
	reply_char = 1;
}
// Helper: Send 1 single char to COM
void sdi_putc(unsigned char c){
	SerialWriteCommBlock(&mspi, &c, 1);
}
// Helper: Send Break-Signal on COM (for at least 12 msec, followed by a pause of at least 8.33 msec)
#define BREAK_MS 20
#define AFTER_BREAK_MS 10
void sdi_sendbreak(void) {
	SerialSetCommBreak(&mspi);
	Sleep(BREAK_MS);
	SerialClearCommBreak(&mspi);
	Sleep(AFTER_BREAK_MS);
}

// Send 0-terminated SDI-Cmd with leading BREAK on COM
void sdi_sendcmd(unsigned char* pc) {
	reply_cnt = -1;	// Expact add. BREAK
	lf_on_break = false;	
	sdi_sendbreak();
	while (*pc) sdi_putc(*pc++);
	for (;;) {	// Wait as long as input is receiving
		reply_char = 0;
		Sleep(COMMAND_MS);
		if (!reply_char) break;
	}
	// Return via reply_cnt
}

// Scan the Bus
void sdi_scanbus(unsigned char astart, unsigned char aend) {
	printf("\n--- Scan Start ---\n");
	for (unsigned char ai = astart; ai <= aend; ai++) {
		sprintf((char*) cmd_buf, "%cI!", ai);
		cmd_idx = 3;
		printf("Scan %c => ",ai);
		cmd_prompt_cnt = 0;	// Editing finished
		sdi_sendcmd(cmd_buf);
		if (reply_cnt == cmd_idx) printf(" => <NO_REPLY>"); // Count each readback char
		else if (reply_cnt < cmd_idx) printf(" => <SDI_ERROR>\a"); // Nothing read???
		printf("\n");
		cmd_idx = -1;
	}
	printf("\n");

}
/* Console Wrapper EMBARCADERO / VS */
int loc_kbhit(void){
#ifdef __BORLANDC__
	return kbhit();
#else // VS
	return _kbhit();
#endif
}
int loc_getch(void){
#ifdef __BORLANDC__
	return getch();
#else // VS
	return _getch();
#endif
}

/*--- sdi_term()) ------*/
void sdi_term(void){
	int c;

	printf("\n--- MENUE --\n");
	printf("Enter SDI12 Commands, send it with '!' (leading <BREAK> added).\n");
	printf("Non-SDI12 characters in Commands (<NL>,<CR>, ...) are ignored.\n");
	printf("<TAB>: Scan SDI12 Bus (Addresses '0' to '9')\n");
	printf("<ESC>: Exit\n\n");

	printf("Ready...\n");
	for(;;){

		if (loc_kbhit()) {
			c = loc_getch();
			if (c == 27) {
				printf("<ESC>");
				break;	// ESC -> Exit
			} if (c == '\t') {
				if (cmd_prompt_cnt > 0) {
					printf(" => <INPUT CANCELED>\a");	// Ignore this command
					cmd_idx = -1;
				}
				sdi_scanbus('0', '9');
			}else if (c == '\b') {	// Backspace
				if (cmd_idx > 0) {
					cmd_prompt_cnt = PROMPT_MS;
					printf("\b \b");
					cmd_buf[--cmd_idx] = 0;
				}else if(!cmd_idx) {
					cmd_prompt_cnt = PROMPT_MS;
					printf("\b\b\b   \b\b\b");
					cmd_idx = -1;
					cmd_prompt_cnt = 0;
				}
			}else if (c >= ' ' && c <= 126 && cmd_idx < MAX_CMDLEN) {
				cmd_prompt_cnt = PROMPT_MS;
				if (cmd_idx < 0) {
					printf("\n> ");
					cmd_idx = 0;
				}

				printf("%c",c);
				cmd_buf[cmd_idx++] = c;
				cmd_buf[cmd_idx] = 0;
				if (c == '!') {	// Send CMD after '!'
					printf(" => ");
					cmd_prompt_cnt = 0;	// Editing finished
					sdi_sendcmd(cmd_buf);
					if (reply_cnt == cmd_idx) printf(" => <NO_REPLY>\a"); // Count each readback char
					else if (reply_cnt < cmd_idx) printf(" => <SDI_ERROR>\a"); // Nothing read???
					cmd_idx = -1;
				}
			}else if (c == '\r' || c == '\n') {	// NL/CR
				if (cmd_prompt_cnt > 0) {
					printf(" => <INPUT CANCELED>\a\n");	// Ignore this command
					cmd_idx = -1;
					cmd_prompt_cnt = 0;	// Editing finished
				}else {
					printf("\n"); // NL for cosmetics
				}
			}else { // ignore Non-CMD Characters NL, CR, ...
				printf("\a");
			}
		}

		Sleep(LOOP_MS);
		if (cmd_prompt_cnt > 0) {
			cmd_prompt_cnt -= LOOP_MS;
			if (cmd_prompt_cnt <= 0) {
				printf(" => <INPUT TIMEOUT>\a");	// Ignore this command
				cmd_idx = -1;
			}
		}
	}
	while(loc_kbhit()) (void)loc_getch();
}

/*---------------MAIN------------------------------*/
int main(int argc, char* argv[]){
	int i,err=0;
	int res;

	printf("-----------------------------------------------------------------------\n");
	printf("* SDI12Term (C)JoEmbedded.de - V" VERSION "\n");
	printf("-----------------------------------------------------------------------\n");

	for(i=1;i<argc;i++){
		if (argv[i][0] == '-') switch (argv[i][1]) {
		case 'c':
			comnr = atoi(&argv[i][2]);
			if (comnr < 1 || comnr>255) err++; // Only use COM1..255
			break;
		default:
			err++;
		}
		else err++;
	}
	//---------------------------------
	printf("Open COM%d:\n",comnr);

	//---------------------- INIT------------
	mspi.com_nr=comnr;
    mspi.baudrate=1200;

	res=SerialOpen(&mspi);

	if(res) {
		printf("<ERROR: Open 'COM%d:'>\n--- Scan COMs: ---",comnr);
		for(i=1;i<256;i++){
			if(!SerialTest(i)) {
				if(i==comnr) printf("COM%d: *** selected ***\n",i);
				printf("COM%d: available\n",i);
			}else if(i==comnr) printf("COM%d: *** selected, but not available ***\n",i);

		}
		// Check Coms
		err++;
	}
	if(err) {
		printf("\n<ERRORS!>\nArguments:\n");
		printf("-cNR (Baudrate fixed: 1200Bd-7E1, Default: '-c1')\n");
		printf("<NL>");
		(void)getchar();
	}else{
		// Set to SPI12 framing 7E1
		res = SerialSetParityDataStop(&mspi, EVENPARITY, 7, ONESTOPBIT);
		if (!res) {
			printf("<ERROR: Baudrate 1200Bd-7E1 not possible on COM%d:>", comnr);
		} else {
			sdi_term();
		}

		//---------------------- Exit------------
		SerialClose(&mspi);
	}

	printf("\n\n*** Bye! ***\n");
	return 0;
}
//---------------------------------------------------------------------------

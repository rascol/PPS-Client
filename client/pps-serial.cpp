/**
 * @file pps-serial.cpp
 * @brief This file contains functions and structures for accessing GPS time updates via the serial port.
 */

/*
 * Copyright (C) 2020  Raymond S. Connell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "../client/pps-client.h"

#define MSG_WAIT_TIME 990000
#define SECS_PER_HOUR 3600
#define VERIFY_NUM 10
#define MAX_NOT_READY 60
//#define DEBUG

extern struct G g;
extern struct ppsFiles f;

/**
 * Local file-scope shared variables.
 */
static struct serialLocalVars {
	bool noGPRMCmsg;
	bool bufferIsEmpty;
	bool badGPRMCmsg;
	bool badTimeConversion;
	int activeCount;
	bool threadIsBusy[1];
	pthread_t tid[1];
	int timeCheckEnable;
	char *serialPort;
	int lostGPSCount;
	bool doReadSerial;
	int lastSerialTimeDif;
	int timeDiff[VERIFY_NUM];
	int diffCount[VERIFY_NUM];
	int notReadyCount;
	int missMsg;
	int gmtSeconds;
	char msgbuf[10000];
	char *ptimestr;
} s;

/**
 * Processes a block of GPS messages to find a GPRMC
 * message and extract the local time in seconds.
 *
 * @param[in] msgbuf The block of GPS messages to
 * process.
 *
 * @param[in,out] tcp A struct pointer used to pass
 * thread data.
 *
 * @param[out] gmtSeconds The UTC time in seconds.
 *
 * @returns true if a complete GPRMC message is found
 * and the time was extracted. Else false.
 */
bool getUTCfromGPSmessages(const char *msgbuf, timeCheckParams *tcp, time_t *gmt0Seconds){

	char scnbuf[10];
	memset(scnbuf, 0, 10);

	struct tm gmt;
	memset(&gmt, 0, sizeof(struct tm));

	char *active, *ctmp2, *ctmp4;

	active = scnbuf;
	ctmp2 = active + 2;
	ctmp4 = ctmp2 + 2;

	float ftmp1, ftmp3, ftmp5, ftmp6;
	int frac;

	char *pstr = (char *)msgbuf;

													// $GPRMC,144940.000,A,3614.5286,N,08051.3851,W,0.01,219.16,260420,,,D*71
	pstr = strstr(pstr, "$GPRMC");					// $GPRMC,205950.000,A,3614.5277,N,08051.3851,W,0.02,288.47,051217, ,,D*75
	if (pstr != NULL){								// If a GPRMC message was received, continue.

		char *pNext = strstr(pstr, "\n");			// this is a complete message by looking for CR
		if (pNext != NULL){							// If the message is complete, read it.
			pNext += 4;
			*pNext = '\0';
			sscanf(pstr, "$GPRMC,%2d%2d%2d.%d,%1c,%f,%1c,%f,%1c,%f,%f,%2d%2d%2d,", &gmt.tm_hour, &gmt.tm_min, &gmt.tm_sec,
					&frac, active, &ftmp1, ctmp2, &ftmp3, ctmp4, &ftmp5, &ftmp6, &gmt.tm_mday, &gmt.tm_mon, &gmt.tm_year);

			if (active[0] == 'A'){					// this is an active message

				gmt.tm_mon -= 1;					// Convert to tm struct format with months: 0 to 11
				gmt.tm_year += 100;					// Convert to tm struct format with year since 1900
													// But actual conversion is from Unix 1/1/1970
				*gmt0Seconds =  timegm(&gmt);		// Local time in seconds because GPS returns the timezone
													// time in gmt and timegm() makes no timezone correction.
				if (*gmt0Seconds == -1){
					s.badTimeConversion = true;
				}
				s.lostGPSCount = 0;
			}
			else {
				s.badGPRMCmsg = true;
				return false;
			}
		}
		else {
			s.badGPRMCmsg = true;
			return false;
		}
	}
	else {
		s.noGPRMCmsg = true;
		return false;
	}
	return true;
}


/**
 * Saves timestamps of local time and GPS time to tcp->gmtTime_file.
 *
 * @param[in] gmt0Seconds The GPS timestamp.
 * @param[in] gmtSeconds The local time timestamp
 * @param[in] tcp Structure containing the gmtTime_file filename.
 */
int saveTimestamps(int gmt0Seconds, int gmtSeconds, int tv_usec, timeCheckParams *tcp){

	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	int sfd = open(tcp->gmtTime_file, O_CREAT | O_WRONLY, mode);
	if (sfd == -1){
		sprintf(tcp->strbuf, "saveTimestamps() Could not create/open gmtTime file %s\n", tcp->gmtTime_file);
		writeToLog(tcp->strbuf, "saveTimestamps()");
		return -1;
	}

	char sbuf[100];
	memset(sbuf, 0, 100);
	sprintf(sbuf, "%d %d %d\n", gmt0Seconds, gmtSeconds, tv_usec);

	int rv = write(sfd, sbuf, 100);
	close(sfd);

	if (rv == -1){
		sprintf(tcp->strbuf, "saveTimestamps() write to gmtTime file failed\n");
		writeToLog(tcp->strbuf, "saveTimestamps()");
	}
	return 0;
}


/**
 * Converts a $GPRMC message s.ptimestr from the serial port to
 * seconds and passes the result along with local time in seconds
 * to read_save();
 *
 * @param[in] captureTime The time in microseconds into the current
 * second at wich the $GPRMC message was captured and converted.
 *
 * @param[in] tcp A struct pointer used to pass thread data.
 */
void read_save(int captureTime, timeCheckParams *tcp){
	time_t gmt0Seconds = 0;

	if (getUTCfromGPSmessages(s.ptimestr, tcp, &gmt0Seconds) == false){
		s.missMsg += 1;

		if (s.missMsg >= MAX_NOT_READY){
			sprintf(tcp->strbuf, "saveGPSTime(): No GPRMC message was recieved from the serial port in 60 seconds\n");
			writeToLog(tcp->strbuf, "saveGPSTime()");
			s.missMsg = 0;
		}
	}
	else if (s.noGPRMCmsg == false && s.badGPRMCmsg == false && s.badTimeConversion == false){

		if (captureTime < 500){

#ifdef DEBUG
			printf("read_save()    gmt0Seconds: %ld captureTime: %d\n", gmt0Seconds, captureTime);
#endif
			saveTimestamps((int)gmt0Seconds, s.gmtSeconds, captureTime, tcp);  	// timestamps from the previous second

			s.missMsg = 0;
		}
		else {
			s.missMsg += 1;
		}
	}

	return;
}


/**
 * Reads the GPS serial port once per second and saves
 * the GPS time, gmt0Seconds, along with the local time,
 * gmtSeconds, at which the GPS time was read, to a file
 * that can be read by makeSerialTimeQuery().
 *
 * Because read_save() is called within 500 usecs
 * after the rollover of the second, that has the consequence
 * that the data saved by this function is one second
 * old by the time it is read by makeSerialTimeQuery().
 *
 * However, the timestamps passed to saveTimestamps()
 * allways correspond to the same second. So the only
 * effect is to delay the reporting of a possible time
 * error by one second.
 *
 * @param[in, out] tcp A struct pointer used to pass
 * thread data.
 */
void saveGPSTime(timeCheckParams *tcp){

	int rfd = open(tcp->serialPort, O_RDONLY | O_NOCTTY);
	if (rfd == -1){
		sprintf(tcp->strbuf, "saveGPSTime() Unable to open %s\n", tcp->serialPort);
		writeToLog(tcp->strbuf, "saveGPSTime()");
		return;
	}

	struct timeval tv1;
	int captureTime, nRead, timeToWait;

	for (int i = 0; i < 1000; i++){						// In case it has backed up, empty the serial port buffer.
		nRead = read(rfd, s.msgbuf, 9950);
		if (nRead < 500){
			break;
		}
	}

    while (true){
    	s.noGPRMCmsg = false;
    	s.bufferIsEmpty = false;
    	s.badGPRMCmsg = false;

		gettimeofday(&tv1, NULL);

		s.gmtSeconds = (int)tv1.tv_sec;

		int timeToStart = 1000000 - tv1.tv_usec + 100;  // Start 100 usec after rollover of second
		usleep(timeToStart);

		memset(s.msgbuf, 0, 10000 * sizeof(char));

		int readNum = 9950;

		nRead = read(rfd, s.msgbuf, readNum);

#ifdef DEBUG
		printf("\nsaveGPSTime() Number of serial port lines read: %d\n", nRead);
#endif
		s.ptimestr = strstr(s.msgbuf, "$GPRMC");

		if (s.ptimestr != NULL &&
				strlen(s.ptimestr) < 150){				// $GPRMC and $GPVTG should be the last two messages. If
#ifdef DEBUG														// not then messages have been delayed and are not reliable.
			printf("\ns.ptimestr: %s\n", s.ptimestr);
			printf("saveGPSTime() s.gmtSeconds: %d usec: %d\n", s.gmtSeconds, (int)tv1.tv_usec);
#endif
			gettimeofday(&tv1, NULL);
			captureTime = (int)tv1.tv_usec;

			read_save(captureTime, tcp);
		}

    	if (nRead == -1){
    		s.bufferIsEmpty = true;
    	}

		gettimeofday(&tv1, NULL);
		int readEnd = tv1.tv_usec;
		timeToWait = 1000000 - readEnd - 10000;

		usleep(timeToWait);								// Sleep until 10 msec before end of second.

   }

	close(rfd);
}

/**
 * Gets the time from a serial port connected to a GPS
 * receiver or equivalent and returns the difference in
 * seconds from the local time to the true GPS time.
 *
 * If a difference is detected, the error is verified
 * by looking for an identical time difference from the
 * next ten time difference checks. If the time difference
 * repeats at least a total of eight times, the result is
 * valid and is returned in g.serialTimeError. Otherwise
 * the difference was an error and g.serialTimeError
 * remains 0.
 *
 * @returns 0 or -1 on a system error.
 */
int makeSerialTimeQuery(timeCheckParams *tcp){

	int rv = 0;
	int sfd;
	char sbuf[100];
	time_t gmt0Seconds, gmtSeconds;
	struct stat statbuf;

	usleep(5000);

	int idx = s.activeCount % VERIFY_NUM;

	if (idx == 0){
		memset(s.timeDiff, 0, VERIFY_NUM * sizeof(int));
		memset(s.diffCount, 0, VERIFY_NUM * sizeof(int));
	}

	memset(sbuf, 0, 100);

	if (stat(f.gmtTime_file, &statbuf) == 0){	// If file exists
		sfd = open(f.gmtTime_file, O_RDONLY);

		if (s.notReadyCount >= MAX_NOT_READY){
			sprintf(g.logbuf, "makeSerialTimeQuery(): Serial port GPS time data has resumed\n");
			writeToLog(g.logbuf, "makeSerialTimeQuery()");
		}

		s.notReadyCount = 0;
		s.activeCount += 1;
	}
	else {
		s.notReadyCount += 1;

		if (s.notReadyCount >= MAX_NOT_READY){

			if (s.notReadyCount == MAX_NOT_READY){
				sprintf(g.logbuf, "makeSerialTimeQuery(): Serial port GPS time data has stopped\n");
				writeToLog(g.logbuf, "makeSerialTimeQuery()");
			}
		}
		return 0;
	}

	int sz = read(sfd, sbuf, 100);
	if (sz == 0){
		close(sfd);
		remove(f.gmtTime_file);
		return 0;
	}
	if (sz == -1){
		close(sfd);
		remove(f.gmtTime_file);
		return 0;
	}

	close(sfd);
	remove(f.gmtTime_file);

	int captureTime;

	sscanf(sbuf, "%ld %ld %d\n", &gmt0Seconds, &gmtSeconds, &captureTime);

	s.timeDiff[idx] = (int)(gmt0Seconds - gmtSeconds);

	int maxDiffCount = 0, timeDiff = 0;

	if (idx == VERIFY_NUM - 1){

		for (int i = 0; i < VERIFY_NUM; i++){						// Get a consensis of the time error for VERIFY_NUM timeDiff values
			for (int j = 0; j < VERIFY_NUM; j++){
				if ((s.timeDiff[i] != 0) && (s.timeDiff[i] == s.timeDiff[j])){
					s.diffCount[i] += 1;
				}
			}
		}

		for (int i = 0; i < VERIFY_NUM; i++){
			if (s.diffCount[i] > maxDiffCount){
				maxDiffCount = s.diffCount[i];
				timeDiff = s.timeDiff[i];
			}
		}

		if ((maxDiffCount >= (VERIFY_NUM - 2)) && (g.serialTimeError == 0)){									// At least four out of five must agree on the timeDiff.
			sprintf(g.logbuf, "makeSerialTimeQuery() Time error: %d seconds. The error will be corrected within 1 minute.\n", timeDiff);
			writeToLog(g.logbuf, "makeSerialTimeQuery()");

			g.serialTimeError = timeDiff;
		}
	}
#ifdef DEBUG
	printf("makeSerialTimeQuery() s.timeDiff[%d]: %d timeDiff %d maxDiffCount: %d\n", idx, s.timeDiff[idx], timeDiff, maxDiffCount);
#endif
	return rv;
}

/**
 * Allocates memory and initializes a thread that will be used by
 * makeSerialimeQuery() to query the serial port. Thread must be
 * released and memory deleted by calling freeSerialThread().
 *
 * @param[out] tcp Struct pointer for passing data.
 *
 * @returns 0 on success or -1 on error.
 */
int allocInitializeSerialThread(timeCheckParams *tcp){
	memset(&s, 0, sizeof(struct serialLocalVars));

	int buflen = strlen(g.serialPort);
	s.serialPort = new char[buflen + 1];
	strcpy(s.serialPort, g.serialPort);

	s.threadIsBusy[0] = false;

	tcp->tid = s.tid;
	tcp->strbuf = new char[STRBUF_SZ];
	tcp->threadIsBusy = s.threadIsBusy;
	tcp->serialPort = s.serialPort;
	tcp->gmtTime_file = f.gmtTime_file;

	tcp->rv = 0;
	tcp->doReadSerial = false;

	int rv = pthread_attr_init(&(tcp->attr));
	if (rv != 0) {
		sprintf(g.logbuf, "Can't init pthread_attr_t object: %s\n", strerror(errno));
		writeToLog(g.logbuf, "allocInitializeSerialThread()");
		return -1;
	}

	rv = pthread_attr_setstacksize(&(tcp->attr), PTHREAD_STACK_REQUIRED);
	if (rv != 0){
		sprintf(g.logbuf, "Can't set pthread_attr_setstacksize(): %s\n", strerror(errno));
		writeToLog(g.logbuf, "allocInitializeSerialThread()");
		return -1;
	}

	rv = pthread_attr_setdetachstate(&(tcp->attr), PTHREAD_CREATE_DETACHED);
	if (rv != 0){
		sprintf(g.logbuf, "Can't set pthread_attr_t object state: %s\n", strerror(errno));
		writeToLog(g.logbuf, "allocInitializeSerialThread()");
		return -1;
	}

	return 0;
}

/**
 * Releases thread and deletes memory used by makeSerialTimeQuery();
 *
 * @param[in] tcp The struct pointer that was used for passing data.
 */
void freeSerialThread(timeCheckParams *tcp){
	pthread_attr_destroy(&(tcp->attr));
	delete[] tcp->strbuf;
	if (tcp->serialPort != NULL){
		delete[] tcp->serialPort;
		tcp->serialPort = NULL;
	}
}

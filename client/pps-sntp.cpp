/**
 * @file pps-sntp.cpp
 * @brief This file contains functions and structures for accessing time updates via the NIST UDP time service.
 */

/*
 * Copyright (C) 2016-2020  Raymond S. Connell
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
#define ADDR_LEN 17
extern struct G g;
extern struct ppsFiles f;

/**
 * Local file-scope shared variables.
 */
static struct nistLocalVars {
	bool hasStarted;
	int serverTimeDiff[MAX_SERVERS];
	bool threadIsBusy[MAX_SERVERS];
	pthread_t tid[MAX_SERVERS];
	int numServers;
	int timeCheckEnable;
	bool allServersQueried;
	bool gotError;
} n;

void copyToLog(char *logbuf, const char* msg){
	char timestamp[100];
	time_t t = time(NULL);
	struct tm *tmp = localtime(&t);
	strftime(timestamp, STRBUF_SZ-1, "%F %H:%M:%S ", tmp);
	strcat(logbuf, timestamp);
	strcat(logbuf, msg);
}

/**
 * Gets the time correction relative to the local clock
 * setting in whole seconds provided by a NIST time server
 * using the UDP protocol and queried by this shell command:
 *
 * 	$ udp-time-client -u[n]
 *
 * where [n] is the id described below. The timeDiff is assumed
 * to be correct only if it is received within the current
 * second. Because of internet delays and other errors UDP
 * time servers do not always respond within a second. Consequently
 * this function is used to query four timer serverss and a consensis
 * of the possible error is obtained from them. The consensis error
 * is added to the whole seconds of the system clock.
 *
 * @param[in] id An identifer recognized by udp-time-client
 * to select a server and used for constructing a filename for
 * server messages. id is in the range 1 to 4.
 *
 * @param[in] strbuf A buffer to hold server messages.
 * @param[in] logbuf A buffer to hold messages for the error log.
 * @param[out] timeDiff The time correction to be made.
 * @param[in] nistTime_file Filename to be used to transfer data
 * from the shell to this program. Contains output from the
 * udp-time-client shell command.
 *
 * @returns 0 or -1 on error.
 */
int getNISTTime(int id, char *strbuf, char *logbuf, time_t *timeDiff, char *nistTime_file){
	struct timeval startTime, returnTime;
	struct stat stat_buf;
	char num[2];
	char buf[500];

	sprintf(num, "%d", id);							// "/run/shm/nist_outn" with n the id val.

	char *cmd = buf;								// Construct a command string:
	sprintf(cmd, "udp-time-client -u%d ", id);		//    "udp-time-client -uxx > /run/shm/nist_outn"
	strcat(cmd, " > ");
	strcat(cmd, nistTime_file);
	strcat(cmd, num);

	gettimeofday(&startTime, NULL);
	int rv = sysCommand(cmd);						// Issue the command:
	if (rv == -1){
		return -1;
	}
	gettimeofday(&returnTime, NULL);				// sysCommand() will block until udp-time-client returns or times out.

	if (returnTime.tv_sec - startTime.tv_sec > 0){	// Took more than 1 second
													// Reusing buf for constructing error message on exit.
//		sprintf(buf, "Skipped server %d. Unable to retrieve in the same second.\n", id);
//		copyToLog(logbuf, buf);
		return -1;
	}

	char *fname = buf;
	strcpy(fname, nistTime_file);
	strcat(fname, num);
													// Open the file: "/run/shm/nist_out[n]"
	int fd = open((const char *)fname, O_RDONLY);
	if (fd == -1){
		strcpy(buf, "ERROR: could not open \"");	// Reusing buf for constructing error message on exit.
		strcat(buf, fname);
		strcat(buf, "\"\n");
		copyToLog(logbuf, buf);
		return -1;
	}

	fstat(fd, &stat_buf);
	int sz = stat_buf.st_size;						// Get the size of file.

	if (sz < NIST_MSG_SZ){
		rv = read(fd, strbuf, sz);					// Read the file.
		if (rv == -1){
			strcpy(buf, "ERROR: reading \"");		// Reusing buf for constructing error message on exit.
			strcat(buf, nistTime_file);
			strcat(buf, "\" was interrupted.\n");
			copyToLog(logbuf, buf);
			return -1;
		}
		strbuf[sz] = '\0';
		close(fd);
		remove(fname);
	}
	else {
		writeFileMsgToLogbuf(fname, logbuf);
		close(fd);
		return -1;
	}

	char *pNum = strpbrk(strbuf, "-0123456789");
	time_t delta;
	if (pNum == strbuf){							// strbuf contains the time difference in whole seconds
		sscanf(strbuf, "%ld\n", &delta);			// between UDP server n and the local clock time.
		*timeDiff = -delta;
//		if (delta != 0){
//			sprintf(strbuf, "getNISTTime(): Says that time is behind by %ld secs\n", *timeDiff);
//			copyToLog(logbuf, strbuf);
//		}
		return 0;
	}
	else {											// strbuf contains an error message
		copyToLog(logbuf, strbuf);
		return -1;
	}
}

/**
 * Requests a date/time from a NIST time server in a detached thread
 * that exits after filling the timeCheckParams struct, tcp, with the
 * requested information and any error info.
 *
 * @param[in,out] tcp struct pointer for passing data.
 */
void doTimeCheck(timeCheckParams *tcp){

	int i = tcp->serverIndex;
	char *strbuf = tcp->strbuf + i * STRBUF_SZ;
	char *logbuf = tcp->logbuf + i * LOGBUF_SZ;

	logbuf[0] = '\0';								// Clear the logbuf.

	tcp->threadIsBusy[i] = true;

	time_t timeDiff;
	int r = getNISTTime(i+1, strbuf, logbuf, &timeDiff, tcp->nistTime_file);
	if (r == -1){
		tcp->serverTimeDiff[i] = 1000000;			// Marker for no time returned
	}
	else {
		tcp->serverTimeDiff[i] = timeDiff;
	}

	tcp->threadIsBusy[i] = false;
}

/**
 * Takes a consensus of the time error between local time and
 * the time reported by NIST servers and reports the error as
 * G.consensusTimeError.
 *
 * @returns The number of NIST servers reporting.
 */
int getTimeConsensusAndCount(void){
	int timeDiff[MAX_SERVERS];
	int count[MAX_SERVERS];

	int nServersReporting = 0;

	memset(timeDiff, 0, MAX_SERVERS * sizeof(int));
	memset(count, 0, MAX_SERVERS * sizeof(int));

	for (int j = 0; j < n.numServers; j++){				// Construct a distribution of diffs
		if (n.serverTimeDiff[j] != 1000000){			// Skip a server not returning a time
			int k;
			for (k = 0; k < n.numServers; k++){
				if (n.serverTimeDiff[j] == timeDiff[k]){	// Matched a value
					count[k] += 1;
					break;
				}
			}
			if (k == n.numServers){						// No value match
				for (int m = 0; m < n.numServers; m++){
					if (count[m] == 0){
						timeDiff[m] = n.serverTimeDiff[j];	// Create a new timeDiff value
						count[m] = 1;					// Set its count to 1
						break;
					}
				}
			}
			nServersReporting += 1;
		}
	}

	int maxHits = 0;
	int maxHitsIndex = 0;
														// Get the timeDiff having the max number of hits
	for (int j = 0; j < n.numServers; j++){
		if (count[j] > maxHits){
			maxHits = count[j];
			maxHitsIndex = j;
		}
	}

	g.consensusTimeError = timeDiff[maxHitsIndex];

	if (g.consensusTimeError != 0){
		if (maxHits >= 3 && n.gotError == false){

			sprintf(g.msgbuf, "getTimeConsensusAndCount(): Time is behind by %d seconds.\n", timeDiff[maxHitsIndex]);
			bufferStatusMsg(g.msgbuf);
			n.gotError = true;

		}
		else if (n.gotError == true){

			sprintf(g.msgbuf, "getTimeConsensusAndCount(): Waiting for controller to become active to correct the time error.\n");
			bufferStatusMsg(g.msgbuf);
		}
		else {
			sprintf(g.msgbuf, "getTimeConsensusAndCount(): Number of servers responding: %d\n", nServersReporting);
			bufferStatusMsg(g.msgbuf);
		}
	}
	else {
		n.gotError = false;

		sprintf(g.msgbuf, "getTimeConsensusAndCount(): Number of servers responding: %d\n", nServersReporting);
		bufferStatusMsg(g.msgbuf);
	}

	for (int i = 0; i < MAX_SERVERS; i++){
		n.serverTimeDiff[i] = 1000000;
	}
	return nServersReporting;
}

/**
 * Updates the PPS-Client log with any errors reported by threads
 * querying NIST time servers.
 *
 * @param[out] buf The message buffer shared by the threads.
 * @param[in] numServers The number of NIST servers.
 */
void updateLog(char *buf, int numServers){

	char *logbuf;

	for (int i = 0; i < numServers; i++){
		logbuf = buf + i * LOGBUF_SZ;

		if (strlen(logbuf) > 0){
			writeToLogNoTimestamp(logbuf);
		}
	}
}

/**
 * At an interval defined by CHECK_TIME, queries a list of NIST servers
 * for date/time using detached threads so that delays in server responses
 * do not affect the operation of the waitForPPS() loop.
 *
 * Called each second.
 *
 * @param[in,out] tcp Struct pointer for passing data.
 */

void makeNISTTimeQuery(timeCheckParams *tcp){
	int rv;

	if (n.allServersQueried){
		if (g.queryWait == false){
			n.allServersQueried = false;

			getTimeConsensusAndCount();
			updateLog(tcp->logbuf, n.numServers);
		}
		if (g.queryWait == true){							// This delays getting the time consensus by one second
			g.queryWait = false;							// to allow the last thread enough time to report back.
		}
	}

	if (n.hasStarted == false && 							// Start a time check against the list of NIST servers
			(g.activeCount == 1 || g.activeCount % CHECK_TIME == 0)){
		n.hasStarted = true;

		n.numServers = MAX_SERVERS;

		for (int i = 0; i < MAX_SERVERS; i++){
			n.serverTimeDiff[i] = 1000000;
			n.threadIsBusy[i] = false;
		}

		n.timeCheckEnable = n.numServers;					// Initialize the server list

		bufferStatusMsg("Starting a time check.\n");
	}

	if (n.timeCheckEnable > 0){								// As long as is true dispatch a thread to query a server
															// starting at the highest thread index.
		tcp->serverIndex = n.timeCheckEnable - 1;

		n.timeCheckEnable -= 1;								// In next second, dispatch thread with next lower index

		int idx = tcp->serverIndex;

		if (idx == 0){
			n.allServersQueried = true;
			n.hasStarted = false;
			g.queryWait = true;
		}

		if (n.threadIsBusy[idx]){
			sprintf(g.msgbuf, "Server %d is busy.\n", idx);
			bufferStatusMsg(g.msgbuf);
		}
		else {
			sprintf(g.msgbuf, "Requesting time from Server %d\n", idx);
			bufferStatusMsg(g.msgbuf);

			rv = pthread_create(&((tcp->tid)[idx]), &(tcp->attr), (void* (*)(void*))&doTimeCheck, tcp);
			if (rv != 0){
				sprintf(g.logbuf, "Can't create thread : %s\n", strerror(errno));
				writeToLog(g.logbuf, "makeNISTTimeQuery()");
			}
		}
	}
}

/**
 * Allocates memory and initializes threads that will be used by
 * makeNISTTimeQuery() to query NIST time servers. Thread must be
 * released and memory deleted by calling freeNISTThreads().
 *
 * @param[out] tcp Struct pointer for passing data.
 *
 * @returns 0 on success or -1 on error.
 */
int allocInitializeNISTThreads(timeCheckParams *tcp){
	memset(&n, 0, sizeof(struct nistLocalVars));

	tcp->tid = n.tid;
	tcp->serverIndex = 0;
	tcp->serverTimeDiff = n.serverTimeDiff;
	tcp->strbuf = new char[STRBUF_SZ * MAX_SERVERS];
	tcp->logbuf = new char[LOGBUF_SZ * MAX_SERVERS];
	tcp->threadIsBusy = n.threadIsBusy;
	tcp->buf = NULL;
	tcp->nistTime_file = f.nistTime_file;

	int rv = pthread_attr_init(&(tcp->attr));
	if (rv != 0) {
		sprintf(g.logbuf, "Can't init pthread_attr_t object: %s\n", strerror(errno));
		writeToLog(g.logbuf, "allocInitializeNISTThreads()");
		return -1;
	}

	rv = pthread_attr_setstacksize(&(tcp->attr), PTHREAD_STACK_REQUIRED);
	if (rv != 0){
		sprintf(g.logbuf, "Can't set pthread_attr_setstacksize(): %s\n", strerror(errno));
		writeToLog(g.logbuf, "allocInitializeNISTThreads()");
		return -1;
	}

	rv = pthread_attr_setdetachstate(&(tcp->attr), PTHREAD_CREATE_DETACHED);
	if (rv != 0){
		sprintf(g.logbuf, "Can't set pthread_attr_t object state: %s\n", strerror(errno));
		writeToLog(g.logbuf, "allocInitializeNISTThreads()");
		return -1;
	}

	return 0;
}

/**
 * Releases threads and deletes memory used by makeNISTTimeQuery();
 *
 * @param[in] tcp The struct pointer that was used for passing data.
 */
void freeNISTThreads(timeCheckParams *tcp){
	pthread_attr_destroy(&(tcp->attr));
	delete[] tcp->strbuf;
	delete[] tcp->logbuf;
	if (tcp->buf != NULL){
		delete[] tcp->buf;
	}
}

/**
 * @file pps-files.cpp
 * @brief This file contains functions and structures for saving and loading files intended for PPS-Client status monitoring and analysis.
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

extern struct G g;

const char *config_file = "/etc/pps-client.conf";								//!< The PPS-Client configuration file.
const char *last_distrib_file = "/pps-error-distrib";							//!< Stores the completed distribution of offset corrections.
const char *distrib_file = "/pps-error-distrib-forming";						//!< Stores a forming distribution of offset corrections.
const char *last_jitter_distrib_file = "/pps-jitter-distrib";					//!< Stores the completed distribution of offset corrections.
const char *jitter_distrib_file = "/pps-jitter-distrib-forming";				//!< Stores a forming distribution of offset corrections.
const char *log_file = "/pps-client.log";										//!< Stores activity and errors.
const char *old_log_file = "/pps-client.old.log";								//!< Stores activity and errors.
const char *pidFilename = "/pps-client.pid";									//!< Stores the PID of PPS-Client.
const char *assert_file = "/pps-assert";										//!< The timestamps of the time corrections each second
const char *displayParams_file = "/pps-display-params";							//!< Temporary file storing params for the status display
const char *arrayData_file = "/pps-save-data";									//!< Stores a request sent to the PPS-Client daemon.
const char *pps_msg_file = "/pps-msg";
const char *linuxVersion_file = "/linuxVersion";
const char *gmtTime_file = "/gmtTime";
const char *nistTime_file = "/nist_out";
const char *integral_state_file = "/.pps-last-state";
const char *home_file = "/pps";
const char *cpuinfo_file = "/cpuinfo";

const char *space = " ";
const char *num = "0123456789.";

extern const char *version;

static struct stat configFileStat;
static time_t modifyTime = 0;
static int lastJitterFileno = 0;
static int lastErrorFileno = 0;
static struct timespec offset_assert = {0, 0};

bool writeJitterDistrib = false;
bool writeErrorDistrib = false;

/**
 * PPS-Client internal files.
 */
struct ppsFiles f; 														//!< PPS-Client internal files.

/**
 * Recognized configuration strings for the PPS-Client
 * configuration file. These strings mirror the configuration
 * file keys #defines in pps-client one-to-one and are
 * identical to the strings in the config file. Usually the
 * #define is the the same word in upper case but not
 * always
 *
 */
const char *valid_config[] = {
		"error-distrib",
		"alert-pps-lost",
		"jitter-distrib",
		"exit-lost-pps",
		"pps-gpio",
		"output-gpio",
		"intrpt-gpio",
		"nist",
		"serial",
		"serialPort",
		"execdir",
		"servicedir",
		"configdir",
		"docdir",
		"rundir",
		"shmdir",
		"tstdir",
		"logdir",
		"zeroOffset",
		"moduledir",
		"ppsdevice",
		"ppsphase",
		"procdir",
		"segregate",
		"ntpcheck",
		"ntpServer"
};

/**
 * List operator that inserts integer values
 * into an ordered list.
 *
 * @param[in] val The integer value to be
 * inserted into the list.
 *
 * @returns The insertion index
 */
int List::binaryInsert(int val){
	if (count == size){
		return size;
	}

	count += 1;

    int high = ln - 1;
    if (ln == 0 || val > lst[high].val){
    	insert(ln, val);
    	return ln;
    }
    if (ln == 1){
    	if (val < lst[0].val){
    		insert(0, val);
    		return 0;
    	}
        if (val > lst[0].val){
            insert(ln, val);
            return ln;
        }
        lst[0].nVals += 1;
		return 0;
    }
    int low = -1;
    int j = 0;
    while ((high - low) > 1){
    	j = (high + low) / 2;
    	if (val <= lst[j].val){
    		high = j;
    	}
    	else {
    		low = j;
    	}
    }
    if (lst[high].val == val){
    	lst[high].nVals += 1;
    	return high;
    }
    insert(high, val);
    return high;
}


/**
 * Returns the average of all values in lst
 * that are less than maxVal. If there
 * are none the returns the lowest value
 * in lst.
 *
 * @param[in] maxVal The comparison value.
 *
 * @returns The average below maxVal
 */
double List::averageBelow(int maxVal){

	int sum = 0;
	int n = 0;
	double average;

	if (ln == 0){
		return 0;
	}

	for (int i = 0; i < ln; i++){
		sum += lst[i].nVals * lst[i].val;
		n += lst[i].nVals;

		if (i < ln-1){
			if (lst[i+1].val - lst[i].val >= maxVal){
				break;
			}
		}
	}

	average = (double)sum / (double)n;

	return average;
}

/**
 * system() function with error handling.
 */
int sysCommand(const char *cmd){
	int rv = system(cmd);
	if (rv == -1 || WIFEXITED(rv) == false){
		sprintf(g.logbuf, "System command failed: %s\n", cmd);
		writeToLog(g.logbuf, "sysCommand()");
		return -1;
	}
	return 0;
}

int checkNTP(const char *cmd) {

	FILE *fp;
	char res[1035];
	char *ret;
	
	fp = popen(cmd, "r");
	
	if (fp == NULL) {
		return -1;
	}
	
	while (fgets(res, sizeof(res), fp) != NULL) {
		
		printf("%s", res);
		
		ret = strstr(res, "no server suitable");
		if (ret) {
			printf("Failed to connect to an NTP server!\n");
			pclose(fp);
			return -2;
		}

		ret = strstr(res, "offset");
		printf("%s\n", ret);
		if (ret) {
			pclose(fp);
			return 0;
		}
		
	}
	
	pclose(fp);
	
	printf("Unknown result from NTP check\n");
	return -3;
  
}

/**
 * Retrieves the string from the config file assigned
 * to the valid_config string with value key.
 *
 * @param[in] key The config key corresponding to a string in
 * the valid_config[] array above.
 *
 * @returns The string assigned to the key.
 */
char *getString(int key){
	char *str;
	int len;
	int i = round(log2(key));

	if (g.config_select & key){

		str = g.configVals[i];

		len = strlen(str);

		while (str[len-1] == ' '){
			str[len-1] = '\0';
			len -= 1;
		}

		return str;
	}
	return NULL;
}

/**
 * Tests configuration strings from pps-client.conf
 * for the specified string. To avoid searching the config
 * file more than once, the config key is a bit position in
 * G.config_select that is set or not set if the corresponding
 * config string is found in the config file when it is first
 * read. In that case an array G.configVals[], constructed when
 * the config file was read, will contain the string from the
 * config file that followed the valid_config. That string
 * will be G.configVals[log2(key)].
 *
 * @param[in] key The config key corresponding to a string in
 * the valid_config[] array above.
 *
 * @param[in] string The string that must be matched by the string
 * that follows the config string name from the valid_config[] array.
 *
 * @returns "true" if the string in the config file matches arg
 * "string", else "false".
 */
bool hasString(int key, const char *string){
	int i = round(log2(key));
	
	if (g.config_select & key){
		char *val = strstr(g.configVals[i], string);
		if (val != NULL){
			return true;
		}
	}
	return false;
}

/**
 * Tests configuration strings from pps-client.conf
 * for the "enable" keyword.
 *
 * @param[in] key The config key corresponding to a string in
 * the valid_config[] array above.
 *
 * @returns "true" if the "enable" keyword is detected,
 * else "false".
 */
bool isEnabled(int key){
	return hasString(key, "enable");
}

/**
 * Tests configuration strings from pps-client.conf
 * for the "disable" keyword.
 *
 * @param[in] key The config key corresponding to a string in
 * the valid_config[] array above.
 *
 * @returns "true" if the "disable" keyword is detected,
 * else false.
 */
bool isDisabled(int key){
	return hasString(key, "disable");
}

/**
 * Data associations for PPS-Client command line save
 * data requests with the -s flag.
 */
struct saveFileData arrayData[] = {
	{"rawError", g.rawErrorDistrib, "/var/local/pps-raw-error-distrib", ERROR_DISTRIB_LEN, 2, RAW_ERROR_ZERO},
	{"frequency-vars", NULL, "/var/local/pps-frequency-vars", 0, 3, 0},
	{"pps-offsets", NULL, "/var/local/pps-offsets", 0, 4, 0}
};

/**
 * Constructs an error message.
 */
void couldNotOpenMsgTo(char *logbuf, const char *filename, const char* location){
	strcpy(logbuf, "ERROR: could not open \"");
	strcat(logbuf, filename);
	strcat(logbuf, "\": ");
	strcat(logbuf, strerror(errno));
	strcat(logbuf, " ");
	strcat(logbuf, location);
	strcat(logbuf, "\n");
}

/**
 * Constructs an error message.
 */
void errorReadingMsgTo(char *logbuf, const char *filename){
	strcpy(logbuf, "ERROR: reading \"");
	strcat(logbuf, filename);
	strcat(logbuf, "\" was interrupted: ");
	strcat(logbuf, strerror(errno));
	strcat(logbuf, "\n");
}

/**
 * Appends logbuf to the log file.
 *
 * @param[in,out] logbuf Pointer to the log buffer.
 */
void writeToLogNoTimestamp(char *logbuf){
	struct stat info;

	bufferStatusMsg(logbuf);

	stat(f.log_file, &info);
	if (info.st_size > 100000){			// Prevent unbounded log file growth
		remove(f.old_log_file);
		rename(f.log_file, f.old_log_file);
	}

	mode_t mode = S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH;
	int fd = open(f.log_file, O_CREAT | O_WRONLY | O_APPEND, mode);
	if (fd == -1){
		couldNotOpenMsgTo(logbuf, f.log_file, "writeToLogNoTimestamp()");
		printf("%s", logbuf);
		return;
	}

	int rv = write(fd, logbuf, strlen(logbuf));
	if (rv == -1){
		;
	}
	close(fd);
}


/**
 * Appends logbuf to the log file with a timestamp.
 *
 * @param[in,out] logbuf Pointer to the log buffer.
 */
void writeToLog(char *logbuf, const char *location){
	struct stat info;

	bufferStatusMsg(logbuf);

	stat(f.log_file, &info);
	if (info.st_size > 100000){			// Prevent unbounded log file growth
		remove(f.old_log_file);
		rename(f.log_file, f.old_log_file);
	}

	mode_t mode = S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH;
	int fd = open(f.log_file, O_CREAT | O_WRONLY | O_APPEND, mode);
	if (fd == -1){
		couldNotOpenMsgTo(logbuf, f.log_file, location);
		printf("%s", logbuf);
		return;
	}

	time_t t = time(NULL);
	struct tm *tmp = localtime(&t);
	strftime(g.strbuf, STRBUF_SZ-1, "%F %H:%M:%S ", tmp);
	int rv = write(fd, g.strbuf, strlen(g.strbuf));
	if (rv == -1){
		;
	}

	rv = write(fd, logbuf, strlen(logbuf));
	if (rv == -1){
		;
	}
	close(fd);
}

/**
 * Concatenates msg to a message buffer, savebuf, which is
 * saved to a tmpfs memory file by writeStatusStrings() each
 * second. These messages can be read and displayed to the
 * command line by showStatusEachSecond().
 *
 * @param[in] msg Pointer to the message to be
 * concatenated.
 */
void bufferStatusMsg(const char *msg){

	if (g.isVerbose){
		fprintf(stdout, "%s", msg);
	}

	int msglen = strlen(g.savebuf) + 10;
	int parmslen = strlen(msg);

	if (msglen + parmslen > MSGBUF_SZ){
		return;
	}

	strcat(g.savebuf, msg);

	return;
}

/**
 * Writes status strings accumulated in a message buffer,
 * G.savebuf, from bufferStateParams() and other sources
 * to a tmpfs memory file, displayParams_file, once each
 * second. This file can be displayed in real time by
 * invoking the PPS-Client program with the -v command
 * line flag while the PPS-Client daemon is running.
 *
 * @returns 0 on success, else -1 on error.
 */
int writeStatusStrings(void){

	int fSize = strlen(g.savebuf);

	remove(f.displayParams_file);
	int fd = open_logerr(f.displayParams_file, O_CREAT | O_WRONLY, "writeStatusStrings() 1");
	if (fd == -1){
		return -1;
	}
	int rv = write(fd, g.savebuf, fSize);
	close(fd);
	if (rv == -1){
		sprintf(g.logbuf, "writeStatusStrings() Could not write to %s. Error: %s\n", f.displayParams_file, strerror(errno));
		writeToLog(g.logbuf, "writeStatusStrings() 2");
		return -1;
	}

	g.savebuf[0] = '\0';
	return 0;
}

/**
 * Reads a file with error logging.
 *
 * @param[in] fd The file descriptor.
 * @param[out] buf The buffer to hold the file data.
 * @param[in] sz The number of bytes to read.
 * @param[in] filename The filename of the file being read.
 *
 * @returns The number of bytes read.
 */
int read_logerr(int fd, char *buf, int sz, const char *filename){
	int rv = read(fd, buf, sz);
	if (rv == -1){
		errorReadingMsgTo(g.logbuf, filename);
		writeToLog(g.logbuf, "read_logerr()");
		return rv;
	}
	return rv;
}

/**
 * Opens a file with error logging and sets standard
 * file permissions for O_CREAT.
 *
 * @param[in] filename The file to open.
 * @param[in] flags The file open flags.
 * @param[in] location The name of the calling function.
 *
 * @returns The file descriptor.
 */
int open_logerr(const char* filename, int flags, const char *location){
	int mode = S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH;
	int fd;
	if ((flags & O_CREAT) == O_CREAT){
		fd = open(filename, flags, mode);
	}
	else {
		fd = open(filename, flags);
	}
	if (fd == -1){
		couldNotOpenMsgTo(g.logbuf, filename, location);
		writeToLog(g.logbuf, location);
		return -1;
	}
	return fd;
}

/**
 * Saves the state corresponding to the makeTimeCorrection()
 * integrators on exit to allow rapid restart. These are
 * saved to /root/.pps-last-state on the RPi or whatever
 * the root home directory is on other processors.
 */
int saveLastState(void){
	char buf[500];

	int fd = open_logerr(f.integral_state_file, O_CREAT | O_WRONLY, "saveLastState()");
	if (fd == -1){
		return -1;
	}

	memset(buf, 0, 500 * sizeof(char));
	char *pbuf;
	pbuf = buf;
	for (int i = 0; i < NUM_INTEGRALS; i++){
		sprintf(pbuf, "%lf\n", g.integral[i]);
		while (*pbuf != '\0'){
			pbuf += 1;
		}
	}

	sprintf(pbuf, "%d\n", g.slewIsLow);
	while (*pbuf != '\0'){
		pbuf += 1;
	}

	sprintf(pbuf, "%lf\n", g.avgIntegral);
	while (*pbuf != '\0'){
		pbuf += 1;
	}

	sprintf(pbuf, "%d\n", g.integralCount);
	while (*pbuf != '\0'){
		pbuf += 1;
	}

	sprintf(pbuf, "%d\n", g.correctionFifo_idx);
	while (*pbuf != '\0'){
		pbuf += 1;
	}

	sprintf(pbuf, "%lf\n", g.integralTimeCorrection);
	while (*pbuf != '\0'){
		pbuf += 1;
	}

	for (int i = 0; i < OFFSETFIFO_LEN; i++){
		sprintf(pbuf, "%d\n", g.correctionFifo[i]);
		while (*pbuf != '\0'){
			pbuf += 1;
		}
	}

	sprintf(pbuf, "%d\n", g.correctionFifoCount);
	while (*pbuf != '\0'){
		pbuf += 1;
	}

	sprintf(pbuf, "%d\n", g.correctionAccum);
	while (*pbuf != '\0'){
		pbuf += 1;
	}

	sprintf(pbuf, "%lf\n", g.freqOffset);
	while (*pbuf != '\0'){
		pbuf += 1;
	}

	sprintf(pbuf, "%d\n", g.activeCount);
	while (*pbuf != '\0'){
		pbuf += 1;
	}

	sprintf(pbuf, "%d\n", g.seq_num);
	while (*pbuf != '\0'){
		pbuf += 1;
	}

	sprintf(pbuf, "%d\n", (int)g.isControlling);
	while (*pbuf != '\0'){
		pbuf += 1;
	}

	sprintf(pbuf, "%d\n", g.hardLimit);
	while (*pbuf != '\0'){
		pbuf += 1;
	}

	int rv = write(fd, buf, strlen(buf) + 1);

	close(fd);

	if (rv == -1){
		sprintf(g.logbuf, "saveLastState() Write to %s failed\n", integral_state_file);
		writeToLog(g.logbuf, "daemonSaveArray()");
		return -1;
	}
	return 0;
}

/**
 * Loads the last state corresponding to the makeTimeCorrection()
 * integrators on startup to allow rapid restart.
 */
int loadLastState(void){
	char buf[500];

	int fd = open(f.integral_state_file, O_RDONLY);
	if (fd == -1){
		return 1;
	}

	int rv = read_logerr(fd, buf, 499, integral_state_file);
	if (rv == -1){
		return -1;
	}
	close(fd);

	char *pbuf = buf;

	for (int i = 0; i < NUM_INTEGRALS; i++){
		sscanf(pbuf, "%lf\n", &g.integral[i]);

		while (*pbuf != '\n'){
			pbuf += 1;
		}
		pbuf += 1;
	}

	int b;
	sscanf(pbuf, "%d\n", &b);
	g.slewIsLow = false;
	if (b > 0){
		g.slewIsLow = true;
	}

	while (*pbuf != '\n'){
		pbuf += 1;
	}
	pbuf += 1;

	sscanf(pbuf, "%lf\n", &g.avgIntegral);
	while (*pbuf != '\n'){
		pbuf += 1;
	}
	pbuf += 1;

	sscanf(pbuf, "%d\n", &g.integralCount);
	while (*pbuf != '\n'){
		pbuf += 1;
	}
	pbuf += 1;

	sscanf(pbuf, "%d\n", &g.correctionFifo_idx);
	while (*pbuf != '\n'){
		pbuf += 1;
	}
	pbuf += 1;

	sscanf(pbuf, "%lf\n", &g.integralTimeCorrection);
	while (*pbuf != '\n'){
		pbuf += 1;
	}
	pbuf += 1;

	for (int i = 0; i < OFFSETFIFO_LEN; i++){
		sscanf(pbuf, "%d\n", &g.correctionFifo[i]);

		while (*pbuf != '\n'){
			pbuf += 1;
		}
		pbuf += 1;
	}

	sscanf(pbuf, "%d\n", &g.correctionFifoCount);
	while (*pbuf != '\n'){
		pbuf += 1;
	}
	pbuf += 1;

	sscanf(pbuf, "%d\n", &g.correctionAccum);
	while (*pbuf != '\n'){
		pbuf += 1;
	}
	pbuf += 1;

	sscanf(pbuf, "%lf\n", &g.freqOffset);
	while (*pbuf != '\n'){
		pbuf += 1;
	}
	pbuf += 1;

	sscanf(pbuf, "%d\n", &g.activeCount);
	while (*pbuf != '\n'){
		pbuf += 1;
	}
	pbuf += 1;

	sscanf(pbuf, "%d\n", &g.seq_num);
	while (*pbuf != '\n'){
		pbuf += 1;
	}
	pbuf += 1;

	int t_f;
	sscanf(pbuf, "%d\n", &t_f);
	if (t_f == 0){
		g.isControlling = false;
	}
	else {
		g.isControlling = true;
	}
	while (*pbuf != '\n'){
		pbuf += 1;
	}
	pbuf += 1;

	sscanf(pbuf, "%d\n", &g.hardLimit);
	while (*pbuf != '\n'){
		pbuf += 1;
	}
	pbuf += 1;

	g.startingFromRestore = SECS_PER_MINUTE;

	g.freqOffset = g.integralTimeCorrection * g.integralGain;

	g.t3.modes = ADJ_FREQUENCY;
	g.t3.freq = (long)round(ADJTIMEX_SCALE * g.freqOffset);
	adjtimex(&g.t3);							// Adjust the system clock frequency.

	return 0;
}

/**
 * Writes the message saved in the file to pps-client.log.
 *
 * This function is used by threads in pps-sntp.cpp.
 *
 * @param[in] filename File containg a message.
 * @param[out] logbuf The log file buffer.
 *
 * @returns 0 on success, else -1 on error;
 */
int writeFileMsgToLogbuf(const char *filename, char *logbuf){
	struct stat stat_buf;
	int rv;

	int fd = open(filename, O_RDONLY);
	if (fd == -1){
		couldNotOpenMsgTo(logbuf, filename, "writeFileMsgToLogbuf()");
		printf("%s", logbuf);
		return -1;
	}
	fstat(fd, &stat_buf);
	int sz = stat_buf.st_size;

	if (sz >= LOGBUF_SZ-1){
		rv = read(fd, logbuf, LOGBUF_SZ-1);
		if (rv == -1){
			errorReadingMsgTo(logbuf, filename);
			printf("%s", logbuf);
			return rv;
		}
		logbuf[LOGBUF_SZ-1] = '\0';
	}
	else {
		rv = read(fd, logbuf, sz);
		if (rv == -1){
			errorReadingMsgTo(logbuf, filename);
			printf("%s", logbuf);
			return rv;
		}
		logbuf[sz] = '\0';
	}
	close(fd);
	remove(filename);

	return 0;
}

/**
 * Writes the message saved in the file to pps-client.log.
 *
 * @param[in] filename File containg a message.
 *
 * @returns 0 on success, else -1 on error;
 */
int writeFileMsgToLog(const char *filename){
	return writeFileMsgToLogbuf(filename, g.logbuf);
}

/**
 * Reads the PID of the child process when
 * the parent process needs to kill it.
 *
 * @returns The PID or -1 on error.
 */
pid_t getChildPID(void){
	pid_t pid = 0;

	memset(g.strbuf, 0, STRBUF_SZ-1);

	int pfd = open_logerr(f.pidFilename, O_RDONLY, "getChildPID()");
	if (pfd == -1){
		return -1;
	}
	if (read_logerr(pfd, g.strbuf, 19, f.pidFilename) == -1){
		close(pfd);
		return -1;
	}
	sscanf(g.strbuf, "%d\n", &pid);
	close(pfd);
	if (pid > 0){
		return pid;
	}
	return -1;
}

/**
 * Uses a system call to pidof to see if PPS-Client is running.
 *
 * @returns If a PID for pps exists returns "true".  Else returns
 * "false".
 */
bool ppsIsRunning(void){
	char buf[50];
	char cmdbuf[100];

	memset(cmdbuf, 0, 100 * sizeof(char));
	strcpy(cmdbuf, "pidof pps-client > ");
	strcat(cmdbuf, f.pps_msg_file);

	int rv = sysCommand(cmdbuf);
	if (rv == -1){
		return false;
	}

	int fd = open(f.pps_msg_file, O_RDONLY);
	if (fd == -1){
		sprintf(g.logbuf, "ppsIsRunning() Failed. Could not open %s. Error: %s\n", f.pps_msg_file, strerror(errno));
		writeToLog(g.logbuf, "ppsIsRunning()");
		return false;
	}
	memset(buf, 0, 50 * sizeof(char));
	rv = read(fd, buf, 50);
	if (rv == -1){
		sprintf(g.logbuf, "ppsIsRunning() Failed. Could not read %s. Error: %s\n", f.pps_msg_file, strerror(errno));
		writeToLog(g.logbuf, "ppsIsRunning()");
		return false;
	}

	int callerPID = 0, daemonPID = 0;					// If running both of these exist
	sscanf(buf, "%d %d\n", &callerPID, &daemonPID);

	close(fd);
	remove(f.pps_msg_file);

	if (daemonPID == 0){									// Otherwise only the first exists.
		return false;
	}
	return true;
}

/**
 * Creates a PID file for the PPS-Client daemon.
 *
 * @returns The PID.
 */
int createPIDfile(void){

	struct stat buf;
	int rv = stat(f.pidFilename, &buf);
	if (rv == 0){							// If the PID file exists from previously,
		rv = remove(f.pidFilename);			// then remove it.
		if (rv != 0){
			return -1;
		}
	}

	int pfd = open_logerr(f.pidFilename, O_RDWR | O_CREAT | O_EXCL, "createPIDfile()");
	if (pfd == -1){
		return -1;
	}

	pid_t ppid = getpid();

	sprintf(g.strbuf, "%d\n", ppid);
	if (write(pfd, g.strbuf, strlen(g.strbuf)) == -1)	// Try to write the PID
	{
		close(pfd);
		sprintf(g.logbuf, "createPIDfile() Could not write a PID file. Error: %s\n", strerror(errno));
		writeToLog(g.logbuf, "createPIDfile()");
		return -1;									// Write failed.
	}
	close(pfd);

	return ppid;
}

/**
 * Reads the PPS-Client config file and sets bits
 * in G.config_select to 1 or 0 corresponding to
 * whether a particular G.configVals appears in the
 * config file. The G.configVals from the file is then
 * copied to fbuf and a pointer to that string is
 * placed in the G.configVals array.
 *
 * If the G.configVals did not occur in the config file
 * then G.configVals has a NULL char* in the corresponding
 * location.
 *
 * @param[in] fconfig Root filename of pps-client.conf.
 *
 * @returns 0 on success, else -1 on error.
 */
int readConfigFile(const char *fconfig){

	struct stat stat_buf;

	int rvs = stat(fconfig, &configFileStat);
	if (rvs == -1){
		sprintf(g.logbuf, "readConfigFile(): Config file not found.\n");
		writeToLog(g.logbuf, "readConfigFile()");
		return -1;									// No config file
	}

	timespec t = configFileStat.st_mtim;

	if (g.configWasRead && g.seq_num > 0 && modifyTime == t.tv_sec){
		return 1;									// Config file unchanged from last read
	}

	modifyTime = t.tv_sec;

	int fd = open_logerr(fconfig, O_RDONLY, "readConfigFile()");
	if (fd == -1){
		return -1;
	}

	fstat(fd, &stat_buf);
	int sz = stat_buf.st_size;

	if (sz >= CONFIG_FILE_SZ){
		sprintf(g.logbuf, "readConfigFile(): not enough space allocated for config file.\n");
		writeToLog(g.logbuf, "readConfigFile()");
		return -1;
	}

	int rv = read_logerr(fd, g.configBuf, sz, fconfig);
	if (rv == -1 || sz != rv){
		return -1;
	}
	close(fd);

	g.configBuf[sz] = '\0';

	int nCfgStrs = 0;
	int i;

	char *pToken = strtok(g.configBuf, "\n");			// Separate tokens at "\n".

	while (pToken != NULL){
		if (strlen(pToken) != 0){						// If not a blank line.
			for (int j = 0; j < 10; j++){				// Skip leading spaces.
				if (pToken[0] == ' '){
					pToken += 1;
				}
				else {
					break;								// Break on first non-space character.
				}
			}

			if (pToken[0] != '#'){						// Ignore comment lines.
				g.configVals[nCfgStrs] = pToken;
				nCfgStrs += 1;
			}
		}
		pToken = strtok(NULL, "\n");					// Get the next token.
	}

	if (nCfgStrs == 0){
		return 0;
	}

	for (i = 0; i < nCfgStrs; i++){						// Compact g.configBuf to remove string terminators inserted by
		if (i == 0){									// strtok() so that g.configBuf can be searched as a single string.
			strcpy(g.configBuf, g.configVals[i]);
		}
		else {
			strcat(g.configBuf, g.configVals[i]);
		}
		strcat(g.configBuf, "\n");
	}

	int nValidCnfgs = sizeof(valid_config) / sizeof(char *);

	char **configVal = g.configVals;					// Use g.configVals to return pointers to value strings
	char *value;

	for (i = 0; i < nValidCnfgs; i++){

		char *found = strstr(g.configBuf, valid_config[i]);
		if (found != NULL){
			g.config_select |= 1 << i;					// Set a bit in g.config_select

			value = strpbrk(found, "=");				// Get the value string following '='.
			value += 1;

			configVal[i] = value;						// Point to g.configVals[i] value string in g.configBuf
		}
		else {
			g.config_select &= ~(1 << i);				// Clear a bit in config_select
			configVal[i] = NULL;
		}
	}

	for (i = 0; i < nValidCnfgs; i++){					// Replace the "\n" at the end of each value
		if (configVal[i] != NULL){						// string with a string terminator.
			value = strpbrk(configVal[i], "\n");
			if (value != NULL){
				*value = 0;
			}
		}
	}

	if (g.seq_num > 0){
		g.configWasRead = true;							// Insures that config file read at least once.
	}

	return 0;
}

/**
 * Writes an accumulating statistical distribution to disk and
 * rolls over the accumulating data to a new file every epoch
 * counts and begins a new distribution file. An epoch is
 * 86,400 counts.
 *
 * @param[in] distrib The array containing the distribution.
 * @param[in] len The length of the array.
 * @param[in] scaleZero The array index corresponding to distribution zero.
 * @param[in] count The current number of samples in the distribution.
 * @param[out] last_epoch The saved count of the previous epoch.
 * @param[in] distrib_file The filename of the last completed
 * distribution file.
 * @param[in] last_distrib_file The filename of the currently
 * forming distribution file.
 */
void writeDistribution(int distrib[], int len, int scaleZero, int count,
		int *last_epoch, const char *distrib_file, const char *last_distrib_file){
	int rv = 0;
	remove(distrib_file);
	int fd = open_logerr(distrib_file, O_CREAT | O_WRONLY | O_APPEND, "writeDistribution()");
	if (fd == -1){
		return;
	}
	for (int i = 0; i < len; i++){
		sprintf(g.strbuf, "%d %d\n", i-scaleZero, distrib[i]);
		rv = write(fd, g.strbuf, strlen(g.strbuf));
		if (rv == -1){
			sprintf(g.logbuf, "writeDistribution() Unable to write to %s. Error: %s\n", distrib_file, strerror(errno));
			writeToLog(g.logbuf, "writeDistribution()");
			close(fd);
			return;
		}
	}
	close(fd);

	int epoch = count / SECS_PER_DAY;
	if (epoch != *last_epoch ){
		*last_epoch = epoch;
		remove(last_distrib_file);
		rename(distrib_file, last_distrib_file);
		memset(distrib, 0, len * sizeof(int));
	}
}

/**
 * Writes a distribution to disk approximately once a minute
 * containing 60 additional jitter samples recorded at the
 * occurrance of the PPS interrupt. The distribution is
 * rolled over to a new file every 24 hours.
 */
void writeJitterDistribFile(void){
	if (g.jitterCount % SECS_PER_MINUTE == 0 && g.seq_num > SETTLE_TIME){
		int scaleZero = JITTER_DISTRIB_LEN / 3;
		writeDistribution(g.jitterDistrib, JITTER_DISTRIB_LEN, scaleZero, g.jitterCount,
				&lastJitterFileno, f.jitter_distrib_file, f.last_jitter_distrib_file);
	}
}

/**
 * Writes a distribution to disk approximately once a minute
 * containing 60 additional time correction samples derived
 * from the PPS interrupt. The distribution is rolled over
 * to a new file every 24 hours.
 */
void writeErrorDistribFile(void){
	if (g.errorCount % SECS_PER_MINUTE == 0 && g.seq_num > SETTLE_TIME){
		int scaleZero = ERROR_DISTRIB_LEN / 6;
		writeDistribution(g.errorDistrib, ERROR_DISTRIB_LEN, scaleZero,
				g.errorCount, &lastErrorFileno, f.distrib_file, f.last_distrib_file);
	}
}

/**
 * Writes the previously completed list of 10 minutes of recorded
 * time offsets and applied frequency offsets indexed by seq_num.
 *
 * @param[in] filename The file to write to.
 */
void writeOffsets(const char *filename){
	int fd = open_logerr(filename, O_CREAT | O_WRONLY | O_TRUNC, "writeOffsets()");
	if (fd == -1){
		return;
	}
	for (int i = 0; i < SECS_PER_10_MIN; i++){
		int j = g.recIndex2 + i;
		if (j >= SECS_PER_10_MIN){
			j -= SECS_PER_10_MIN;
		}
		sprintf(g.strbuf, "%d %d %lf\n", g.seq_numRec[j], g.offsetRec[j], g.freqOffsetRec2[j]);
		int rv = write(fd, g.strbuf, strlen(g.strbuf));
		if (rv == -1){
			sprintf(g.logbuf, "writeOffsets() Unable to write to %s. Error: %s\n", filename, strerror(errno));
			writeToLog(g.logbuf, "writeOffsets()");
		}
	}
	close(fd);
}

/**
 * Writes the last 24 hours of clock frequency offset and Allan
 * deviation in each 5 minute interval indexed by the timestamp
 * at each interval.
 *
 * @param[in] filename The file to write to.
 */
void writeFrequencyVars(const char *filename){
	int fd = open_logerr(filename, O_CREAT | O_WRONLY | O_TRUNC, "writeFrequencyVars()");
	if (fd == -1){
		return;
	}
	for (int i = 0; i < NUM_5_MIN_INTERVALS; i++){
		int j = g.recIndex + i;							// Read the circular buffers relative to g.recIndx.
		if (j >= NUM_5_MIN_INTERVALS){
			j -= NUM_5_MIN_INTERVALS;
		}
		sprintf(g.strbuf, "%ld %lf %lf\n", g.timestampRec[j], g.freqOffsetRec[j], g.freqAllanDev[j]);
		int rv = write(fd, g.strbuf, strlen(g.strbuf));
		if (rv == -1){
			sprintf(g.logbuf, "writeFrequencyVars() Write to %s failed with error: %s\n", filename, strerror(errno));
			writeToLog(g.logbuf, "writeFrequencyVars");
			close(fd);
			return;
		}
	}
	close(fd);
}

/**
 * Saves a distribution consisting of an array of doubles.
 *
 * @param[in] distrib The distribution array.
 * @param[in] filename The file to save to.
 * @param[in] len The length of the array.
 * @param[in] arrayZero The array index of distribution value zero.
 *
 * @returns 0 on success, else -1 on error.
 */
int saveDoubleArray(double distrib[], const char *filename, int len, int arrayZero){

	int fd = open_logerr(filename, O_CREAT | O_WRONLY | O_TRUNC, "saveDoubleArray()");
	if (fd == -1){
		return -1;
	}

	int fileMaxLen = len * MAX_LINE_LEN * sizeof(char);
	char *filebuf = new char[fileMaxLen];
	int fileLen = 0;

	filebuf[0] = '\0';
	for (int i = 0; i < len; i++){
		sprintf(g.strbuf, "%d %7.2lf\n", i - arrayZero, distrib[i]);
		fileLen += strlen(g.strbuf);
		strcat(filebuf, g.strbuf);
	}

	int rv = write(fd, filebuf, fileLen + 1);
	if (rv == -1){
		sprintf(g.logbuf, "saveDoubleArray() Write to %s failed with error: %s\n", filename, strerror(errno));
		writeToLog(g.logbuf, "saveDoubleArray()");
		return -1;
	}

	fsync(fd);

	delete[] filebuf;
	close(fd);
	return 0;
}

/**
 * From within the daemon, reads the data label and filename
 * of an array to write to disk from a request made from the
 * command line with "pps-client -s [label] <filename>".
 * Then matches the requestStr to the corresponding arrayData
 * which is then passed to a routine that saves the array
 * identified by the data label.
 *
 * @returns 0 on success else -1 on fail.
 */
int processWriteRequest(void){
	struct stat buf;

	int rv = stat(f.arrayData_file, &buf);			// stat() used only to check that there is an arrayData_file
	if (rv == -1){
		return 0;
	}

	int fd = open(f.arrayData_file, O_RDONLY);
	if (fd == -1){
		sprintf(g.logbuf, "processWriteRequest() Unable to open %s. Error: %s\n", f.arrayData_file, strerror(errno));
		writeToLog(g.logbuf, "processWriteRequest()");
		return -1;
	}

	char requestStr[25];
	char filename[225];

	filename[0] = '\0';
	rv = read(fd, g.strbuf, STRBUF_SZ-1);
	sscanf(g.strbuf, "%s %s", requestStr, filename);

	close(fd);
	remove(f.arrayData_file);

	int arrayLen = sizeof(arrayData) / sizeof(struct saveFileData);
	for (int i = 0; i < arrayLen; i++){
		if (strcmp(requestStr, arrayData[i].label) == 0){
			if (strlen(filename) == 0){
				strcpy(filename, arrayData[i].filename);
			}
			if (arrayData[i].arrayType == 2){
				saveDoubleArray((double *)arrayData[i].array, filename, arrayData[i].arrayLen, arrayData[i].arrayZero);
				break;
			}
			if (arrayData[i].arrayType == 3){
				writeFrequencyVars(filename);
				break;
			}
			if (arrayData[i].arrayType == 4){
				writeOffsets(filename);
				break;
			}

		}
	}
	return 0;
}

/**
 * Gets the daemon internal file names and state params
 * for accessDaemon(). Necessary because daemons can not
 * directly share contents of variables.
 */
int getSharedConfigs(void){

	int rv = readConfigFile(config_file);
	if (rv == -1){
		return rv;
	}

	char *sp;

	sp = getString(RUNDIR);
	if (sp != NULL){
		strcpy(f.pidFilename, sp);
		strcat(f.pidFilename, pidFilename);
	}

	sp = getString(SHMDIR);
	if (sp != NULL){
		strcpy(f.assert_file, sp);
		strcat(f.assert_file, assert_file);

		strcpy(f.displayParams_file, sp);
		strcat(f.displayParams_file, displayParams_file);

		strcpy(f.arrayData_file, sp);
		strcat(f.arrayData_file, arrayData_file);

		strcpy(f.pps_msg_file, sp);
		strcat(f.pps_msg_file, pps_msg_file);
	}

	sp = getString(TSTDIR);
	if (sp != NULL){
		strcpy(f.last_distrib_file, sp);
		strcat(f.last_distrib_file, last_distrib_file);

		strcpy(f.distrib_file, sp);
		strcat(f.distrib_file, distrib_file);

		strcpy(f.last_jitter_distrib_file, sp);
		strcat(f.last_jitter_distrib_file, last_jitter_distrib_file);

		strcpy(f.home_file, sp);
		strcat(f.home_file, home_file);
	}

	sp = getString(LOGDIR);
	if (sp != NULL){
		strcpy(f.log_file, sp);
		strcat(f.log_file, log_file);

		strcpy(f.old_log_file, sp);
		strcat(f.old_log_file, old_log_file);
	}

	g.doNISTsettime = true;

	if (isEnabled(SERIAL)){
		g.doNISTsettime = false;
		g.doSerialsettime = true;
	}
	else if (isDisabled(SERIAL)){
		g.doSerialsettime = false;
	}

	sp = getString(SERIAL_PORT);
	if (sp != NULL){
		strcpy(g.serialPort, sp);
	}

	if (isEnabled(NIST)){
		g.doNISTsettime = true;
	}
	else if (isDisabled(NIST)){
		g.doNISTsettime = false;
	}

	if (isEnabled(NTPCHECK)){
		g.checkNTP = true;
	}
	else if (isDisabled(NTPCHECK)){
		g.checkNTP = false;
	}

	sp = getString(NTPSERVER);
	if (sp != NULL){
		strcpy(g.ntpServer, sp);
	}
	
	g.ntpChecked = false;
	
	return 0;
}

/**
 * Processes the files and configuration settings specified
 * by the PPS-Client config file for the pps-client daemon.
 */
int getConfigs(void){

	// Activities that are checked each second ******************

	if (isEnabled(ERROR_DISTRIB)){
		if (writeErrorDistrib == false){
			memset(g.errorDistrib, 0, ERROR_DISTRIB_LEN * sizeof(int));
			g.errorCount = 0;
			writeErrorDistrib = true;
		}
	}
	else {
		writeErrorDistrib = false;
	}

	if (writeErrorDistrib){
		writeErrorDistribFile();
	}

	if (isEnabled(JITTER_DISTRIB)){
		if (writeJitterDistrib == false){
			memset(g.jitterDistrib, 0, JITTER_DISTRIB_LEN * sizeof(int));
			g.jitterCount = 0;
			writeJitterDistrib = true;
		}
	}
	else {
		writeJitterDistrib = false;
	}

	if (writeJitterDistrib){
		writeJitterDistribFile();
	}

	int rv = processWriteRequest();
	if (rv == -1){
		return rv;
	}

	//****************************************************************

	rv = readConfigFile(config_file);
	if (rv == -1){
		return rv;
	}
	if (rv == 1){						// Skip reading again if rv == 1
		return 0;
	}

	char *sp;
	struct stat dirStat;

	sp = getString(RUNDIR);
	if (sp != NULL){

		rv = stat(sp, &dirStat);
		if (rv == -1){
			printf("Invalid path for rundir. %s: %s\n", strerror(errno), sp);
			return rv;
		}

		strcpy(f.pidFilename, sp);
		strcat(f.pidFilename, pidFilename);
	}

	sp = getString(SHMDIR);
	if (sp != NULL){

		rv = stat(sp, &dirStat);
		if (rv == -1){
			printf("Invalid path for shmdir in pps-client.conf. %s: %s\n", strerror(errno), sp);
			return rv;
		}

		strcpy(f.assert_file, sp);
		strcat(f.assert_file, assert_file);

		strcpy(f.displayParams_file, sp);
		strcat(f.displayParams_file, displayParams_file);

		strcpy(f.arrayData_file, sp);
		strcat(f.arrayData_file, arrayData_file);

		strcpy(f.pps_msg_file, sp);
		strcat(f.pps_msg_file, pps_msg_file);

		strcpy(f.linuxVersion_file, sp);
		strcat(f.linuxVersion_file, linuxVersion_file);

		strcpy(f.gmtTime_file, sp);
		strcat(f.gmtTime_file, gmtTime_file);

		strcpy(f.nistTime_file, sp);
		strcat(f.nistTime_file, nistTime_file);
	}

	sp = getString(TSTDIR);
	if (sp != NULL){

		rv = stat(sp, &dirStat);
		if (rv == -1){
			printf("Invalid path for tstdir in pps-client.conf. %s: %s\n", strerror(errno), sp);
			return rv;
		}

		strcpy(f.last_distrib_file, sp);
		strcat(f.last_distrib_file, last_distrib_file);

		strcpy(f.distrib_file, sp);
		strcat(f.distrib_file, distrib_file);

		strcpy(f.last_jitter_distrib_file, sp);
		strcat(f.last_jitter_distrib_file, last_jitter_distrib_file);

		strcpy(f.jitter_distrib_file, sp);
		strcat(f.jitter_distrib_file, jitter_distrib_file);

		strcpy(f.home_file, sp);
		strcat(f.home_file, home_file);
	}

	sp = getString(LOGDIR);
	if (sp != NULL){

		rv = stat(sp, &dirStat);
		if (rv == -1){
			printf("Invalid path for logdir in pps-client.conf. %s: %s\n", strerror(errno), sp);
			return rv;
		}

		strcpy(f.log_file, sp);
		strcat(f.log_file, log_file);

		strcpy(f.old_log_file, sp);
		strcat(f.old_log_file, old_log_file);
	}

	sp = getString(PPSDEVICE);
	if (sp != NULL){

		rv = stat(sp, &dirStat);
		if (rv == -1){
			printf("Invalid path for ppsdevice in pps-client.conf. %s: %s\n", strerror(errno), sp);
			return rv;
		}

		strcpy(f.pps_device, sp);
	}

	sp = getString(PPSDELAY);
	if (sp != NULL){
		char *ptr;
		g.zeroOffset = (int)strtol(sp, &ptr, 10);
	}

	sp = getString(SEGREGATE);
	if (sp != NULL){
		rv = sscanf(sp, "%d/%d", &g.useCore, &g.nCores);
		if (rv != 2 || g.useCore >= g.nCores){
			printf("Invalid value for segregate in pps-client.conf\n");
			return -1;
		}
	}

	sp = getString(PPSPHASE);
	if (sp != NULL){
		char *ptr;
		g.ppsPhase = (int)strtol(sp, &ptr, 10);
		if(g.ppsPhase > 1){
			printf("Invalid value for ppsphase in pps-client.conf. Must be 0 or 1.\n");
			return -1;
		}
	}

	sp = getString(PROCDIR);
	if (sp != NULL){

		rv = stat(sp, &dirStat);
		if (rv == -1){
			printf("Invalid path for procdir in pps-client.conf. %s: %s\n", strerror(errno), sp);
			return rv;
		}

		strcpy(f.cpuinfo_file, sp);
		strcat(f.cpuinfo_file, cpuinfo_file);
	}

	g.doNISTsettime = true;

	if (isEnabled(NIST)){
		g.doNISTsettime = true;
	}
	else if (isDisabled(NIST)){
		g.doNISTsettime = false;
	}

	if (isEnabled(SERIAL)){
		g.doNISTsettime = false;
		g.doSerialsettime = true;
	}
	else if (isDisabled(SERIAL)){
		g.doSerialsettime = false;
	}

	sp = getString(SERIAL_PORT);
	if (sp != NULL){
		strcpy(g.serialPort, sp);
	}

	if (isEnabled(EXIT_LOST_PPS)){
		g.exitOnLostPPS = true;
	}
	else if (isDisabled(EXIT_LOST_PPS)){
		g.exitOnLostPPS = false;
	}

	if (isEnabled(NTPCHECK)){
		g.checkNTP = true;
	}
	else if (isDisabled(NTPCHECK)){
		g.checkNTP = false;
	}

	sp = getString(NTPSERVER);
	if (sp != NULL){
		strcpy(g.ntpServer, sp);
	}
	
	return 0;
}

/**
 * Write a timestamp provided as a double to a temporary
 * file each second.
 *
 * @param[in] timestamp The timestamp value.
 */
void writeTimestamp(double timestamp){
	memset(g.strbuf, 0, STRBUF_SZ-1);

	sprintf(g.strbuf, "%lf#%d\n", timestamp, g.seq_num);
	remove(f.assert_file);

	int pfd = open_logerr(f.assert_file, O_CREAT | O_WRONLY, "writeTimestamp() 1");
	if (pfd == -1){
		return;
	}
	int rv = write(pfd, g.strbuf, strlen(g.strbuf) + 1);		// Write PPS timestamp to f.assert_file
	if (rv == -1){
		sprintf(g.logbuf, "writeTimestamp() write to assert_file failed with error: %s\n", strerror(errno));
		writeToLog(g.logbuf, "writeTimestamp() 2");
	}
	close(pfd);
}

/**
 * Provides formatting for console printf() strings.
 *
 * Horizontally left-aligns a number following token, ignoring
 * numeric sign, in a buffer generated by sprintf() by padding
 * the buffer with spaces preceding the number to be aligned and
 * returning the adjusted length of the buffer.
 *
 * @param[in] token The token string preceding the number to be aligned.
 * @param[in,out] buf The buffer containing the token.
 * @param[in] len The initial length of the buffer.
 *
 * @returns Adjusted length of the buffer.
 */
int alignNumbersAfter(const char *token, char *buf, int len){
	int pos = 0;

	char *str = strstr(buf, token);
	if (str == NULL){
		sprintf(g.logbuf, "alignNumbersAfter(): token not found. Exiting.\n");
		writeToLog(g.logbuf, "alignNumbersAfter()");
		return -1;
	}
	str += strlen(token);

	pos = str - buf;
	if (buf[pos] != '-'){
		memmove(str + 1, str, len - pos);
		buf[pos] = ' ';
		len += 1;
	}
	return len;
}

/**
 * Provides formatting for console printf() strings.
 *
 * Horizontally aligns token by a fixed number of
 * characters from the end of refToken by padding
 * the buffer with spaces at the end of the number
 * following refToken.
 *
 * @param[in] refToken The reference token which
 * is followed by a number having a variable length.
 *
 * @param[in] offset The number of characters in the
 * adjusted buffer from the end of refToken to the start
 * of token.
 *
 * @param[in] token The token to be aligned.
 * @param[out] buf The buffer containing the tokens.
 * @param[in] len the initial length of the buffer.
 *
 * @returns The adjusted length of the buffer.
 */
int alignTokens(const char *refToken, int offset, const char *token, char *buf, int len){

	int pos1, pos2;

	char *str = strstr(buf, refToken);
	if (str == NULL){
		sprintf(g.logbuf, "alignTokens(): refToken not found. Exiting.\n");
		writeToLog(g.logbuf, "alignTokens()");
		return -1;
	}
	str += strlen(refToken);
	pos1 = str - buf;

	str = strstr(buf, token);
	if (str == NULL){
		sprintf(g.logbuf, "alignTokens(): token not found. Exiting.\n");
		writeToLog(g.logbuf, "alignTokens()");
		return -1;
	}
	pos2 = str - buf;

	while (pos2 < pos1 + offset){
		memmove(str + 1, str, len - pos2);
		buf[pos2] = ' ';
		pos2 += 1;
		str += 1;
		len += 1;
	}
	return len;
}

/**
 * Records a state params string to a buffer, savebuf, that
 * is saved to a tmpfs memory file by writeStatusStrings()
 * along with other relevant messages recorded during the
 * same second.
 *
 * @returns 0 on success, else -1 on error.
 */
int bufferStateParams(void){

	if (g.interruptLossCount == 0) {
		const char *timefmt = "%F %H:%M:%S";
		char timeStr[50];
		char printStr[200];

		memset(timeStr, 0, 50 * sizeof(char));
		strftime(timeStr, 50, timefmt, localtime(&g.pps_t_sec));

		char *printfmt = g.strbuf;

		strcpy(printfmt, "%s.%06d  %d  jitter: ");

		if (g.clampAbsolute){
			strcat(printfmt, "%d freqOffset: %f avgCorrection: %f  clamp: %d\n");
		}
		else {
			strcat(printfmt, "%d freqOffset: %f avgCorrection: %f  clamp: %d*\n");
		}

		sprintf(printStr, printfmt, timeStr, g.pps_t_usec, g.seq_num,
				g.jitter, g.freqOffset, g.avgCorrection, g.hardLimit);

		int len = strlen(printStr) + 1;							// strlen + '\0'
		len = alignNumbersAfter("jitter: ", printStr, len);
		if (len == -1){
			return -1;
		}
		len = alignTokens("jitter:", 6, "freqOffset:", printStr, len);
		if (len == -1){
			return -1;
		}
		len = alignNumbersAfter("freqOffset:", printStr, len);
		if (len == -1){
			return -1;
		}
		len = alignTokens("freqOffset:", 12, "avgCorrection:", printStr, len);
		if (len == -1){
			return -1;
		}
		len = alignNumbersAfter("avgCorrection: ", printStr, len);
		if (len == -1){
			return -1;
		}
		len = alignTokens("avgCorrection:", 12, "clamp:", printStr, len);
		if (len == -1){
			return -1;
		}

		bufferStatusMsg(printStr);
	}
	return 0;
}

/**
 * Removes all lines containing "key1 key2" from the text in fbuf.
 *
 * @param[in] key1
 * @param[in] key2
 * @param[out] fbuf The text buffer to process.
 */
void removeConfigKeys(const char *key1, const char *key2, char *fbuf){

	char *pHead = NULL, *pTail = NULL, *pNxt = NULL;
	char *pLine = NULL;

	pLine = fbuf;

	while (strlen(pLine) > 0){
										// Search for key1 followed by key2
		while (pLine != NULL){										// If this is the next line

			pNxt = pLine;
			while (pNxt[0] == ' ' || pNxt[0] == '\t'){
				pNxt += 1;
			}

			if (strncmp(pNxt, key1, strlen(key1)) == 0){
				pHead = pNxt;
				pNxt += strlen(key1);

				while (pNxt[0] == ' ' || pNxt[0] == '\t'){
					pNxt += 1;
				}

				if (strncmp(pNxt, key2, strlen(key2)) == 0){
					pNxt += strlen(key2);

					while (pNxt[0] == ' ' || pNxt[0] == '\t' ||  pNxt[0] == '\n'){
						pNxt += 1;
					}
					pTail = pNxt;

					memmove(pHead, pTail, strlen(pTail)+1);
					pLine = pHead;								// Point pLine to any remaining
					break;										// tail of the file for removal
				}												// of any more lines containing
			}													// "key1 key2".
			pLine = strchr(pLine, '\n') + 1;
		}
	}
}

/**
 * Returns the Linux kernel version string corresponding
 * to 'uname -r'.
 */
char *getLinuxVersion(void){
	int rv;
	char fbuf[20];
	char cmdbuf[100];

	memset(cmdbuf, 0, 100 * sizeof(char));
	strcpy(cmdbuf, "uname -r > ");
	strcat(cmdbuf, f.linuxVersion_file);

	rv = sysCommand(cmdbuf);
	if (rv == -1){
		return NULL;
	}

	int fd = open(f.linuxVersion_file, O_RDONLY);
	rv = read(fd, fbuf, 20);
	if (rv == -1){
		sprintf(g.logbuf, "getLinuxVersion(): Unable to read Linux version from %s\n", f.linuxVersion_file);
		writeToLog(g.logbuf, "getLinuxVersion()");
		return NULL;
	}
	sscanf(fbuf, "%s\n", g.linuxVersion);
	return g.linuxVersion;
}


/**
 * Returns the principle version number of
 * this CPU if it is a Raspberry Pi.
 *
 * @returns The CPU version number or 0
 * if not a Raspberry Pi.
 */
int getRPiCPU(void){
	int rv;

	int fd = open_logerr(f.cpuinfo_file, O_RDONLY, "getRPiCPU()");
	if (fd == -1){
		printf("getRPiCPU() Open f.cpuinfo_file failed\n");
		return -1;
	}

	char *filebuf = new char[CONFIG_FILE_SZ];
	memset(filebuf, 0, CONFIG_FILE_SZ * sizeof(char));

	rv = read(fd, filebuf, CONFIG_FILE_SZ-1);
	if (rv == -1){
		sprintf(g.logbuf, "getRPiCPU(): Unable to read from %s\n", f.cpuinfo_file);
		writeToLog(g.logbuf, "getRPiCPU()");
		delete filebuf;
		return -1;
	}

	rv = 0;

	char *pstr = strstr(filebuf, "Raspberry Pi");
	if (pstr != NULL){
		if (strncmp(pstr, "Raspberry Pi 3", 14) == 0){
			rv = 3;
		}
		else if (strncmp(pstr, "Raspberry Pi 4", 14) == 0){
			rv = 4;
		}
	}
	delete filebuf;
	return rv;
}

/**
 * Segregate PPS-Client to a separate core from
 * the other processes running on the processor.
 *
 * Not all will be movable. Error messages are generated
 * for the ones that can't be moved but those messages
 * are suppressed.
 */
int assignProcessorAffinity(void){
	int rv;
	char cmdstr[100];
	char cmd[100];
	int bitmask = 0;
	struct stat stat_buf;

//	sprintf(cmdstr, "printf 'Assigned PPS-Client to processor %d\n'", g.useCore);
	sysCommand(cmdstr);

	for (int i = g.nCores - 1; i >= 0; i--){						// Create bitmask of cores to be used
		bitmask = bitmask << 1;										// by all processes except PPS-Client
		if (g.useCore != i){
			bitmask |= 1;
		}
	}

	memset(cmd, 0, 100);
	sprintf(cmd,"taskset -p %d ", bitmask);

	const char *end = " > /dev/null 2>&1";							// "> /dev/null 2>&1" suppresses all messages

	rv = sysCommand("ps --no-headers -eo pid > /dev/shm/pid.txt");	// Save PIDs only of all running processes
	if (rv == -1){
		return rv;
	}


	int fd = open("/dev/shm/pid.txt", O_RDONLY);					// Open the PID file
	if (fd == -1){
		return fd;
	}

	fstat(fd, &stat_buf);
	int sz = stat_buf.st_size;

	char *fbuf = new char[sz+1];

	rv = read(fd, fbuf, sz);										// Read PID file into fbuf

	close(fd);

	if (rv == -1){
		delete fbuf;
		return rv;
	}

	char *pbuf = strtok(fbuf, "\n");								// Locate the first CR

	pbuf = strtok(NULL, "\n\0");
	memset(cmdstr, 0, 100);
	strcpy(cmdstr, cmd);
	strcat(cmdstr, fbuf);
	strcat(cmdstr, end);

	rv = sysCommand(cmdstr);
	if (rv == -1){
		delete fbuf;
		return rv;
	}

	while (pbuf != NULL){
		pbuf = strtok(NULL, "\n\0");
		if (pbuf != NULL){

			memset(cmdstr, 0, 100);
			strcpy(cmdstr, cmd);
			strcat(cmdstr, pbuf);
			strcat(cmdstr, end);

			rv = sysCommand(cmdstr);				// Attempt to put each running processes on other cores
			if (rv == -1){
				delete fbuf;
				return rv;
			}

		}
	}

	bitmask = 0;

	for (int i = g.nCores - 1; i >= 0; i--){		// Create bitmask of core to be used
		bitmask = bitmask << 1;						// by PPS-Client
		if (g.useCore == i){
			bitmask |= 1;
		}
	}

	memset(cmdstr, 0, 100);
	sprintf(cmdstr,"taskset -p %d `pidof pps-client` > /dev/null 2>&1", bitmask);
	sysCommand(cmdstr);

//	printf("\n");

	delete fbuf;
	return 0;
}


/**
 * Extracts the sequence number G.seq_num from a char string
 * and returns the value.
 *
 * @param[in] pbuf The string to search.
 *
 * @returns The sequence number.
 */
int getSeqNum(const char *pbuf){

	char *pSpc, *pNum;
	int seqNum = 0;

	pSpc = strpbrk((char *)pbuf, space);

	pNum = strpbrk(pSpc, num);
	pSpc = strpbrk(pNum, space);
	pNum = strpbrk(pSpc, num);

	sscanf(pNum, "%d ", &seqNum);
	return seqNum;
}

/**
 * Reads the state params saved to shared memory by the
 * PPS-Client daemon and prints the param string to the
 * console each second.
 */
void showStatusEachSecond(void){
	struct timeval tv1;
	struct timespec ts2;
	char paramsBuf[MSGBUF_SZ];
	struct stat stat_buf;
	int seqNum = 0, lastSeqNum = -1;

	if (g.doSerialsettime == true){
		printf("\nSerial port, %s, is providing time of day from GPS Satellites\n\n", g.serialPort);
	}
	else if (g.doNISTsettime == true){
		printf("\nNIST UDP time servers are providing time of day over the Internet\n\n");
	}

	int dispTime = 500000;									// Display at half second

	gettimeofday(&tv1, NULL);
	ts2 = setSyncDelay(dispTime, tv1.tv_usec);

	for (;;){

		if (g.exit_loop){
			break;
		}
		nanosleep(&ts2, NULL);

		int fd = open(f.displayParams_file, O_RDONLY);
		if (fd == -1){
			printf("showStatusEachSecond(): Could not open f.displayParams_file");
			printf("%s", f.displayParams_file);
			printf("\n");
		}
		else {
			fstat(fd, &stat_buf);
			int sz = stat_buf.st_size;

			if (sz >= MSGBUF_SZ){
				printf("showStatusEachSecond() buffer too small. sz: %d\n", sz);
				close(fd);
				break;
			}

			int rv = read(fd, paramsBuf, sz);
			close(fd);
			if (rv == -1){
				printf("showStatusEachSecond() Read paramsBuf failed with error: %s\n", strerror(errno));
				break;

			}

			if (sz > 0){

				paramsBuf[sz]= '\0';

				char *sv = strstr(paramsBuf, "jitter");
				if (sv != NULL){								// Is a standard status line containing "jitter".
					seqNum = getSeqNum(paramsBuf);

					if (seqNum != lastSeqNum){
						printf("%s", paramsBuf);
					}
				}
				else {											// Is informational or error message
					seqNum += 1;

					if (seqNum != lastSeqNum){
						printf("%s", paramsBuf);
					}
				}
				lastSeqNum = seqNum;
			}
		}

		gettimeofday(&tv1, NULL);

		ts2 = setSyncDelay(dispTime, tv1.tv_usec);
	}
	printf(" Exiting PPS-Client status display\n");
}

/**
 * Responds to the ctrl-c key combination by setting
 * the exit_loop flag. This causes an exit from the
 * showStatusEachSecond() function.
 *
 * @param[in] sig The signal returned by the system.
 */
void INThandler(int sig){
	g.exit_loop = true;
}

/**
 * Checks for and reports on missing arguments in a
 * command line request.
 *
 * @param[in] argc System command line arg
 * @param[in] argv System command line arg
 * @param[in] i The arg index.
 *
 * @returns "true" if an argument is missing,
 * else "false".
 */
bool missingArg(int argc, char *argv[], int i){
	if (i == argc - 1 || argv[i+1][0] == '-'){
		printf("Error: Missing argument for %s.\n", argv[i]);
		return true;
	}
	return false;
}

/**
 * Transmits a data save request to the PPS-Client daemon via
 * data written to a tmpfs shared memory file.
 *
 * @param[in] requestStr The request string.
 * @param[in] filename The shared memory file to write to.
 *
 * @returns 0 on success, else -1 on error.
 */
int daemonSaveArray(const char *requestStr, const char *filename){
	char buf[200];

	int fd = open_logerr(f.arrayData_file, O_CREAT | O_WRONLY | O_TRUNC, "daemonSaveArray()");
	if (fd == -1){
		printf("daemonSaveArray() Open f.arrayData_file failed\n");
		return -1;
	}

	strcpy(buf, requestStr);

	if (filename != NULL){
		strcat(buf, " ");
		strcat(buf, filename);
	}

	int rv = write(fd, buf, strlen(buf) + 1);
	close(fd);
	if (rv == -1){
		sprintf(g.logbuf, "daemonSaveArray() Write to tmpfs memory file failed\n");
		writeToLog(g.logbuf, "daemonSaveArray()");
		return -1;
	}
	return 0;
}

/**
 * Prints a list to the terminal of the command line
 * args for saving data that are recognized by PPS-Client.
 */
void printAcceptedArgs(void){
	printf("Accepts any of these:\n");
	int arrayLen = sizeof(arrayData) / sizeof(struct saveFileData);
	for (int i = 0; i < arrayLen; i++){
		printf("%s\n", arrayData[i].label);
	}
}

/**
 * Reads a command line save data request and either forwards
 * the data to the daemon interface or prints entry errors
 * back to the terminal.
 *
 * @param[in] argc System command line arg
 * @param[in] argv System command line arg
 * @param[in] requestStr The request string.
 *
 * @returns 0 on success, else -1 on error.
 */
int parseSaveDataRequest(int argc, char *argv[], const char *requestStr){

	int arrayLen = sizeof(arrayData) / sizeof(struct saveFileData);

	int i;
	for (i = 0; i < arrayLen; i++){
		if (strcmp(requestStr, arrayData[i].label) == 0){
			break;
		}
	}
	if (i == arrayLen){
		printf("Arg \"%s\" not recognized\n", argv[i+1]);
		printAcceptedArgs();
		return -1;
	}

	char *filename = NULL;
	for (int j = 1; j < argc; j++){
		if (strcmp(argv[j], "-f") == 0){
			if (missingArg(argc, argv, j)){
				printf("Requires a filename.\n");
				return -1;
			}
			strncpy(g.strbuf, argv[j+1], STRBUF_SZ-1);
			g.strbuf[strlen(argv[j+1])] = '\0';
			filename = g.strbuf;
			break;
		}
	}

	if (filename != NULL){
		printf("Writing to file: %s\n", filename);
	}
	else {
		for (i = 0; i < arrayLen; i++){
			if (strcmp(requestStr, arrayData[i].label) == 0){
				printf("Writing to default file: %s\n", arrayData[i].filename);
			}
		}
	}

	if (daemonSaveArray(requestStr, filename) == -1){
		return -1;
	}
	return 0;
}

/**
 * Provides command line access to the PPS-Client daemon.
 *
 * Checks if program is running. If not, returns -1.
 * If an error occurs returns -2. If the program is
 * running then returns 0 and prints a message to that
 * effect.
 *
 * Recognizes data save requests (-s) and forwards these
 * to the daemon interface.
 *
 * If verbose flag (-v) is read then also displays status
 * params of the running program to the terminal.
 *
 * @param[in] argc System command line arg
 * @param[in] argv System command line arg
 *
 * @returns 0 on success, else as described.
 */
int accessDaemon(int argc, char *argv[]){
	bool verbose = false;

	getSharedConfigs();

	if (! ppsIsRunning()){						// If not running,
		remove(f.pidFilename);					// remove a zombie PID filename if one is found
		return 1;								// and return.
	}

	signal(SIGINT, INThandler);					// Set handler to enable exiting with ctrl-c.

	printf("\nPPS-Client v%s is running.\n", version);

	if (argc > 1){

		for (int i = 1; i < argc; i++){
			if (strcmp(argv[i], "-v") == 0){
				verbose = true;
			}
		}
		for (int i = 1; i < argc; i++){
			if (strcmp(argv[i], "-s") == 0){	// This is a save data request.
				if (missingArg(argc, argv, i)){
					printAcceptedArgs();
					return -1;
				}

				if (parseSaveDataRequest(argc, argv, argv[i+1]) == -1){
					return -1;
				}
				break;
			}
		}
	}

	if (verbose){
		printf("Displaying second-by-second state params (ctrl-c to quit):\n");
		showStatusEachSecond();
	}

	return 0;
}

/**
 * Constructs a distribution of time correction values with
 * zero offset at middle index that can be saved to disk for
 * analysis.
 *
 * A time correction is the raw time error passed through a hard
 * limitter to remove jitter and then scaled by the proportional
 * gain constant.
 *
 * @param[in] timeCorrection The time correction value to be
 * accumulated to a distribution.
 */
void buildErrorDistrib(int timeCorrection){
	int len = ERROR_DISTRIB_LEN - 1;
	int idx = timeCorrection + len / 6;

	if (idx < 0){
		idx = 0;
	}
	else if (idx > len){
		idx = len;
	}
	g.errorDistrib[idx] += 1;

	g.errorCount += 1;
}

/**
 * Constructs a distribution of jitter that can be
 * saved to disk for analysis.
 *
 * @param[in] rawError The raw error jitter value
 * to save to the distribution.
 */
void buildJitterDistrib(int rawError){
	int len = JITTER_DISTRIB_LEN - 1;
	int idx = rawError + len / 3;

	if (idx < 0){
		idx = 0;
	}
	else if (idx > len){
		idx = len;
	}
	g.jitterDistrib[idx] += 1;

	g.jitterCount += 1;
}

/**
 * Responds to the SIGTERM signal by starting the exit
 * sequence in the daemon.
 *
 * @param[in] sig The signal from the system.
 */
void TERMhandler(int sig){
	signal(SIGTERM, SIG_IGN);
	sprintf(g.logbuf,"Recieved SIGTERM\n");
	writeToLog(g.logbuf, "TERMhandler()");
	g.exit_requested = true;
	signal(SIGTERM, TERMhandler);
}

/**
 * Catches the SIGHUP signal, causing it to be ignored.
 *
 * @param[in] sig The signal from the system.
 */
void HUPhandler(int sig){
	signal(SIGHUP, SIG_IGN);
}

/**
 * Accumulates the clock frequency offset over the last 5 minutes
 * and records offset difference each minute over the previous 5
 * minute interval. This function is called once each minute.
 *
 * The values of offset difference, g.freqOffsetDiff[], are used
 * to calculate the Allan deviation of the clock frequency offset
 * which is determined so that it can be saved to disk.
 * g.freqOffsetSum is used to calculate the average clock frequency
 * offset in each 5 minute interval so that value can also be saved
 * to disk.
 */
void recordFrequencyVars(void){
	timeval t;
	g.freqOffsetSum += g.freqOffset;

	g.freqOffsetDiff[g.intervalCount] = g.freqOffset - g.lastFreqOffset;

	g.lastFreqOffset = g.freqOffset;
	g.intervalCount += 1;

	if (g.intervalCount >= FIVE_MINUTES){
		gettimeofday(&t, NULL);

		double norm = 1.0 / (double)FREQDIFF_INTRVL;

		double diffSum = 0.0;
		for (int i = 0; i < FREQDIFF_INTRVL; i++){
			diffSum += g.freqOffsetDiff[i] * g.freqOffsetDiff[i];
		}
		g.freqAllanDev[g.recIndex] = sqrt(diffSum * norm * 0.5);

		g.timestampRec[g.recIndex] = t.tv_sec;

		g.freqOffsetRec[g.recIndex] = g.freqOffsetSum * norm;

		g.recIndex += 1;
		if (g.recIndex == NUM_5_MIN_INTERVALS){
			g.recIndex = 0;
		}

		g.intervalCount = 0;
		g.freqOffsetSum = 0.0;
	}
}

/**
 * Each second, records the time correction that was applied to
 * the system clock and also records the last clock frequency
 * offset (in parts per million) that was applied to the system
 * clock.
 *
 * These values are recorded so that they may be saved to disk
 * for analysis.
 *
 * @param[in] timeCorrection The time correction value to be
 * recorded.
 */
void recordOffsets(int timeCorrection){

	g.seq_numRec[g.recIndex2] = g.seq_num;
	g.offsetRec[g.recIndex2] = timeCorrection;
	g.freqOffsetRec2[g.recIndex2] = g.freqOffset;

	g.recIndex2 += 1;
	if (g.recIndex2 >= SECS_PER_10_MIN){
		g.recIndex2 = 0;
	}
}

/**
 * Establishes a connection to the system PPS driver.
 *
 * @param[in] path The driver path. Usually /dev/pps0.
 * @param[out] handle A handle to the driver.
 * @param[out] avail_mode Info needed when accessing the driver.
 *
 * @returns 0 on success else -1 on fail.
 */
int find_source(const char *path, pps_handle_t *handle, int *avail_mode)
{
	pps_params_t params;
	int ret;

	/* Try to find the source by using the supplied "path" name */
	ret = open(path, O_RDWR);

	if (ret < 0) {
		sprintf(g.logbuf, "Unable to open device \"%s\" (%m)\n", path);
		fprintf(stderr, "%s", g.logbuf);
		writeToLog(g.logbuf, "find_source()");
		sprintf(g.logbuf, "Is the PPS driver enabled?\n");
		fprintf(stderr, "%s", g.logbuf);
		writeToLog(g.logbuf, "find_source()");
		return ret;
	}

	/* Open the PPS source (and check the file descriptor) */
	ret = time_pps_create(ret, handle);
	if (ret < 0) {
		sprintf(g.logbuf, "cannot create a PPS source from device "
				"\"%s\" (%m)\n", path);
		writeToLog(g.logbuf, "find_source()");
		return -1;
	}

	/* Find out what features are supported */
	ret = time_pps_getcap(*handle, avail_mode);
	if (ret < 0) {
		sprintf(g.logbuf, "cannot get capabilities (%m)\n");
		writeToLog(g.logbuf, "find_source()");
		return -1;
	}
	if ((*avail_mode & PPS_CAPTUREASSERT) == 0) {
		sprintf(g.logbuf, "cannot CAPTUREASSERT\n");
		writeToLog(g.logbuf, "find_source()");
		return -1;
	}

	/* Capture assert timestamps */
	ret = time_pps_getparams(*handle, &params);
	if (ret < 0) {
		sprintf(g.logbuf, "cannot get parameters (%m)\n");
		writeToLog(g.logbuf, "find_source()");
		return -1;
	}
	params.mode |= PPS_CAPTUREASSERT;
	/* Override any previous offset if possible */
	if ((*avail_mode & PPS_OFFSETASSERT) != 0) {
		params.mode |= PPS_OFFSETASSERT;
		params.assert_offset = offset_assert;
	}
	ret = time_pps_setparams(*handle, &params);
	if (ret < 0) {
		sprintf(g.logbuf, "cannot set parameters (%m)\n");
		writeToLog(g.logbuf, "find_source()");
		return -1;
	}

	return 0;
}

/**
 * Gets the PPS rising edge time from the Linux PPS driver.
 *
 * @param[in] handle The handle to the system PPS driver.
 * @param[in] avail_mode Info for the driver.
 * @param[out] tm The timestamp obtained from the driver.
 *
 * @returns 0 on success, else -1 on driver error.
 */
int readPPSTimestamp(pps_handle_t *handle, int *avail_mode, int *tm)
{
	struct timespec timeout;
	pps_info_t infobuf;
	int ret;

	/* create a zero-valued timeout */
	timeout.tv_sec = 3;
	timeout.tv_nsec = 0;

retry:
	if (*avail_mode & PPS_CANWAIT){ /* waits for the next event */
		ret = time_pps_fetch(*handle, PPS_TSFMT_TSPEC, &infobuf, &timeout);
	}
	else {
		sleep(1);
		ret = time_pps_fetch(*handle, PPS_TSFMT_TSPEC, &infobuf, &timeout);
	}
	if (ret < 0) {
		if (ret == -EINTR) {
			sprintf(g.logbuf, "readPPSTimestamp(): time_pps_fetch() got a signal!\n");
			writeToLog(g.logbuf, "readPPSTimestamp");
			goto retry;
		}

		return -1;
	}

	if (g.ppsPhase == 0){
		tm[0] = (int)infobuf.assert_timestamp.tv_sec;
		tm[1] = (int)(infobuf.assert_timestamp.tv_nsec / 1000);
	}
	else {
		tm[0] = (int)infobuf.clear_timestamp.tv_sec;
		tm[1] = (int)(infobuf.clear_timestamp.tv_nsec / 1000);
	}

	return 0;
}

int getRootHome(void){
	char buf[100];
	char cmdStr[100];

	memset(cmdStr, 0, 100);
	strcpy(cmdStr, "echo $HOME > ");
	strcat(cmdStr, f.home_file);

	sysCommand(cmdStr);

	int fd = open(f.home_file, O_RDONLY);
	if (fd == -1){
		sprintf(g.logbuf, "getRootHome(): Unable to open file %s\n", "./pps");
		writeToLog(g.logbuf, "getRootHome()");
		return -1;
	}

	memset(buf, 0, 100);

	int rv = read(fd, buf, 99);
	if (rv == -1){
		close(fd);
		return -1;
	}
	close(fd);

	char *pbuf = buf;
	while (*pbuf != '/'){
		pbuf += 1;
	}

	char *pbufEnd = pbuf;
	while(*pbufEnd != '\n'){
		pbufEnd += 1;
	}
	*pbufEnd = '\0';

	strcpy(f.integral_state_file, pbuf);
	strcat(f.integral_state_file, integral_state_file);

	remove(f.home_file);

	return 0;
}

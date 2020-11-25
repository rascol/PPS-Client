/*
 * pps-timer.cpp
 *
 * Created on: Nov 1, 2020
 * Copyright (C) 2020 Raymond S. Connell
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sched.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <signal.h>

#define NSECS_PER_SEC 1000000000
#define SECS_PER_MINUTE 60
#define SECS_PER_DAY 86400
#define SAMPLES_PER_USEC 2
#define SAMPLE_INTVL (1.0 / (double)SAMPLES_PER_USEC)
#define PROBE_TIME -15.0
#define MSGBUF_SZ 10000

#define TIME_DISTRIB_LEN 51
#define MAX_DISTRIB_LEN 251
#define PID_FILE_SZ 10000

const char *version = "pps-timer v1.0.0";

const char *time_distrib_file = "/var/local/pps-time-distrib-forming";
const char *last_time_distrib_file = "/var/local/pps-time-distrib";
const char *displayParams_file = "/run/shm/pps-display-params";							//!< Temporary file storing params for the status display

char paramsBuf[MSGBUF_SZ];

int sysCommand(const char *cmd){
	int rv = system(cmd);
	if (rv == -1 || WIFEXITED(rv) == false){
		printf("System command failed: %s\n", cmd);
		return -1;
	}
	return 0;
}

struct interruptTimerGlobalVars {
	int seq_num;
	struct timespec tm;
	char strbuf[200];
	char msgbuf[200];
	double probeTime;
	int samplesPerUsec;
	double sampleIntvl;
	int timeCount;
	int lastTimeCount;
	int timeLowestVal;

	int odd_even;

	int timeDistrib[MAX_DISTRIB_LEN];
	int timeDistribLen;

	int lastTimeFileno;
	int exit_requested;
} g;

/**
 * Constructs an error message.
 */
void couldNotOpenMsgTo(char *msgbuf, const char *filename){
	strcpy(msgbuf, "ERROR: could not open \"");
	strcat(msgbuf, filename);
	strcat(msgbuf, "\": ");
	strcat(msgbuf, strerror(errno));
	strcat(msgbuf, "\n");
}

/**
 * Opens a file with error printing and sets standard
 * file permissions for O_CREAT.
 *
 * @param[in] filename The file to open.
 * @param[in] flags The file open flags.
 *
 * @returns The file descriptor.
 */
int open_logerr(const char* filename, int flags){
	mode_t mode = S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH;
	int fd;
	if ((flags & O_CREAT) == O_CREAT){
		fd = open(filename, flags, mode);
		if (fd == -1){
			couldNotOpenMsgTo(g.msgbuf, filename);
			printf("%s\n", g.msgbuf);
			return -1;
		}
	}
	else {
		fd = open(filename, flags);
		if (fd == -1){
			couldNotOpenMsgTo(g.msgbuf, filename);
			printf("%s\n", g.msgbuf);
			return -1;
		}
	}
	return fd;
}

/**
 * Writes an accumulating statistical distribution to disk and
 * rolls over the accumulating data to a new file every epoch
 * counts and begins a new distribution file. An epoch is
 * 86,400 counts.
 *
 * @param[in] distrib The int array containing the distribution.
 * @param[in] len The length of the array.
 * @param[in] scaleZero The array index corresponding to distribution zero.
 * @param[in] count The current number of samples in the distribution.
 * @param[out] last_epoch The saved count of the previous epoch.
 * @param[in] distrib_file The filename of the last completed
 * distribution file.
 * @param[in] last_distrib_file The filename of the currently
 * forming distribution file.
 */
void writeDistribution(int factor, double scaleIncr, int distrib[], int distriblen, int scaleZero, int count,
		int *last_epoch, const char *distrib_file, const char *last_distrib_file){
	int rv;

	remove(distrib_file);
	int fd = open_logerr(distrib_file, O_CREAT | O_WRONLY | O_APPEND);
	if (fd == -1){
		return;
	}
	for (int i = 0; i < distriblen; i++){
		double scaleVal = (i + scaleZero) * scaleIncr;
		if (scaleIncr == 1.0){
			sprintf(g.strbuf, "%3.0lf %d\n", scaleVal, distrib[i]);
		}
		else {
			sprintf(g.strbuf, "%5.1lf %d\n", scaleVal, distrib[i]);
		}

		rv = write(fd, g.strbuf, strlen(g.strbuf));
		if (rv == -1){
			printf("Write to %s failed.\n", distrib_file);
			break;
		}
	}
	close(fd);

	int epoch = count / (SECS_PER_DAY / factor);
	if (epoch != *last_epoch ){
		*last_epoch = epoch;
		remove(last_distrib_file);
		rename(distrib_file, last_distrib_file);
		memset(distrib, 0, distriblen * sizeof(int));
	}
}

/**
 * Writes a distribution to disk approximately every other
 * minute containing 60 additional delay samples recorded
 * at the ccurrance of the pulse. The distribution rolls
 *  over to a new file every 24 hours.
 */
void writeTimeDistribFile(void){
	if (g.timeCount % SECS_PER_MINUTE == 0 && g.timeCount != g.lastTimeCount){
		g.lastTimeCount = g.timeCount;

		writeDistribution(1, g.sampleIntvl, g.timeDistrib, g.timeDistribLen, g.timeLowestVal, g.timeCount,
				&g.lastTimeFileno, time_distrib_file, last_time_distrib_file);
	}
}


/**
 * Constructs a distribution of values with a specified
 * value as the first value in the distribution.
 *
 * @param[in] distribVal The value to save to save to
 * the distribution.
 *
 * @param[in] zeroVal The distrib value that is entered
 * into distrib[0].
 *
 * @param[in/out] distrib The pulse distribution to create
 *
 * @param[in] distriblen The number of bins in the distribution
 *
 * @param[in/out] count The count of recorded pulse times
 */
void buildDistrib(int distribVal, int zeroVal, int distrib[], int distriblen, int *count){

	int idx = distribVal - zeroVal;

	if (idx < 0){
		idx = 0;
	}
	else if (idx > distriblen - 1){
		idx = distriblen - 1;
	}
	distrib[idx] += 1;

	*count += 1;
}

/**
 * Reads the major number assigned to pps-timer
 * from "/proc/devices" as a string which is
 * returned in the majorPos char pointer. This
 * value is used to load the hardware driver that
 * pps-timer requires.
 */
char *copyMajorTo(char *majorPos){

	struct stat stat_buf;

	const char *filename = "/run/shm/proc_devices";

	int rv = sysCommand("cat /proc/devices > /run/shm/proc_devices"); 	// "/proc/devices" can't be handled like
	if (rv == -1){														// a normal file so we copy it to a file.
		return NULL;
	}

	int fd = open(filename, O_RDONLY);
	if (fd == -1){
		return NULL;
	}

	fstat(fd, &stat_buf);
	int sz = stat_buf.st_size;

	char *fbuf = new char[sz+1];

	rv = read(fd, fbuf, sz);
	if (rv == -1){
		close(fd);
		remove(filename);
		delete(fbuf);
		return NULL;
	}
	close(fd);
	remove(filename);

	fbuf[sz] = '\0';

	char *pos = strstr(fbuf, "pps-timer");
	if (pos == NULL){
		printf("Can't find pps-timer in \"/run/shm/proc_devices\"\n");
		delete fbuf;
		return NULL;
	}
	char *end = pos - 1;
	*end = 0;

	pos -= 2;
	char *pos2 = pos;
	while (pos2 == pos){
		pos -= 1;
		pos2 = strpbrk(pos,"0123456789");
	}
	strcpy(majorPos, pos2);

	delete fbuf;
	return majorPos;
}

/**
 * Loads the hardware driver required by pps-timer which
 * is expected to be available in the file:
 * "/lib/modules/'uname -r'/extra/pps-timer.ko".
 */
int driver_load(void){

	memset(g.strbuf, 0, 200 * sizeof(char));

	char *insmod = g.strbuf;

	strcpy(insmod, "/sbin/insmod /lib/modules/`uname -r`/extra/pps-timer.ko");

	int rv = sysCommand("rm -f /dev/pps-timer");			// Clean up any old device files.
	if (rv == -1){
		return -1;
	}

	rv = sysCommand(insmod);									// Issue the insmod command
	if (rv == -1){
		return -1;
	}

	char *mknod = g.strbuf;
	strcpy(mknod, "mknod /dev/pps-timer c ");
	char *major = copyMajorTo(mknod + strlen(mknod));
	if (major == NULL){											// No major found! insmod failed.
		printf("driver_load() error: No major found!\n");
		sysCommand("/sbin/rmmod pps-timer");
		return -1;
	}
	strcat(mknod, " 0");

	rv = sysCommand(mknod);										// Issue the mknod command
	if (rv == -1){
		return -1;
	}

	rv = sysCommand("chgrp root /dev/pps-timer");
	if (rv == -1){
		return -1;
	}
	rv = sysCommand("chmod 664 /dev/pps-timer");
	if (rv == -1){
		return -1;
	}

	return 0;
}

/**
 * Unloads the pps-timer kernel driver.
 */
void driver_unload(void){
	sysCommand("/sbin/rmmod pps-timer");
	sysCommand("rm -f /dev/pps-timer");
}

/**
 * Sets a nanosleep() time delay equal to the time remaining
 * in the second from the time recorded as fracSecNow plus an
 * adjustment value of timeAt in nanoseconds.
 *
 * @param[in] timeAt Adjustment value in nanoseconds
 * @param[in] fracSecNow The fractional second of the
 * current time in nanoseconds.
 */
struct timespec setSyncDelay(int timeAt, int fracSecNow){

	struct timespec ts2;

	int timerVal = NSECS_PER_SEC + timeAt - fracSecNow;

	if (timerVal >= NSECS_PER_SEC){
		ts2.tv_sec = 1;
		ts2.tv_nsec = timerVal - NSECS_PER_SEC;
	}
	else if (timerVal < 0){
		ts2.tv_sec = 0;
		ts2.tv_nsec = NSECS_PER_SEC + timerVal;
	}
	else {
		ts2.tv_sec = 0;
		ts2.tv_nsec = timerVal;
	}

	return ts2;
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
	if (i == argc - 1){
		printf("Error: Missing argument for %s.\n", argv[i]);
		return true;
	}
	return false;
}

/**
 * Attempts to move all running processes except
 * pps-client and pps-timer to processor cores 1 - 3.
 *
 * Not all will be movable. Error messages are generated
 * for the ones that can't be moved but those messages
 * are suppressed.
 */
int assignProcessorAffinity(void){
	int rv;
	char cmdstr[50];

	printf("Aasigning processor affinity:\n");

	const char *cmd = "taskset -cp 2-3 ";
	const char *end = " > /dev/null 2>&1";							// "> /dev/null 2>&1" suppresses all messages

	rv = sysCommand("ps --no-headers -eo pid > /dev/shm/pid.txt");	// Save PIDs only of all running processes
	if (rv == -1){
		return rv;
	}

	int fd = open("/dev/shm/pid.txt", O_RDONLY);					// Open the PID file
	if (fd == -1){
		return fd;
	}

	char *fbuf = new char[PID_FILE_SZ];

	rv = read(fd, fbuf, PID_FILE_SZ-1);								// Read PID file into fbuf

	close(fd);

	if (rv == -1){
		delete fbuf;
		return rv;
	}

	char *pbuf = strtok(fbuf, "\n");								// Locate the first CR

	pbuf = strtok(NULL, "\n\0");
	memset(cmdstr, 0, 50);
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

			memset(cmdstr, 0, 50);
			strcpy(cmdstr, cmd);
			strcat(cmdstr, pbuf);
			strcat(cmdstr, end);

			rv = sysCommand(cmdstr);							// Attempt to put each running processes on Core 2 or 3
			if (rv == -1){
				delete fbuf;
				return rv;
			}
		}
	}

	sysCommand("taskset -cp 0 `pidof pps-client`");
	sysCommand("taskset -cp 0 `pidof pps-timer`");
	sysCommand("taskset -cp 1 `pidof pulse-generator` > /dev/null 2>&1");
//	sysCommand("taskset -cp 1 `pidof pulse-generator`");
	printf("\n");

	delete fbuf;
	return 0;
}

bool jitterIsAcceptable(){
	struct stat stat_buf;
	int jitter;

	int fd = open(displayParams_file, O_RDONLY);
	if (fd == -1){
		printf("jitterIsAcceptable(): Could not open displayParams_file");
		printf("%s", displayParams_file);
		printf("\n");
		return false;
	}

	fstat(fd, &stat_buf);
	int sz = stat_buf.st_size;

	if (sz >= MSGBUF_SZ){
		printf("jitterIsAcceptable(): Buffer is too small. Size is %d\n", sz);
		close(fd);
		return false;
	}
	if (sz == 0){
		printf("jitterIsAcceptable(): Bad file read. Size is %d\n", sz);
		close(fd);
		return false;
	}

	int rv = read(fd, paramsBuf, sz);
	close(fd);
	if (rv == -1){
		printf("jitterIsAcceptable(): Read paramsBuf failed with error: %s\n", strerror(errno));
		return false;
	}

	paramsBuf[sz] = '\0';

	char *sv = strstr(paramsBuf, "jitter");
	if (sv != NULL){								// Is a standard status line containing "jitter".
		sscanf(sv, "jitter: %d ", &jitter);
		if (jitter < 3 && jitter > -3){
			return true;
		}
	}
	else {											// Is an informational or error message
		return false;
	}

	return false;
}

/**
 * Responds to the SIGTERM on kill by starting
 * the exitsequence.
 *
 * @param[in] sig The signal from the system.
 */
void TERMhandler(int sig){
	signal(SIGTERM, SIG_IGN);
	g.exit_requested = true;
	signal(SIGTERM, TERMhandler);
}

void SIGINThandler(int sig){
	signal(SIGINT, SIG_IGN);
	g.exit_requested = true;
}

void detectTermination(void){
	signal(SIGTERM, TERMhandler);					// Handler for kill.
	signal(SIGINT, SIGINThandler);					// Handler for ctl-c
	signal(SIGHUP, SIG_IGN);						// Ignore SIGHUP which can be received
	return;											// if a terminal disconnects.
}

int main(int argc, char *argv[]){

	int pulseStart1 = 0;
	int writeData[4];
	int readData[3];
	const char *deviceName = "/dev/pps-timer";
	struct timespec ts1, ts2;
	struct sched_param param;

	int fd;
	int rv;
	int pps_time = 0;
	double f_pps_time;

	memset(&g, 0, sizeof(struct interruptTimerGlobalVars));

	g.samplesPerUsec = SAMPLES_PER_USEC;
	g.probeTime = PROBE_TIME;
	g.timeLowestVal = (int)g.probeTime * g.samplesPerUsec;
	g.sampleIntvl = SAMPLE_INTVL;
	g.timeDistribLen = TIME_DISTRIB_LEN;

	if (argc > 1){
		for (int i = 1; i < argc; i++){

			if (strcmp(argv[i], "-t") == 0){
				if (missingArg(argc, argv, i)){
					goto info;
				}
				sscanf(argv[i+1], "%lf", &g.probeTime);
				g.probeTime -= 15.0;
			}
			if (strcmp(argv[i], "-dr") == 0){
				if (missingArg(argc, argv, i)){
					goto info;
				}
				sscanf(argv[i+1], "%d", &g.samplesPerUsec);
				if (g.samplesPerUsec > 10){
					g.samplesPerUsec = 10;
				}
				if (g.samplesPerUsec < 1){
					g.samplesPerUsec = 1;
				}
				g.sampleIntvl = 1.0 / (double)g.samplesPerUsec;
				g.timeDistribLen = 25 * g.samplesPerUsec + 1;
			}
		}
		g.timeLowestVal = (int)g.probeTime * g.samplesPerUsec;
	}
	goto start;

info:
	printf("Usage:\n");
	printf("Calling pps-timer with no arguments causes\n");
	printf("it to begin timing the PPS interrupt.\n\n");

	printf("To time an interrupt expected at time t\n");
	printf("into each second, provide the interrupt time\n");
	printf("in microseconds with,\n");
	printf("  -t <time>\n\n");

	printf("To adjust the time resolution of the generated time\n");
	printf("distribution in samples per microsecond (range: 1 to 10)\n");
	printf("use,\n");
	printf(" -dr <samplesPerUsec>\n\n");
	return 0;

start:

	int tryCount = 0;
	char timeStr[100];
	const char *timefmt = "%F %H:%M:%S";

	if (geteuid() != 0){
		printf("Requires superuser privileges. Please sudo this command.\n");
		return 1;
	}

	printf("%s\n", version);
														// Process must be run as
	param.sched_priority = 99;							// root to change priority.
	sched_setscheduler(0, SCHED_FIFO, &param);			// Else, this has no effect.

	driver_load();

	fd = open(deviceName, O_RDWR);						// Open the pps-timer device driver.
	if (fd == -1){
		printf("pps-timer: Driver is not loaded. Exiting.\n");
		return 1;
	}

	assignProcessorAffinity();



	int latency = 250000;								// nanoseconds

	double probeTime = 1000.0 * g.probeTime;			// nanoseconds

	pulseStart1 = (int)probeTime - latency;				// This will start the driver about 250 microsecs ahead of the
														// write time thus allowing about 250 usec coming out of sleep.
	int writeTime;

	for (;;){											// Run until kill or ctl-c
		detectTermination();
														// if a terminal disconnects.
		if (g.exit_requested == true){
			break;
		}

		clock_gettime(CLOCK_REALTIME, &ts1);
		ts2 = setSyncDelay(pulseStart1, ts1.tv_nsec);	// Sleep to pulseStart1.
		nanosleep(&ts2, NULL);

		detectTermination();

		if (g.exit_requested == true){
			break;
		}

		writeTime = (int)probeTime;						// In nanoseconds

		writeData[0] = 0;								// Identify requested operation to driver
		writeData[1] = 0;
		writeData[2] = writeTime;						// Set the pulse time in nanoseconds into the second
		writeData[3] = g.seq_num;						// Send for debug purposes

		rv = write(fd, writeData, 4 * sizeof(int));		// Request a write at probeTime.
		if (rv == -1){
			printf("Write to %s failed.\n", deviceName);
			break;
		}

		if (rv == 0){
			clock_gettime(CLOCK_REALTIME, &ts1);
			strftime(timeStr, 100, timefmt, localtime((const time_t*)(&(ts1.tv_sec))));
			if (tryCount < 20){
				printf("%s %d  PPS not detected.\n", timeStr, g.seq_num);
			}
			else {
				printf("%s %d  PPS not detected. Is PPS-Client running?\n", timeStr, g.seq_num);
			}

			tryCount += 1;
			g.seq_num += 1;
			continue;
		}

		tryCount = 0;

		readData[0] = 0;
		rv = read(fd, readData, 3 * sizeof(int));
		if (rv == -1){
			printf("Read from %s failed.\n", deviceName);
			break;
		}

		if (rv > 0){
			pps_time = readData[2];

			if (pps_time > 900000000){
				pps_time = -(1000000000 - pps_time);
			}

			f_pps_time = (double)pps_time * 0.001;

			if (jitterIsAcceptable() == true){
				buildDistrib((int)round((double)g.samplesPerUsec * f_pps_time), g.timeLowestVal, g.timeDistrib, g.timeDistribLen, &g.timeCount);

				clock_gettime(CLOCK_REALTIME, &ts1);
				g.tm = ts1;
				strftime(timeStr, 100, timefmt, localtime((const time_t*)(&(ts1.tv_sec))));

				fprintf(stdout, "%s %d  pps_time: %5.2lf usecs\n", timeStr, g.seq_num, f_pps_time);
				fflush(stdout);
			}
			else {
				clock_gettime(CLOCK_REALTIME, &ts1);
				g.tm = ts1;
				strftime(timeStr, 100, timefmt, localtime((const time_t*)(&(ts1.tv_sec))));

				fprintf(stdout, "%s %d  pps_time:  Too much PPS jitter\n", timeStr, g.seq_num);
				fflush(stdout);
			}
		}
		else {
			clock_gettime(CLOCK_REALTIME, &ts1);
			strftime(timeStr, 100, timefmt, localtime((const time_t*)(&(ts1.tv_sec))));
			fprintf(stdout, "%s Read failed.\n", timeStr);
			fflush(stdout);
		}

		if (g.seq_num > 10){
			writeTimeDistribFile();
		}

		g.seq_num += 1;
	}

	close(fd);											// Close the pps-timer device driver.

	driver_unload();
	printf("\nUnloaded driver\n");

	return 0;
}

/**
 * @file pps-client.h v2.0.0 updated on 27 Mar 2020
 *
 * @brief This file contains includes, defines and structures for PPS-Client.
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

#ifndef PPS_CLIENT_H_
#define PPS_CLIENT_H_

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/timex.h>
#include <sys/time.h>
#include <math.h>
#include <sys/types.h>
#include <errno.h>
#include <poll.h>
#include <sys/mman.h>
#include "timepps.h"
#include <inttypes.h>

#define USECS_PER_SEC 1000000
#define SECS_PER_MINUTE 60
#define SECS_PER_5_MIN 300
#define SECS_PER_10_MIN 600
#define SECS_PER_HOUR 3600
#define SECS_PER_DAY 86400
#define NUM_5_MIN_INTERVALS 288				//!< Number of five minute intervals in 24 hours
#define FIVE_MINUTES 5
#define PER_MINUTE (1.0 / (double)SECS_PER_MINUTE)
#define SETTLE_TIME (2 * SECS_PER_10_MIN)	//!< The PPS-Client up time required before saving performance data
#define INV_GAIN_1 1						//!< Controller inverse proportional gain constant during active controller operation
#define INV_GAIN_0 4						//!< Controller inverse proportional gain constant at startup
#define INTEGRAL_GAIN 0.63212				//!< Controller integral gain constant in active controller operation
#define FREQDIFF_INTRVL 5					//!< The number of minutes between Allan deviation samples of system clock frequency correction
#define PPS_WINDOW 500						//!< WaitForPPS delay loop time window in which to look for a PPS
#define PTHREAD_STACK_REQUIRED 196608		//!< Stack space requirements for threads

#define ZERO_OFFSET_RPI3 7
#define ZERO_OFFSET_RPI4 4

#define OFFSETFIFO_LEN 80					//!< Length of \b G.correctionFifo which contains the data used to generate \b G.avgCorrection.
#define NUM_INTEGRALS 10					//!< Number of integrals used by \b makeAverageIntegral() to calculate the one minute clock frequency correction
#define PER_NUM_INTEGRALS (1.0 / (double)NUM_INTEGRALS)	//!< Inverse of NUM_INTEGRALS

#define ADJTIMEX_SCALE 65536.0				//!< Frequency scaling required by \b adjtimex().

#define RAW_ERROR_ZERO  20					//!< Index corresponding to rawError == 0 in \b buildRawErrorDistrib().
#define RAW_ERROR_DECAY 0.98851				//!< Decay rate for \b G.rawError samples (1 hour half life)

#define INTERRUPT_LOST 15					//!< Number of consecutive lost interrupts at which a warning starts

#define MAX_SERVERS 4						//!< Maximum number of NIST time servers to use
#define CHECK_TIME 1024						//!< Interval between Internet time checks (about 17 minutes)
#define BLOCK_FOR_10 10						//!< Blocks detection of external system clock changes for 10 seconds
#define BLOCK_FOR_3 3						//!< Blocks detection of external system clock changes for 3 seconds
#define CHECK_TIME_SERIAL 10				//!< Interval between serial port time checks (10 seconds)

#define MAX_SPIKES 60						//!< Maximum microseconds to suppress a burst of continuous positive jitter
#define MAX_SPIKE_LEVEL 1000000				//!< An initialization value for \b G.minSustainedDelay
#define CLK_CHANGED_LEVEL 1000

#define LARGE_SPIKE 80						//!< Level above which spikes are are disruptive
#define NOISE_ACCUM_RATE 0.1				//!< Sets the rate at which \b G.noiseLevel adjusts to \b G.rawError
#define NOISE_LEVEL_MIN 4					//!< The minimum level at which interrupt delays are delay spikes.
#define SLEW_LEN 10							//!< The slew accumulator (slewAccum) update interval
#define SLEW_MAX 300						//!< Jitter slew value below which the controller will begin to frequency lock.

#define MAX_LINE_LEN 50
#define STRBUF_SZ 1000
#define LOGBUF_SZ 1000
#define MSGBUF_SZ 1000
#define NIST_MSG_SZ 200
#define CONFIG_FILE_SZ 10000

#define NUM_PARAMS 5
#define ERROR_DISTRIB_LEN 121
#define JITTER_DISTRIB_LEN 181
#define INTRPT_DISTRIB_LEN 121

#define HARD_LIMIT_NONE 32768
#define HARD_LIMIT_1024 1024
#define HARD_LIMIT_4 4
#define HARD_LIMIT_1 1

#define HIGH 1
#define LOW 0

#define MAX_CONFIGS 32

#define ERROR_DISTRIB 1				// Configuration file Keys
#define ALERT_PPS_LOST 2
#define JITTER_DISTRIB 4
#define EXIT_LOST_PPS 8
#define PPS_GPIO 16
#define OUTPUT_GPIO 32
#define INTRPT_GPIO 64
#define NIST 128
#define SERIAL 256
#define SERIAL_PORT 512
#define EXECDIR 1024
#define SERVICEDIR 2048
#define CONFIGDIR 4096
#define DOCDIR 8192
#define RUNDIR 16384
#define SHMDIR 32768
#define TSTDIR 65536
#define LOGDIR 131072
#define PPSDELAY 262144
#define MODULEDIR 524288
#define PPSDEVICE 1048576
#define PPSPHASE 2097152
#define PROCDIR 4194304
#define SEGREGATE 8388608


/*
 * Struct for passing arguments to and from threads
 * querying time servers.
 */
struct timeCheckParams {
	pthread_t *tid;									//!< Thread id
	pthread_attr_t attr;							//!< Thread attribute object
	int serverIndex;								//!< Identifying index from the list of active NIST servers
	int *serverTimeDiff;							//!< Time difference between local time and server time
	char **ntp_server;								//!< The active NIST server list when NIST is used
	char *serialPort;								//!< The serial port filename when serial time is used
	char *buf;										//!< Space for the active NIST server list
	bool doReadSerial;								//!< Flag to read serial messages from serial port
	char *strbuf;									//!< Space for messages and query strings
	char *logbuf;									//!< Space for returned log messages
	bool *threadIsBusy;								//!< True while thread is waiting for or processing a time query
	int rv;											//!< Return value of thread
													//!< Struct for passing arguments to and from threads querying NIST time servers or GPS receivers.
	char *gmtTime_file;
	char *nistTime_file;
};

/*
 * Struct for program-wide global variables.
 */
struct G {
	int nCores;										//!< If PPS-Client is segregated, identifies the number of processor cores.
	int useCore;									//!< If PPS-Client is segregated, the core on which it runs.
	int cpuVersion;									//!< The principle CPU version number for Raspberry Pi processors else 0.

	bool isVerbose;									//!< Enables continuous printing of PPS-Client status params when "true".

	bool configWasRead;								//!< True if pps-client.conf was read at least once.

	unsigned int seq_num;							//!< Advancing count of the number of PPS interrupt timings that have been received.

	int ppsTimestamp;								//!< Fractional second value of the PPS timestamp from the kernel driver

	bool isControlling;								//!< Set "true" by \b getAcquireState() when the control loop can begin to control the system clock frequency.
	unsigned int activeCount;						//!< Advancing count of active (not skipped) controller cycles once \b G.isControlling is "true".

	bool interruptReceived;							//!< Set "true" when \b makeTimeCorrection() processes an interrupt time from the Linux PPS device driver.
	bool interruptLost;								//!< Set "true" when a PPS interrupt time fails to be received.
	int interruptLossCount;							//!< Records the number of consecutive lost PPS interrupt times.

	struct timeval t;								//!< Time of system response to the PPS interrupt received from the Linux PPS device driver.

	int tm[6];										//!< Returns the timestamp from the Linux PPS device driver as a pair of ints.

	int t_now;										//!< Rounded seconds of current time reported by \b gettimeofday().
	int t_count;									//!< Rounded seconds counted at the time of \b G.t_now.
	double t_mono_now;								//!< Current monotonic time
	double t_mono_last;								//!< Last recorded monotonic time

	int	zeroOffset;									//!< System time delay between rising edge and timestamp of the PPS interrupt including
													//!< settling offset in microseconds. Assigned as a constant in pps-client.conf.
	double noiseLevel;								//!< PPS time delay value beyond which a delay is defined to be a delay spike.
	int ppsPhase;									//!< Accounts for a possible hardware inversion of the PPS signal.

	int rawError;									//!< Signed difference: \b G.ppsTimestamp - \b G.zeroOffset in \b makeTimeCorrection().

	double rawErrorDistrib[ERROR_DISTRIB_LEN];		//!< The distribution of rawError values accumulated in \b buildRawErrorDistrib().
	unsigned int ppsCount;							//!< Advancing count of \b G.rawErrorDistrib[] entries made by \b buildRawErrorDistrib().

	int nDelaySpikes;								//!< Current count of continuous delay spikes made by \b detectDelaySpike().
	bool isDelaySpike;								//!< Set "true" by \b detectDelaySpike() when \b G.rawError exceeds \b G.noiseLevel.
	int minSustainedDelay;							//!< The observed minimum delay value of a sustained sequence of delay spikes
	bool clockChanged;								//!< Set true if an external clock change is detected.

	double slewAccum;								//!< Accumulates \b G.rawError in \b getTimeSlew() and is used to determine \b G.avgSlew.
	int slewAccum_cnt;								//!< Count of the number of times \b G.rawError has been summed into \b G.slewAccum.
	double avgSlew;									//!< Average slew value determined by \b getTimeSlew() from the average of \b G.slewAccum each time \b G.slewAccum_cnt reaches \b SLEW_LEN.
	bool slewIsLow;									//!< Set to "true" in \b getAcquireState() when \b G.avgSlew is less than \b SLEW_MAX. This is a precondition for \b getAcquireState() to set \b G.isControlling to "true".

	int zeroError;									//!< The controller error resulting from removing jitter noise from \b G.rawError in \b removeNoise().
	int hardLimit;									//!< An adaptive limit value determined by \b setHardLimit() and applied to \b G.rawError by \b clampJitter() as the final noise reduction step to generate \b G.zeroError.
	bool clampAbsolute;								//!< Hard limit relative to zero if true else relative to average \b G.rawError.

	int invProportionalGain;						//!< Controller proportional gain configured inversely to use as an int divisor.
	int timeCorrection;								//!< Time correction value constructed in \b makeTimeCorrection().
	struct timex t3;								//!< Passes \b G.timeCorrection to the system function \b adjtimex() in \b makeTimeCorrection().

	double avgCorrection;							//!< A one-minute rolling average of \b G.timeCorrection values generated by \b getMovingAverage().
	int correctionFifo[OFFSETFIFO_LEN];				//!< Contains the \b G.timeCorrection values from over the previous 60 seconds.
	int correctionFifoCount;						//!< Signals that \b G.correctionFifo contains a full count of \b G.timeCorrection values.
	int correctionAccum;							//!< Accumulates \b G.timeCorrection values from \b G.correctionFifo in \b getMovingAverage() in order to generate \b G.avgCorrection.

	double integral[NUM_INTEGRALS];					//!< Array of integrals constructed by \b makeAverageIntegral().
	double avgIntegral;								//!< One-minute average of the integrals in \b G.integral[].
	int integralCount;								//!< Counts the integrals formed over the last 10 controller cycles and signals when all integrals in \b G.integral have been constructed.
	int correctionFifo_idx;							//!< Advances \b G.correctionFifo on each controller cycle in \b integralIsReady() which returns "true" every 60 controller cycles.

	double integralGain;							//!< Current controller integral gain.
	double integralTimeCorrection;					//!< Integral or average integral of \b G.timeCorrection returned by \b getIntegral();
	double freqOffset;								//!< System clock frequency correction calculated as \b G.integralTimeCorrection * \b G.integralGain.

	bool doNISTsettime;
	bool nistTimeUpdated;
	int consensusTimeError;							//!< Consensus value of whole-second time corrections for DST or leap seconds from Internet NIST servers.

	bool doSerialsettime;
	bool serialTimeUpdated;
	int serialTimeError;							//!< Error reported by GPS serial port.S

	char linuxVersion[20];							//!< Array for recording the Linux version.
	/**
	 * @cond FILES
	 */
	int startingFromRestore;

	char logbuf[LOGBUF_SZ];
	char msgbuf[MSGBUF_SZ];
	char savebuf[MSGBUF_SZ];
	char strbuf[STRBUF_SZ];
	char *configVals[MAX_CONFIGS];

	bool exit_requested;
	bool exitOnLostPPS;
	bool exit_loop;

	int blockDetectClockChange;

	int recIndex;
	int recIndex2;

	time_t pps_t_sec;
	int pps_t_usec;

	unsigned int config_select;

	int intervalCount;

	int jitter;

	int seq_numRec[SECS_PER_10_MIN];

	double lastFreqOffset;
	double freqOffsetSum;
	double freqOffsetDiff[FREQDIFF_INTRVL];

	unsigned int lastActiveCount;

	int delayLabel[NUM_PARAMS];

	int interruptDistrib[INTRPT_DISTRIB_LEN];
	int interruptCount;

	int jitterDistrib[JITTER_DISTRIB_LEN];
	int jitterCount;

	int errorDistrib[ERROR_DISTRIB_LEN];
	int errorCount;
	bool queryWait;

	double freqAllanDev[NUM_5_MIN_INTERVALS];
	double freqOffsetRec[NUM_5_MIN_INTERVALS];
	double freqOffsetRec2[SECS_PER_10_MIN];
	__time_t timestampRec[NUM_5_MIN_INTERVALS];
	int offsetRec[SECS_PER_10_MIN];
	char serialPort[50];
	char configBuf[CONFIG_FILE_SZ];
	/**
	 * @endcond
	 */
};													//!< Struct for program-wide global variables showing those important to the controller.

/**
 * @cond FILES
 */


time_t getServerTime(const char *, int, char *, char *);
int allocInitializeNISTThreads(timeCheckParams *);
void freeNISTThreads(timeCheckParams *);
void makeNISTTimeQuery(timeCheckParams *);

int allocInitializeSerialThread(timeCheckParams *tcp);
void freeSerialThread(timeCheckParams *tcp);
int makeSerialTimeQuery(timeCheckParams *tcp);

/**
 * Struct to hold associated data for PPS-Client command line
 * save data requests with the -s flag.
 */
struct saveFileData {
	const char *label;			//!< Command line identifier
	void *array;				//!< Array to hold data to be saved
	const char *filename;		//!< Filename to save data
	int arrayLen;				//!< Length of the array in array units
	int arrayType;				//!< Array type: 1 - int, 2 - double
	int arrayZero;				//!< Array index of data zero.
};

/**
 * PPS-Client internal files
 */
struct ppsFiles {
	char last_distrib_file[100];
	char distrib_file[100];
	char last_jitter_distrib_file[100];
	char jitter_distrib_file[100];
	char log_file[100];
	char old_log_file[100];
	char pidFilename[100];
	char config_file[100];
	char assert_file[100];
	char displayParams_file[100];
	char arrayData_file[100];
	char pps_device[100];
	char module_file[100];
	char pps_msg_file[100];
	char linuxVersion_file[50];
	char gmtTime_file[50];
	char nistTime_file[50];
	char integral_state_file[50];
	char home_file[50];
	char cpuinfo_file[50];
};

struct intPair {
	int val;
	int nVals;
};

/**
 * Construcor for an ordered list.
 */
class List {

private:
	int ln;
	int size;
	int count;

	void insert(int idx, int val){
		for (int i = ln; i >= idx; i--){
			lst[i+1] = lst[i];
		}
		lst[idx].val = val;
		lst[idx].nVals = 1;
		ln += 1;
	}

public:
	struct intPair *lst;

	void clear(void){
		for (int i = 0; i < size; i++){
			lst[i].val = 0;
			lst[i].nVals = 0;
		}
		ln = 0;
		count = 0;
	}

	List(int sz){
		size = sz;
		lst = new intPair[size];

		clear();
	}

	~List(){
		delete[] lst;
	}

	int binaryInsert(int val);
	double averageBelow(int maxVal);
};

int sysCommand(const char *);
void initSerialLocalData(void);
void bufferStatusMsg(const char *);
int writeStatusStrings(void);
bool ppsIsRunning(void);
int writeFileMsgToLog(const char *);
int writeFileMsgToLogbuf(const char *, char *);
void writeToLog(char *, const char *);
pid_t getChildPID(void);
int createPIDfile(void);
void writeOffsets(void);
void writeTimestamp(double);
int bufferStateParams(void);
int disableNTP(void);
int enableNTP(void);
int open_logerr(const char*, int, const char *);
int read_logerr(int fd, char *, int, const char *);
void writeInterruptDistribFile(void);
int getConfigs(void);
bool isEnabled(int);
bool isDisabled(int);
void writeSysdelayDistribFile(void);
void showStatusEachSecond(void);
struct timespec setSyncDelay(int, int);
int accessDaemon(int argc, char *argv[]);
void buildErrorDistrib(int);
void buildJitterDistrib(int);
void TERMhandler(int);
void HUPhandler(int);
void buildInterruptJitterDistrib(int);
void recordFrequencyVars(void);
void recordOffsets(int timeCorrection);
void writeToLogNoTimestamp(char *);
int getTimeErrorOverSerial(int *);
int find_source(const char *path, pps_handle_t *handle, int *avail_mode);
int readPPSTimestamp(pps_handle_t *handle, int *avail_mode, int *tm);
void saveGPSTime(timeCheckParams *tcp);
int saveLastState(void);
int loadLastState(void);
int getRootHome(void);
int getRPiCPU(void);
int assignProcessorAffinity(void);
/**
 * @endcond
 */
#endif /* PPS_CLIENT_H_ */

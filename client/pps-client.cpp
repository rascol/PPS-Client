/**
 * @file pps-client.cpp
 * @brief This file contains the principal PPS-Client controller functions and structures.
 *
 * The PPS-Client daemon synchronizes the system clock to a Pulse-Per-Second (PPS)
 * source to a resolution of one microsecond with an absolute accuracy
 * of a few microseconds. To obtain this level of performance PPS-Client
 * provides offset corrections every second and frequency corrections every
 * minute. This and removal of jitter in the reported PPS time keeps the
 * system clock continuously synchronized to the PPS source.
 *
 * A wired GPIO connection is required from a PPS source. Synchronization
 * is provided by the rising edge of that PPS source which is connected to
 * GPIO 4 on Raspberry Pi.
 *
 * The executable for this daemon is "/usr/sbin/pps-client"
 *
 * The daemon script is "/lib/systemd/system/pps-client.service"
 *
 * The configuration file is "/etc/pps-client.conf"
 */

/*
 * Copyright (C) 2016-2020 Raymond S. Connell
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

/**
 * System call
 */
extern int adjtimex (struct timex *timex);

/**
 * System call
 */
//extern int gettimeofday(struct timeval *tv, struct timezone *tz);

const char *version = "2.0.2";							//!< Program v2.0.0 updated on 9 Jul 2020

struct G g;												//!< Declares the global variables defined in pps-client.h.

extern struct ppsFiles f;
extern bool writeJitterDistrib;
extern bool writeErrorDistrib;

static double rawErrorAvg = 0.0;						// Variable cannot be in the G struct because
														// it is cleared on every restart.
class List rawErr(SLEW_LEN);
bool threadIsRunning = false;
bool readState = false;


/**
 * Sets global variables to initial values at
 * startup or restart and sets system clock
 * frequency offset to zero.
 *
 * @param[in] verbose Enables printing of state status params when "true".
 */
int initialize(bool verbose){
	int rv;

	memset(&g, 0, sizeof(struct G));

	g.isVerbose = verbose;
	g.integralGain = INTEGRAL_GAIN;
	g.invProportionalGain = INV_GAIN_0;
	g.hardLimit = HARD_LIMIT_NONE;
	g.exitOnLostPPS = true;

	g.t3.modes = ADJ_FREQUENCY;			// Initialize system clock
	g.t3.freq = 0;						// frequency offset to zero.
	adjtimex(&g.t3);

	rv = getConfigs();

	g.cpuVersion = getRPiCPU();
	if (g.cpuVersion == 3){
		g.zeroOffset = ZERO_OFFSET_RPI3;

		if (g.nCores > 0 && g.nCores != 4){
			printf("Invalid value for segregate in pps-client.conf\n");
			g.nCores = 0;
		}
	}
	else if (g.cpuVersion == 4){
		g.zeroOffset = ZERO_OFFSET_RPI4;

		if (g.nCores > 0 && g.nCores != 4){
			printf("Invalid value for segregate in pps-client.conf\n");
			g.nCores = 0;
		}
	}

	if (g.nCores > 0){
		assignProcessorAffinity();
	}
//	printf("CPU zeroOffset: %d\n", g.zeroOffset);

	return rv;
}

/**
 * Returns true when the control loop can begin to
 * control the system clock frequency. At program
 * start only the time slew is adjusted because the
 * drift can be too large for it to be practical to
 * adjust the system clock frequency to correct for
 * it. SLEW_MAX sets a reasonable limit below which
 * frequency offset can also be adjusted to correct
 * system time.
 *
 * Consequently, once the drift is within SLEW_MAX
 * microseconds of zero and the controller has been
 * running for at least 60 seconds (time selected for
 * convenience), this function returns "true" causing
 * the controller to begin to also adjust the system
 * clock frequency offset.
 *
 * @returns "true" when the control loop can begin to
 * control the system clock frequency. Else "false".
 */
bool getAcquireState(void){

	if (! g.slewIsLow && g.slewAccum_cnt == 0
			&& fabs(g.avgSlew) < SLEW_MAX){					// SLEW_MAX only needs to be low enough
		g.slewIsLow = true;									// that the controller can begin locking
	}														// at limitValue == HARD_LIMIT_NONE

	return (g.slewIsLow && g.seq_num >= SECS_PER_MINUTE);	// The g.seq_num requirement sets a limit on the
}															// length of time to run the Type 1 controller
															// that initially pushes avgSlew below SLEW_MAX.
/**
 * Uses G.avgSlew or avgCorrection and the curent
 * hard limit, G.hardLimit, to determine the global
 * G.hardLimit to set on zeroError to convert error
 * values to time corrections.
 *
 * Because it is much more effective and does not
 * introduce additional time delay, hard limiting
 * is used instead of filtering to remove noise
 * (jitter) from the reported time of PPS capture.
 *
 * @param[in] avgCorrection Current average
 * correction value.
 */
void setHardLimit(double avgCorrection){

	double avgCorrectionMag = fabs(avgCorrection);

	if (g.activeCount < SECS_PER_MINUTE){
		g.hardLimit = HARD_LIMIT_NONE;
		return;
	}

	if (abs(g.avgSlew) > SLEW_MAX){								// As long as average time slew is
		int d_4 = abs(g.avgSlew) * 4;							// outside of range SLEW_MAX this keeps
		while (g.hardLimit < d_4								// g.hardLimit above 4 * g.avgSlew
				&& g.hardLimit < HARD_LIMIT_NONE){				// which is high enough to allow the
			g.hardLimit = g.hardLimit << 1;						// controller to pull avgSlew within
		}														// SLEW_MAX.
		return;
	}

	if (avgCorrectionMag < ((double)g.hardLimit * 0.25)){		// If avgCorrection is below 1/4 of limitValue
		if (g.hardLimit > 1){									// and g.hardLimit not 1
			g.hardLimit = g.hardLimit >> 1;						// then halve limitValue.
		}
	}
	else if (avgCorrectionMag > ((double)g.hardLimit * 0.5)){	// If avgCorrection is above 1/2 of limitValue
		g.hardLimit = g.hardLimit << 1;							// then double limitValue.

		if (g.hardLimit > HARD_LIMIT_NONE){
			g.hardLimit = HARD_LIMIT_NONE;
		}
	}

	return;
}

/**
 * Gets the average time offset from zero over the interval
 * SLEW_LEN and updates avgSlew with this value every SLEW_LEN
 * seconds. Excludes all large delay spikes from the average.
 *
 * @param[in] rawError The raw error to be accumulated to
 * determine average slew including delay spikes.
 */
void getTimeSlew(int rawError){

	rawErr.binaryInsert(rawError);							// Store rawError samples in sorted order of absolute
															// delay to allow rawErr.averageBelow() to ignore
	g.slewAccum_cnt += 1;									// the samples above the LARGE_SPIKE level.

	g.slewAccum += (double)rawError;

	if (g.slewAccum_cnt >= SLEW_LEN){
		g.slewAccum_cnt = 0;

		double avg = g.slewAccum / (double)SLEW_LEN;
		double avgBelow = rawErr.averageBelow(LARGE_SPIKE);  // Do the average only below the LARGE_SPIKE level

		if (fabs(avg) < fabs(avgBelow)){
			g.avgSlew = avg;
		}
		else {
			g.avgSlew = avgBelow;
		}

		g.slewAccum = 0.0;
		rawErr.clear();
	}
}

/**
 * Clamps rawError to an adaptive value relative to the average
 * rawError and determined at the current G.hardLimit value from
 * the current value of G.noiseLevel.
 *
 * Once the rawError values have been limited to values of +/- 1
 * microsecond and the control loop has settled, this clamping causes
 * the controller to make the average number of positive and negative
 * rawError values equal rather than making the sum of the positive and
 * negative jitter values zero. This removes the bias that would
 * otherwise be introduced by the rawError values which are largely
 * random and consequently would introduce a constantly changing
 * random offset. The result is also to move the average PPS interrupt
 * delay to its median value.
 *
 * @param[in] rawError The raw error value to be converted to a
 * zero error.
 */
int clampJitter(int rawError){
	int maxClamp, posClamp, negClamp;

	maxClamp = g.hardLimit;

	int zeroError = rawError;

	if (rawErrorAvg < 1.0 && g.hardLimit <= 4){
		g.clampAbsolute = true;
	}
	else if (g.hardLimit >= 16){
		g.clampAbsolute = false;
	}

	if (g.clampAbsolute){
		posClamp = maxClamp;
		negClamp = -maxClamp;
	}
	else {
		posClamp = (int)rawErrorAvg + maxClamp;
		negClamp = (int)rawErrorAvg - maxClamp;
	}

	if (rawError > posClamp){
		zeroError = posClamp;
	}
	else if (rawError < negClamp){
		zeroError = negClamp;
	}

	return zeroError;
}

/**
 * Constructs, at each second over the last 10 seconds
 * in each minute, 10 integrals of the average time
 * correction over the last minute.
 *
 * These integrals are then averaged to G.avgIntegral
 * just before the minute rolls over. The use of this
 * average of the last 10 integrals to correct the
 * frequency offset of the system clock provides a modest
 * improvement over using only the single last integral.
 *
 * @param[in] avgCorrection The average correction
 * to be integrated.
 */
void makeAverageIntegral(double avgCorrection){

	int indexOffset = SECS_PER_MINUTE - NUM_INTEGRALS;

	if (g.correctionFifo_idx >= indexOffset){					// Over the last NUM_INTEGRALS seconds in each minute

		int i = g.correctionFifo_idx - indexOffset;
		if (i == 0){
			g.avgIntegral = 0.0;
			g.integralCount = 0;
		}

		g.integral[i] = g.integral[i] + avgCorrection;			// avgCorrection sums into g.integral[i] once each
																// minute forming the ith integral over the last minute.
		if (g.hardLimit == HARD_LIMIT_1){
			g.avgIntegral += g.integral[i];						// Accumulate each integral that is being formed
			g.integralCount += 1;								// into g.avgIntegral for averaging.
		}
	}

	if (g.correctionFifo_idx == SECS_PER_MINUTE - 1				// just before the minute rolls over.
			&& g.integralCount == NUM_INTEGRALS){

		g.avgIntegral *= PER_NUM_INTEGRALS;						// Normalize g.avgIntegral.
	}
}

/**
 * Advances the G.correctionFifo index each second and
 * returns "true" when 60 new time correction values
 * have been accumulated in G.correctionAccum.
 *
 * When a value of "true" is returned, new average
 * time correction integrals have been generated by
 * makeAverageIntegral() and are ready for use.
 *
 * @returns "true" when ready, else "false".
 */
bool integralIsReady(void){
	bool isReady = false;

	if (g.correctionFifo_idx == 0){
		isReady = true;
	}

	g.correctionFifo_idx += 1;
	if (g.correctionFifo_idx >= SECS_PER_MINUTE){
		g.correctionFifo_idx = 0;
	}

	return isReady;
}

/**
 * Maintains G.correctionFifo which contains second-by-second
 * values of time corrections over the last minute, accumulates
 * a rolling sum of these and returns the moving average correction
 * over the last minute.
 *
 * Although moving average is more complicated to generate than a
 * conventional expponential average, moving averge has almost the
 * same amount of noise reduction per sample but no history of noise
 * disburbances that occurred in any previous minute. Consequently,
 * the servo loop can converge significantly faster using a moving
 * average.
 *
 * @param[in] timeCorrection The time correction value to be accumulated.
 *
 * @returns The average correction value.
 */
double getMovingAverage(int timeCorrection){

	double avgCorrection;

	g.correctionAccum += timeCorrection;				// Add the new timeCorrection into the error accumulator.

	if (g.correctionFifoCount == SECS_PER_MINUTE){		// Once the FIFO is full, maintain the continuous
														// rolling sum accumulator by subtracting the
		int oldError = g.correctionFifo[g.correctionFifo_idx];
		g.correctionAccum -= oldError;					// old timeCorrection value at the current correctionFifo_idx.
	}

	g.correctionFifo[g.correctionFifo_idx] = timeCorrection;	// and replacing the old value in the FIFO with the new.

	if (g.correctionFifoCount < SECS_PER_MINUTE){		// When correctionFifoCount == SECS_PER_MINUTE
		g.correctionFifoCount += 1;						// the FIFO is full and ready to use.
	}

	avgCorrection = g.correctionAccum * PER_MINUTE;
	return avgCorrection;
}

/**
 * Returns the time of day as the nearest integer to
 * the system time. Because detectMissedPPS() is
 * called very near the rollover of the second, this
 * prevents G.t_now from being initialized with the
 * integer value of the previous second if
 * detectMissedPPS() is called slightly ahead of rollover.
 */
int getNearestSecond(void){
	int roundedTime;
	struct timespec t_now;

	clock_gettime(CLOCK_REALTIME, &t_now);
	roundedTime = (int)round((double)t_now.tv_sec + 1e-9 * (double)t_now.tv_nsec);
	return roundedTime;
}

/**
 * If NIST time is enabled in pps-client.conf,
 * sets the system time whenever there is an error
 * relative to the whole seconds obtained from
 * Internet NIST servers by writing the whole
 * second correction to the adjtimex function.
 *
 * Errors are infrequent. But if one occurs the whole
 * seconds of system clock are set following the
 * CHECK_TIME interval.
 */
void setClocktoNISTtime(void){
	memset(&g.t3, 0, sizeof(struct timex));

	g.t3.modes = ADJ_SETOFFSET | ADJ_STATUS;
	g.t3.status = STA_PLL;

	g.t3.time.tv_sec = g.consensusTimeError;
	g.t3.time.tv_usec = 0;

	int rv = adjtimex(&g.t3);
	if (rv == -1){
		sprintf(g.logbuf, "In setClocktoNISTtime() adjtimex() returned: errno: %d, %s\n", errno, strerror(errno));
		writeToLog(g.logbuf, "setClocktoNISTtime()");
	}
	else {
		sprintf(g.logbuf, "adjtimex(): Requested correction: %d secs\n", g.consensusTimeError);
		writeToLog(g.logbuf, "setClocktoNISTtime()");

		sprintf(g.logbuf, "adjtimex(): Log message will have a timestamp resulting from this correction\n");
		writeToLog(g.logbuf, "setClocktoNISTtime()");
	}

	g.consensusTimeError = 0;

	g.nistTimeUpdated = true;

	return;
}

/**
 * If GPS time through a serial port is enabled in
 * pps-client.conf, sets the system time whenever
 * there is an error relative to the whole seconds
 * obtained through the serial port by writing the
 * whole second correction to the adjtimex function.
 *
 * Errors are infrequent. But if one occurs the whole
 * seconds of system clock are set.
 */
void setClockToGPStime(void){
	memset(&g.t3, 0, sizeof(struct timex));

	g.t3.modes = ADJ_SETOFFSET | ADJ_STATUS;
	g.t3.status = STA_PLL;

	g.t3.time.tv_sec = g.serialTimeError;
	g.t3.time.tv_usec = 0;

	int rv = adjtimex(&g.t3);
	if (rv == -1){
		sprintf(g.logbuf, "adjtimex() returned: errno: %d, %s\n", errno, strerror(errno));
		writeToLog(g.logbuf, "setClockToGPStime()");
	}
	else {
		sprintf(g.logbuf, "adjtimex(): Requested correction: %d secs\n", g.serialTimeError);
		writeToLog(g.logbuf, "setClockToGPStime()");
		sprintf(g.logbuf, "adjtimex(): Log message will have a timestamp resulting from this correction\n");
		writeToLog(g.logbuf, "setClockToGPStime()");
	}

	g.serialTimeError = 0;

	g.serialTimeUpdated = true;

	return;
}


/**
 * Constructs an exponentially decaying distribution
 * of rawError with a half life on individual samples
 * of 1 hour.
 *
 * @param[in] rawError The distribution values.
 * @param[out] errorDistrib The distribution being constructed.
 * @param[in,out] count The count of distribution samples.
 */
void buildRawErrorDistrib(int rawError, double errorDistrib[], unsigned int *count){
	int len = ERROR_DISTRIB_LEN - 1;

	int idx = rawError + RAW_ERROR_ZERO;
	if (idx > len){
		idx = len;
	}
	else if (idx < 0){
		idx = 0;
	}

	if (g.hardLimit == HARD_LIMIT_1){

		if (*count > 600 && *count % 60 == 0){						// About once a minute

			for (int i = 0; i < len; i++){							// Scale errorDistrib to allow older
				errorDistrib[i] *= RAW_ERROR_DECAY;					// values to decay (halflife 1 hour).
			}
		}
		errorDistrib[idx] += 1.0;
	}

	*count += 1;
}

/**
 * Averages rawError (exponential average) and the positive
 * half of the rawError distribution to determine an average
 * noise level.
 *
 * @param[in] rawError The fractional second time of arrival
 * of the PPS.
 */
void getAvgNoiseLevel(int rawError){
	double diff = ((double)rawError - rawErrorAvg) * NOISE_ACCUM_RATE;
	rawErrorAvg += diff;

	double absdiff = ((double)abs(g.jitter) - g.noiseLevel) * NOISE_ACCUM_RATE;
	g.noiseLevel += absdiff;
}

/**
 * Removes jitter delay spikes by returning "true"
 * as long as the jitter value remains beyond a
 * threshold that is determined by the curent value
 * of G.noiseLevel.
 *
 * @param[in] rawError The raw error vlue to be
 * tested for being a delay spike.
 *
 * @returns "true" if a delay spike is detected. Else "false".
 */
bool detectDelaySpike(int rawError){
	bool isDelaySpike = false;
	bool limitCondition;

	if (g.clampAbsolute){
		limitCondition = g.hardLimit == 1 && rawError >= NOISE_LEVEL_MIN;
	}
	else {
		limitCondition = g.isControlling && (rawError - rawErrorAvg) >= LARGE_SPIKE;
	}

	if (limitCondition){

		if (g.nDelaySpikes < MAX_SPIKES) {
			if (g.nDelaySpikes == 0){
				g.minSustainedDelay = MAX_SPIKE_LEVEL;
			}
			else {
				if (rawError < g.minSustainedDelay){
					g.minSustainedDelay = rawError;
				}
			}
			g.nDelaySpikes += 1;					// Record unbroken sequence of delay spikes

			isDelaySpike = true;
		}
		else {										// If nDelaySpikes == MAX_SPIKES stop the
			isDelaySpike = false;					// suspend even if spikes continue.

			if (g.minSustainedDelay > CLK_CHANGED_LEVEL){
				g.clockChanged = true;
			}
		}
	}
	else {

		if (g.clampAbsolute == false){
			getAvgNoiseLevel(rawError);
		}

		isDelaySpike = false;

		if (g.nDelaySpikes > 0){
			g.nDelaySpikes = 0;
		}
	}
	return isDelaySpike;
}

/**
 * Removes delay spikes and jitter from rawError and
 * returns the resulting clamped zeroError.
 *
 * @param[in] rawError The raw error value to be processed.
 *
 * @returns The resulting zeroError value.
 */
int removeNoise(int rawError){

	int zeroError;

	buildRawErrorDistrib(rawError, g.rawErrorDistrib, &(g.ppsCount));

	g.jitter = rawError;
	g.isDelaySpike = detectDelaySpike(rawError);	// g.isDelaySpike == true will prevent time and
													// frequency updates during a delay spike.
	getTimeSlew(rawError);

	if (writeJitterDistrib && g.seq_num > SETTLE_TIME){
		buildJitterDistrib(rawError);
	}

	if (g.isDelaySpike){
		return 0;
	}

	setHardLimit(g.avgCorrection);

	zeroError = clampJitter(rawError);				// Recover the time error by
													// limiting away the jitter.
	if (g.clampAbsolute == true){
		getAvgNoiseLevel(zeroError);
	}

	if (g.isControlling){
		g.invProportionalGain = INV_GAIN_1;
	}

	if (g.seq_num > SETTLE_TIME && writeErrorDistrib){
		buildErrorDistrib(zeroError);
	}

	return zeroError;
}

/**
 * if G.hardLimit == HARD_LIMIT_1, gets an integral time
 * correction as a 10 second average of integrals of average
 * time corrections over one minute. Otherwise gets the
 * integral time correction as the single last integral
 * of average time corrections over one minute.
 *
 * @returns The integral of time correction values.
 */
double getIntegral(void){
	double integral;

	if (g.hardLimit == HARD_LIMIT_1
			&& g.integralCount == NUM_INTEGRALS){
		integral = g.avgIntegral;					// Use average of last 10 integrals
	}												// in the last minute.
	else {
		integral = g.integral[9];					// Use only the last integral from
													// the last minute
	}

	recordFrequencyVars();

	return integral;
}

/**
 * Gets the time of the PPS rising edge from the
 * timeCorrection value and sets the corresponding
 * timestamp.
 *
 * @param[in] timeCorrection The correction to
 * be applied to get the time of the PPS rising
 * edge.
 */
void savePPStime(int timeCorrection){

	struct timeval tv1;
	gettimeofday(&tv1, NULL);					// Will always be after rollover of second

	g.pps_t_sec = tv1.tv_sec;					// So the unmodified second will be correct

	g.pps_t_usec = -timeCorrection;
	if (g.pps_t_usec < 0){
		g.pps_t_usec = 1000000 - timeCorrection;
		g.pps_t_sec -= 1;
	}

	double timestamp = (double)g.pps_t_sec + 1e-6 * (double)g.pps_t_usec;

//	printf("\ntimestamp: %lf timeCorrection: %d\n", timestamp, timeCorrection);

	writeTimestamp(timestamp);
}

/**
 * Gets the fractional seconds part of interrupt time
 * and if the value should be interpreted as negative
 * then translates the value.
 *
 * @param[in] fracSec The delayed time of
 * the PPS rising edge returned by the system clock.
 *
 * @returns The signed fractional seconds part of the time.
 */
int signedFractionalSeconds(int fracSec){

	if (fracSec > 500000){
		fracSec -= USECS_PER_SEC;
	}
	return fracSec;
}

/**
 * Advances a monotonic time count G.t_count second by
 * second. That happens even when this function is not
 * called in a particular second.
 *
 * The G.t_count value is used in detectExteralSystemClockChange()
 * to determine if the system time has been set externally.
 */
void detectMissedPPS(void){
	struct timespec t_mono;

	g.t_now = getNearestSecond();					// Current time seconds

	if (g.blockDetectClockChange > 0){
		g.blockDetectClockChange -= 1;

		if (g.blockDetectClockChange == 0){
			g.t_count = g.t_now;
		}
	}

	clock_gettime(CLOCK_MONOTONIC, &t_mono);
	g.t_mono_now = (double)t_mono.tv_sec + 1e-9 * (double)t_mono.tv_nsec;

	if (g.seq_num < 2 || g.startingFromRestore != 0){	// Initialize g.t_mono_last to
		g.t_mono_last = g.t_mono_now - 1.0;				// prevent incorrect detection
	}

	if (g.seq_num == 0 || g.startingFromRestore != 0){	// Initialize g.t_count at start
		g.t_count = g.t_now;
	}

	double diff = g.t_mono_now - g.t_mono_last;
	int iDiff = (int)round(diff);
												// test. For normal operation uncomment the following section:
	if (iDiff > 1){
		sprintf(g.logbuf, "detectMissedPPS(): Missed PPS %d time(s)\n", iDiff-1);
		writeToLog(g.logbuf, "detectMissedPPS()");
	}

	g.t_count += iDiff;								// The counter is advanced only if monotonic clock advanced.

	g.t_mono_last = g.t_mono_now;					// Set value of g.t_mono_last to be used next
}

/**
 * Determines whether the system clock has been set
 * externally.
 *
 * @param[in] pps_t The delayed time of the PPS rising
 * edge returned by the system clock.
 *
 * @returns true if clock was set else false.
 */
bool detectExteralSystemClockChange(struct timeval pps_t){
	bool clockChanged = false;

	if (g.startingFromRestore != 0){
		return clockChanged;
	}

	if (g.isControlling && g.seq_num > SLEW_LEN && fabs(g.avgSlew) < SLEW_MAX) {

		if (g.t_now != g.t_count){
			int change = g.t_now - g.t_count;
			sprintf(g.logbuf, "detectExteralSystemClockChange() System time changed externally by %d seconds\n", change);
			writeToLog(g.logbuf, "detectExteralSystemClockChange()");

			clockChanged = true;     						// The clock was set externally.
			g.t_count = g.t_now;							// Update the seconds counter.
		}
		else if (g.hardLimit == HARD_LIMIT_1 && g.clockChanged){
			g.clockChanged = false;

			sprintf(g.logbuf, "detectExteralSystemClockChange() Error in fractional second of %ld microseconds\n", pps_t.tv_usec);
			writeToLog(g.logbuf, "detectExteralSystemClockChange()");

			clockChanged = true;     						// The clock was set externally.
			g.t_count = g.t_now;							// Update the seconds counter.
		}
	}
	return clockChanged;
}

/**
 * Corrects the system time whenever the system clock is
 * set externally.
 *
 * The external device that sets the clock might inject a
 * non-zero fractional second along with the whole seconds.
 * It is assumed that the whole system clock seconds would
 * have the same value as the whole PPS seconds. So the
 * correction to the fractional second that is made must not
 * cause a change to the whole seconds.
 *
 * Because the adjtimex() function will not accept a negative
 * number as a fractional second and the correction value
 * is a positive number then to subtract the correction,
 * (1e6 - correction) microseconds is added instead.
 *
 * If correction is less than half a second then (1e6 - correction)
 * is greater than half a second. That would push the system
 * clock one second ahead of the PPS. To prevent that, one
 * second is subtracted from the system clock whole seconds.
 *
 * If the correction is greater than a half second then
 * (1e6 - correction) is less than half a second so no change
 * to the system clock whole seconds is necessary.
 *
 * If the correction is near half a second, handling of the
 * whole second is ambiguous and the change to the whole second
 * could be wrong. In that case the the whole second error would
 * eventually be caught by a time update from the external
 * timekeeper and corrected then.
 *
 * @param[in] correction The fractional second correction value
 * in microseconds.
 */
void setClockFractionalSecond(int correction){			// The correction is always a positive number.

	memset(&g.t3, 0, sizeof(struct timex));
														// Not possible to assign a negative number to fractional part
	g.t3.modes = ADJ_SETOFFSET | ADJ_STATUS;			// given to adjtimex(). So must give it (1e6 - correction) instead.
	g.t3.status = STA_PLL;

	if (correction < 500000){							// (1e6 - correction) is greater than a half second. That would
														// push the system clock one second ahead of the PPS second.
		g.t3.time.tv_sec = -1;							// Compensate by subtracting a whole second.
		g.t3.time.tv_usec = USECS_PER_SEC - correction;
	}
	else if (correction > 1000000){						// Positive g.zeroOffset could push correction into a range where
		g.t3.time.tv_sec = -1;							// (1e6 - correction) is less than zero which is an invalid range
		g.t3.time.tv_usec = 2 * USECS_PER_SEC - correction;	// for adjtimex() so add 1000000 usec and subtract 1 sec
	}
	else {												// In this case, (1e6 - correction) is less than half a second
														// so adding the difference would not push the system clock
		g.t3.time.tv_sec = 0;							// time ahead by a second. So no whole second is subtracted.
		g.t3.time.tv_usec = USECS_PER_SEC - correction;
	}

	g.t_now = (int)g.t3.time.tv_sec;					// Reconcile the g.t_count monotonic counter to prevent
	g.t_count = g.t_now;								// triggering detectExteralSystemClockChange().

	int rv = adjtimex(&g.t3);
	if (rv == -1){
		sprintf(g.logbuf, "adjtimex() returned: errno: %d, %s\n", errno, strerror(errno));
		writeToLog(g.logbuf, "setClockFractionalSecond()");
	}

	return;
}

/**
 * Removes the error potentially introduced in the interrupt
 * time by an external clock change. The correction that is
 * introduced only needs to be good enough that the time servo
 * can track out any remaining error.
 *
 * @param[in] pps_t The delayed time of the PPS rising
 * edge returned by the system clock.
  */
int correctFractionalSecond(struct timeval *pps_t){

	int correction = pps_t->tv_usec;

	int relCorrection = correction;
	if (relCorrection >= 1000000){
		relCorrection = correction - 1000000;
	}
	else if (relCorrection > 500000){
		relCorrection = -(1000000 - correction);
	}

	if (abs(relCorrection) < 15){								// No correction needed for less than 15 usec since
		return 1;												// the servo can remove an error of this magnitude.
	}

	setClockFractionalSecond(correction);

	pps_t->tv_usec = pps_t->tv_usec - correction;				// Correct the interrut delay time that will be processed.

	return 0;
}

/**
 * Makes any necessary corrections to the system time required
 * or caused by external time keepers.
 *
 * @param[in] pps_t The delayed time of the PPS rising
 * edge returned by the system clock.
 */
void doTimeFixups(struct timeval pps_t){
	int rv;

	if (g.serialTimeUpdated == true){
		g.t_now = getNearestSecond();
		g.t_count = g.t_now;
		g.serialTimeUpdated = false;
	}

	if (g.nistTimeUpdated == true){
		g.t_now = getNearestSecond();
		g.t_count = g.t_now;
		g.nistTimeUpdated = false;
	}

	if (g.doNISTsettime && g.consensusTimeError != 0){			// When a NIST time correction is needed
		setClocktoNISTtime();									// it is done here.
	}

	if (g.doSerialsettime && g.serialTimeError != 0){			// When a serial time correction is needed
		setClockToGPStime();									// it is done here.
	}

	if (g.blockDetectClockChange == 0 &&
			detectExteralSystemClockChange(pps_t)){

		if (g.serialTimeUpdated == true){						// If the clock was changed because of serial time update,
			return;												// don't need to correct the fractional second.
		}

		if (g.nistTimeUpdated == true){							// Same for NIST
			return;
		}

		rv = correctFractionalSecond(&pps_t);
		if (rv == 1){
			return;
		}

		g.blockDetectClockChange = SECS_PER_MINUTE;				// Block detectExteralSystemClockChange() for one minute
																// after it has been triggered.
		sysCommand("systemctl stop systemd-timesyncd.service");	// If systemctl was used to set the system time, kill it.
	}
	else if (g.blockDetectClockChange > SECS_PER_MINUTE - 4){
		correctFractionalSecond(&pps_t);						// Continue fixing the fractional seconds for awhile.
	}

	return;
}

/**
 * Makes time corrections each second, frequency corrections
 * each minute and removes jitter from the PPS time reported
 * by pps_t.
 *
 * Jitter is removed by clamping corrections to a sequence of
 * positive and negative values each of magnitude one microsec.
 * Thus the average timeCorrection, which is forced to be zero
 * by the controller, also corresponds to a time delay where the
 * number of positive and negative corrections are equal. Thus
 * the correction is the median of the time delays causing the
 * corrections rather than the average value of those delays.
 * This strategy almost completely eliminates second-by-second
 * noise.
 *
 * This function is called by readPPS_SetTime() from within the
 * one-second delay loop in the waitForPPS() function.
 *
 * @param[in] pps_t The delayed time of the PPS rising
 * edge returned by the system clock.
 *
 * @returns 0 on success else -1 on system error.
 */
int makeTimeCorrection(struct timeval pps_t){

	g.interruptReceived = true;

	g.seq_num += 1;

	if (g.isControlling && g.startingFromRestore == 0){
		doTimeFixups(pps_t);
	}

	g.ppsTimestamp = (int)pps_t.tv_usec;

	int time0 = g.ppsTimestamp - g.zeroOffset;

	g.rawError = signedFractionalSeconds(time0);		// g.rawError is set to zero by the feedback loop causing
														// pps_t.tv_usec == g.zeroOffset so that the timestamp
	g.zeroError = removeNoise(g.rawError);				// is equal to the PPS delay.

	if (g.isDelaySpike){								// Skip a delay spike.
		savePPStime(0);
		return 0;
	}

	g.timeCorrection = -g.zeroError						// The sign of g.zeroError is chosen to provide negative feedback.
				/ g.invProportionalGain;				// Apply controller proportional gain factor.

	g.t3.status = 0;
	g.t3.modes = ADJ_OFFSET_SINGLESHOT;
	g.t3.offset = g.timeCorrection;

	adjtimex(&g.t3);

	g.isControlling = getAcquireState();				// Provides enough time to reduce time slew on startup.
	if (g.isControlling){

		g.avgCorrection = getMovingAverage(g.timeCorrection);

		makeAverageIntegral(g.avgCorrection);			// Constructs an average of integrals of one
														// minute rolling averages of time corrections.
		if (integralIsReady()){							// Get a new frequency offset.
			g.integralTimeCorrection = getIntegral();
			g.freqOffset = g.integralTimeCorrection * g.integralGain;

			g.t3.status = 0;
			g.t3.modes = ADJ_FREQUENCY;
			g.t3.freq = (long)round(ADJTIMEX_SCALE * g.freqOffset);
			adjtimex(&g.t3);							// Adjust the system clock frequency.
		}

		recordOffsets(g.timeCorrection);

		g.activeCount += 1;
	}
	else {
		g.t_count = g.t_now;							// Unless g.isControlling let g.t_count copy pps_t.tv_sec.
	}													// If g.isControlling then g.t_count is an independent counter.

	savePPStime(g.timeCorrection);
	return 0;
}

/**
 * Logs loss and resumption of the PPS interrupt.
 * Can force exit if the interrupt is lost for more than one
 * hour when "exit-lost-pps=enable" is set in the config file.
 *
 * @returns 0 on success, else -1 on error.
 */
int checkPPSInterrupt(void){

	if (g.seq_num > 0 && g.exit_requested == false){	// Start looking for lost PPS
		if (g.interruptReceived == false){
			g.interruptLossCount += 1;

			if (g.interruptLossCount == INTERRUPT_LOST){
				sprintf(g.logbuf, "WARNING: PPS interrupt lost\n");
				writeToLog(g.logbuf, "checkPPSInterrupt()");
			}
			if (g.exitOnLostPPS &&  g.interruptLossCount >= SECS_PER_HOUR){
				sprintf(g.logbuf, "ERROR: Lost PPS for one hour.");
				writeToLog(g.logbuf, "checkPPSInterrupt()");
				return -1;
			}
		}
		else {
			if (g.interruptLossCount >= INTERRUPT_LOST){
				sprintf(g.logbuf, "PPS interrupt resumed\n");
				writeToLog(g.logbuf, "checkPPSInterrupt()");
			}
			g.interruptLossCount = 0;
		}
	}

	g.interruptReceived = false;

	return 0;
}

/**
 * Sets a nanosleep() time delay equal to the time remaining
 * in the second from the time recorded as fracSec plus an
 * adjustment value of timeAt in microseconds.
 *
 * @param[in] timeAt The adjustment value in microseconds.
 *
 * @param[in] fracSec The fractional second part of
 * the system time in microseconds.
 *
 * @returns The length of time to sleep in seconds and nanoseconds.
 */
struct timespec setSyncDelay(int timeAt, int fracSec){

	struct timespec ts2;

	int timerVal = USECS_PER_SEC - fracSec + timeAt;

	if (timerVal >= USECS_PER_SEC){
		ts2.tv_sec = 1;
		ts2.tv_nsec = (timerVal - USECS_PER_SEC) * 1000;
	}
	else if (timerVal < 0){
		ts2.tv_sec = 0;
		ts2.tv_nsec = (USECS_PER_SEC + timerVal) * 1000;
	}
	else {
		ts2.tv_sec = 0;
		ts2.tv_nsec = timerVal * 1000;
	}

	return ts2;
}

/**
 * Requests a read of the timestamp of the PPS hardware
 * interrupt by the system PPS driver and passes the value
 * read to makeTimeCorrection().
 *
 * The first time pps-client runs, the time slew can be as
 * large as hundreds of milliseconds. When this is the case,
 * limits imposed by adjtimex() prevent changes in offset of
 * more than about 500 microseconds each second. As a result,
 * pps-client will make a partial correction each minute and
 * will restart several times before the slew is small enough
 * that getAcquireState() will set G.isControlling to "true".
 * This looping will eventually converge  but can take as
 * long as 20 minutes.
 *
 * @param[in] verbose If "true" then write pps-client
 * state status messages to the console. Else not.
 *
 * @param[in] tcp State data for makeNISTTimeQuery
 *
 * @param[in] pps_handle The handle to the system PPS device.
 * @param[in] pps_mode The system PPS device mode.
 *
 * @returns 0 if no restart is required, 1 if restart
 * is required or -1 on system error.
 */
int readPPS_SetTime(bool verbose, timeCheckParams *tcp, pps_handle_t *pps_handle, int *pps_mode){
	int restart = 0;

	int rv = readPPSTimestamp(pps_handle, pps_mode, g.tm);

	detectMissedPPS();

	g.interruptLost = false;
	if (rv < 0){
		if (! g.exit_requested){
			time_t t = time(NULL);
			struct tm *tmp = localtime(&t);
			strftime(g.strbuf, STRBUF_SZ-1, "%F %H:%M:%S ", tmp);
			strcat(g.strbuf, "Read PPS interrupt failed\n");
			bufferStatusMsg(g.strbuf);
		}
		else {
			sprintf(g.logbuf, "gps-pps-io PPS read() returned: %d Error: %s\n", rv, strerror(errno));
			writeToLog(g.logbuf, "readPPS_SetTime()");
		}
		g.interruptLost = true;
	}
	else {

		g.t.tv_sec = g.tm[0];					// Seconds value read by gps-pps-io driver from system clock
												// at rising edge of PPS signal.
		g.t.tv_usec = g.tm[1];					// Fractional seconds read from the timestamp of the PPS signal

		rv = makeTimeCorrection(g.t);
		if (rv == -1){
			sprintf(g.logbuf, "%s\n", "makeTimeCorrection() returned -1");
			writeToLog(g.logbuf, "readPPS_SetTime()");
			return -1;
		}

		if (g.startingFromRestore == 0){

			if ((!g.isControlling && g.seq_num >= SECS_PER_MINUTE)			// If time slew on startup is too large
					|| (g.isControlling && g.hardLimit > HARD_LIMIT_1024	// or if g.avgSlew becomes too large
					&& abs(g.avgSlew) > SLEW_MAX)){							// after acquiring

				sprintf(g.logbuf, "pps-client is restarting from SLEW_MAX...\n");
				writeToLog(g.logbuf, "readPPS_SetTime() 1");

				initialize(verbose);				// then restart the controller.

				restart = 1;
			}
		}
		else {
			if (g.isControlling && abs(g.avgSlew) > SLEW_MAX){

				sprintf(g.logbuf, "pps-client is restarting from restore...\n");
				writeToLog(g.logbuf, "readPPS_SetTime() 2");

				initialize(verbose);				// then restart the controller.
				restart = 1;
			}
		}
	}
	return restart;
}

//void reportLeak(const char *msg){
//	sprintf(g.logbuf, msg);
//	writeToLog(g.logbuf);
//}

//void testForArrayLeaks(void){
//	if (g.seq_num % SECS_PER_10_MIN == 0){
//		for (int i = 0; i < 100; i++){
//			if (g.pad1[i] != 0){
//				reportLeak("Leak in g.pad1 .................................\n");
//			}
//
//			if (g.pad2[i] != 0){
//				reportLeak("Leak in g.pad2 .................................\n");
//			}
//
//		}
//	}
//}

//int printDuration(struct timeval *tv2, struct timeval *tv1){
//	int usec;
//	int sec;
//
//	tv1->tv_sec = g.tm[0];
//	tv1->tv_usec = g.tm[1];
//
//	if (tv2->tv_usec >= tv1->tv_usec){
//		usec = tv2->tv_usec - tv1->tv_usec;
//		sec = tv2->tv_sec - tv1->tv_sec;
//	}
//	else {
//		usec = tv2->tv_usec - tv1->tv_usec + 1000000;
//		sec = tv2->tv_sec - tv1->tv_sec - 1;
//	}
//
//	printf("  Runtime: %d, rawErrorAvg: %lf, g.noiseLevel: %lf, g.avgSlew: %lf, g.isDelaySpike: %d\n", usec + 1000000 * sec, rawErrorAvg, g.noiseLevel, g.avgSlew, g.isDelaySpike);
//	return usec;
//}

/**
 * Runs the one-second wait loop that waits for
 * the PPS hardware interrupt that returns the
 * timestamp of the interrupt which is passed to
 * makeTimeCorrection().
 *
 * @param[in] verbose If "true" then write pps-client
 * state status messages to the console. Else not.
 *
 * @param[in] pps_handle The handle to the system PPS device.
 * @param[in] pps_mode The the system PPS device mode.
 *
 */
void waitForPPS(bool verbose, pps_handle_t *pps_handle, int *pps_mode){
	struct timeval tv1;
	struct timespec ts2;
	int timePPS;
	int rv;
	timeCheckParams tcp;
	int restart = 0;

	if (g.doNISTsettime){
		rv = allocInitializeNISTThreads(&tcp);
		if (rv == -1){
			goto end;
		}
	}
	if (g.doSerialsettime){
		char cmd[80];
		strcpy(cmd, "stty -F ");

		sprintf(g.logbuf, "\nSerial port, %s, is providing time of day from GPS Satellites\n\n", g.serialPort);
		writeToLog(g.logbuf, "waitForPPS() 1");

		strcat(cmd, g.serialPort);
		strcat(cmd, " raw 9600 cs8 clocal -cstopb");
		rv = sysCommand(cmd);
		if (rv == -1){
			return;
		}
		allocInitializeSerialThread(&tcp);
	}

	signal(SIGHUP, HUPhandler);			// Handler used to ignore SIGHUP.
	signal(SIGTERM, TERMhandler);		// Handler for the termination signal.

	sprintf(g.logbuf, "PPS-Client v%s is starting ...\n", version);
	writeToLog(g.logbuf, "waitForPPS() 1");
										// Set up a one-second delay loop that stays in synch by
	timePPS = -PPS_WINDOW;		    	// continuously re-timing to before the roll-over of the second.
										// timePPS allows for a time window in which to look for the PPS
	writeStatusStrings();

	for (;;){							// Look for the PPS time returned by the PPS driver

		if (readState == false){

			rv = loadLastState();		// Attempts to use the previous state to avoid long restarts.
			if (rv == -1){				// If the previous state is from a recent state can save restart time.
				break;
			}
			readState = true;
		}

		if (g.startingFromRestore > 0){		// Only used on restore
			g.startingFromRestore -= 1;		// Stops decrementing when g.startingFromRestore == 0

			g.t_now = getNearestSecond();
			g.t_count = g.t_now;
		}

		g.isVerbose = verbose;

		if (g.exit_requested){
			sprintf(g.logbuf, "PPS-Client stopped.\n");
			writeToLog(g.logbuf, "waitForPPS() 2");
			break;
		}

		gettimeofday(&tv1, NULL);
		ts2 = setSyncDelay(timePPS, tv1.tv_usec);

		nanosleep(&ts2, NULL);			// Sleep until ready to look for PPS interrupt

		restart = readPPS_SetTime(verbose, &tcp, pps_handle, pps_mode);
		if (restart == -1){
			break;
		}

		if (restart == 0){

			if (g.doSerialsettime && threadIsRunning == false && g.isControlling){
				threadIsRunning = true;

				int rv = pthread_create(&((tcp.tid)[0]), &(tcp.attr), (void* (*)(void*))&saveGPSTime, &tcp);
				if (rv != 0){
					sprintf(g.logbuf, "Can't create thread : %s\n", strerror(errno));
					writeToLog(g.logbuf, "waitForPPS()");
					break;
				}
			}

			if (checkPPSInterrupt() != 0){
				sprintf(g.logbuf, "Lost PPS or system error. pps-client is exiting.\n");
				writeToLog(g.logbuf, "waitForPPS() 3");
				break;
			}

			if (bufferStateParams() == -1){
				break;
			}

			if (g.doNISTsettime && g.isControlling){
				makeNISTTimeQuery(&tcp);
			}

			if (g.doSerialsettime && g.isControlling){
				makeSerialTimeQuery(&tcp);
			}

			writeStatusStrings();

			if (! g.interruptLost && ! g.isDelaySpike){
				if (getConfigs() == -1){
					break;
				}
			}
		}

//		if (verbose){
//			printDuration(&tv1, &tst);
//		}
	}

	saveLastState();

end:
	if (g.doNISTsettime){
		freeNISTThreads(&tcp);
	}
	if (g.doSerialsettime){
		pthread_cancel((tcp.tid)[0]);
	}

	if (g.doSerialsettime){
		freeSerialThread(&tcp);
	}
	return;
}

/**
 * If not already running, creates a detached process that
 * will run as a daemon. Accepts one command line arg: -v
 * that causes the daemon to run in verbose mode which
 * writes a status string and event messages to the console
 * once per second. These messages continue until the
 * console that started the pps-client daemon is closed.
 *
 * Alternatively, if the daemon is already running,
 * displays a statement to that effect and accepts
 * the following optional command line args:
 *
 * The -v flag starts the second-by-second display
 * of a status string that will continue until ended
 * by ctrl-c.
 *
 * The -s flag requests that specified files be saved.
 * If the -s flag is not followed by a file specifier,
 * a list of the files that can be saved is printed.
 */
int main(int argc, char *argv[])
{
	int rv = 0;
	int ppid;
	bool verbose = false;

	if (argc > 1){
		if (strcmp(argv[1], "-v") == 0){
			verbose = true;
		}
	}

	int prStat = accessDaemon(argc, argv);				// Send commands to the daemon.
	if (prStat == 0 || prStat == -1){					// Program is running or an error occurred.
		return rv;
	}

	if (geteuid() != 0){								// Superuser only!
		printf("pps-client is not running. \"sudo pps-client\" to start.\n");
		return rv;
	}

	pid_t pid = fork();									// Fork a duplicate child of this process.

	if (pid > 0){										// This is the parent process.
		bufferStatusMsg("Spawning pps-client daemon.\n");
		return rv;										// Return from the parent process and leave
	}													// the child running.

	if (pid == -1){										// Error: unable to fork a child from parent,
		sprintf(g.logbuf, "Fork in main() failed: %s\n", strerror(errno));
		writeToLog(g.logbuf, "main()");
		return pid;
	}
						// pid == 0 for the child process which now will run this code as a daemon

	getRootHome();

	pps_handle_t pps_handle;
	int pps_mode;

	initialize(verbose);
	if (rv == -1){
		goto end0;
	}

	rv = find_source(f.pps_device, &pps_handle, &pps_mode);
	if (rv < 0){
		sprintf(g.logbuf, "Unable to get PPS source. Exiting.\n");
		fprintf(stderr, "%s", g.logbuf);
		writeToLog(g.logbuf, "main()");
		goto end0;
	}

	ppid = createPIDfile();								// Create the PID file for this process.
	if (ppid == -1){									// Either already running or could not
		rv = -1;										// create a pid file. In either case, exit.
		goto end0;
	}

	struct sched_param param;							// Process must be run as root
	mlockall(MCL_CURRENT | MCL_FUTURE);

	rv = sysCommand("timedatectl set-ntp 0");			// Disable NTP, to prevent it from disciolining the clock.
	if (g.doNISTsettime && rv != 0){
		goto end0;
	}													// Also disable systemd-timesyncd.
	rv = sysCommand("systemctl stop systemd-timesyncd.service");
	if (rv != 0){
		goto end0;
	}

	param.sched_priority = 99;							// to get real-time priority.
	sched_setscheduler(0, SCHED_FIFO, &param);			// SCHED_FIFO: Don't yield to scheduler until sleep.

	sprintf(g.msgbuf, "Process PID: %d\n", ppid);		// PPS client is starting.
	bufferStatusMsg(g.msgbuf);

	waitForPPS(verbose, &pps_handle, &pps_mode); 		// Synchronize to the PPS.

	time_pps_destroy(pps_handle);

	sysCommand("rm /run/pps-client.pid");				// Remove the PID file with system() which blocks until
														// rm completes keeping shutdown correctly sequenced.
//	sysCommand("timedatectl set-ntp 1");				// Re-enable a system time service on shutdown.
end0:
	return rv;
}


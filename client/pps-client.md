# PPS Client Documentation (rev. a) {#mainpage}

- [Uses](#uses)
- [PPS-Client High Accuracy Timekeeping](#pps-client-high-accuracy-timekeeping)
  - [Noise and Latency](#noise-and-latency)
    - [The Timeline](#the-timeline)
    - [Raspberry Pi 3](#raspberry-pi-3)
    - [Raspberry Pi 4](#raspberry-pi-4)
    - [Latency Spikes](#latency-spikes)
- [The PPS-Client Controller](#the-pps-client-controller)
  - [Feedback Controller](#feedback-controller)
  - [Feedforward Compensation](#feedforward-compensation)
  - [Driver](#driver)
  - [Controller Behavior on Startup](#controller-behavior-on-startup)
  - [Performance Under Stress](#performance-under-stress)
  - [Error Handling](#error-handling)
- [Testing and Calibrating](#testing-and-calibrating)
  - [Performance Evaluation](#performance-evaluation)
    - [Configuration File](#configuration-file)
    - [Command Line](#command-line)
  - [Determining Precision](#determining-precision)
  - [Determining Accuracy](#determining-accuracy)
    - [Measuring ZeroOffset](#measuring-zerooffset)
  - [Test Software](#test-software)
    - [Compiling the Kernel](#compiling-the-kernel)
    - [The pps-timer Utility](#the-pps-timer-utility)
    - [The normal-params Utility](#normal-params-utility)
  - [Measuring zeroOffset with pps-timer](#measuring-zerooffset-with-pps-timer)
  - [Test Results](#test-results)
  - [Test Notes](#test-notes)


# Uses {#uses}

Although the goal of high accuracy computer time keeping has been around since at least the introduction of the Network Time Protocol, general support for high precision time keeping over the Internet is probably not practical. However, GPS reception is available everywhere and in conjunction with a daemon like PPS-Client can be used for that purpose now. Indeed, an Internet search found commercial GPS repeaters that can bring GPS reception indoors with local coverage up to at least 30 meters.

The ability to time synchronize multiple computers with microsecond accuracy is particularly important for small embedded processors like the RPi that can be used to used to construct distributed systems with a large number of individual cores. This works very well when the individual RPis are time synchronized because that makes applications possible that would otherwise be difficult or impractical. One example is a high-speed multiple video camera system that synchronizes the video frames from the individual RPis each handling a different camera.

There are other uses for time synchronized computers: Network administrators might find it useful to have the possibility of making one-way, single-path time measurements of network delays. That becomes possible if the computers at the end points are accurately synchronized to GPS time - which is rarely the case at present. Then too, certain kinds of scientific applications require accurate synchronization to a GSP clock. One sometimes of interest to amateur astronomers is [occultation timing](http://www.poyntsource.com/IOTAmanual/Preview.htm). Another is collection of distributed seismic data with remote computers in the study of earthquakes or for substratum mapping. If computers were routinely provided with microsecond-accuracy time keeping other applications would soon appear.

# PPS-Client High Accuracy Timekeeping {#pps-client-high-accuracy-timekeeping}

The PPS-Client daemon is implemented as a [proportional-integral (PI) controller](https://en.wikipedia.org/wiki/PID_controller) (a.k.a. type 2 servo) with proportional and integral feedback provided each second but with the value of the integral feedback adjusted once per minute. The PI controller model provides a view of time synchronization as a linear feedback system in which gain coefficients can be adjusted to provide a good compromise among stability, transient response and amount of noise introduced in the error signal. 

The error signal is the time difference between the one-second interval provided by a PPS signal and the length of the second reported by the system clock. The noise of concern is the second-to-second variation in time reported by the system because of the corresponding variation of system delay in responding to the PPS interrupt. This variable component of the delay, referred to as jitter, is a combination of clock oscillator jitter, PPS signal jitter and system latency in responding the the interrupt.

Because the error signal has jitter and is being used to control synchronization, the jitter component can have the effect of a *false error signal* that causes the time and frequency synchronization to fluctuate as the controller attempts to follow the jitter. The conventional approach to reducing jitter is to low-pass or median filter the error signal. But filtering has the serious disadvantage that reduction of the jitter must be traded off against time delay introduced by the filter. Additional time delay in the feedback loop unavoidably degrades controller performance. The PPS-Client program uses a much better technique that introduces **no** delay. To reduce the jitter, the time values returned by the system are passed through a hard limiter that clips them before applying them as time corrections.

Indeed, individual time corrections constitute jitter added to a very small time correction. Consequently, each individual correction is mostly jitter and thus is wrong by nearly the amount that it deviates from zero. Because of that, the limiting employed in this system clips the maximum time corrections when the controller has fully stabilized to 1 microsecond.

It might seem that such extreme limiting would remove the desired error signal along with the noise. But that doesn't happen because the true time error is a slowly varying quasi-stationary (DC) level. Limiting only slices off the dynamic (AC) component of the error signal. The DC component remains. (To see what limiting does see Figures 3, 4, and 5 and the relevant discussion.) If the jitter were not limited, the controller would make the sum of the positive and negative jitter zero. That would be undesirable even after filtering because the noise has significant components in time periods that extend to well beyond one minute. Filtering would remove noise only for time intervals within the cut-off region of the filter. Longer period noise would remain. 

On the other hand, instead of zeroing the *sum* of negative and positive jitter and thereby allowing the difference to be introduced as noise, hard limiting causes the controller to make the *number* of positive and negative excursions to be equal around zero. That happens because the clipped positive and negative amplitudes are identical (1 microsecond). Thus, making the sum zero makes the count equal. As a result, the varying magnitude of the jitter around the control point is ignored and the reported time delay of the PPS rising edge adjusts to its median value, i.e., the delay at which there were as many shorter as longer reported delays over the previous minute.

The only disadvantage of hard limiting is that it reduces the amount of time correction that can be applied each each second. But that limitation is easily circumvented by allowing the hard limit level to track the amount of required time correction. This insures that the hard limit level does not prevent larger time corrections when they are necessary.

## Noise and Latency{#noise-and-latency}

While the average accuracy of the PPS signal is on the order of tens of nanoseconds and the jitter standard deviation in the PPS signal is on the order of a fraction of a microsecond, application processors are not particularly suited to maintaining the system time to that order of accuracy. The difficulty is, of course, that typically, an application processor has dozens to hundreds of applications all running at the same time and all competing for processor time. As a result, even for real-time applications, unavoidable delays occur both in responding to an interrupt and also in timestamping it after it has been received. These delays set the limit to processor time accuracy.

Nevertheless, it is perhaps remarkable that it is possible to achieve microsecond accuracy by characterizing the components of time error and finding methods of reducing or compensating for most of these components. For that reason, this section will identify the contributing components of time error: PPS latency and the latency variation described above as jitter.

Time error, defined as the measurable error between PPS time and the time reported on the local clock, is the sum of a static constant component, the **intrinsic PPS interrupt delay**, and three identifiable noise components. The intrinsic PPS delay is defined as the minimum time difference between the assertion of the PPS signal as an interrupt and the timestamp record of it provided by the PPS driver. Intrinsic PPS Delay can be [measured directly in software](#measuring-zerooffset). For that reason it can be measured on any processor running Linux, even those without direct GPIO access.

The jitter components of time error are

Type 1: A **variable delay** component with a time constant up to several minutes introduced primarily by

<BLOCKQUOTE>a. processor delay in either responding to the PPS interrupt or in generating the timestamp both resulting from ongoing activity among the many applications running on the processor that compete with PPS-Client for processor time and<BR>

b. slow drift (and occasionally abrupt micro-shifts) in the system clock oscillator frequency caused by the 1/f nature of [flicker noise](https://en.wikipedia.org/wiki/Flicker_noise) in the clock oscillator,
</BLOCKQUOTE>

Type 2: An approximately [normally-distributed](https://en.wikipedia.org/wiki/Normal_distribution) component mixed with a [half-normal](https://en.wikipedia.org/wiki/Half-normal_distribution) random component composed of

<BLOCKQUOTE>a. second-by-second delay changes introduced as in 1a above with a later-time half-normal noise contribution,<BR>

b. normally distributed jitter from flicker noise in the system clock oscillator and<BR>
    
c. normally distributed jitter in the PPS signal source.</BLOCKQUOTE>
    
Type 3: Sporadically occurring spikes of longer duration latency occurring when the scheduler is unable to immediately provide the requested time slot to PPS-Client.

Type 1a and Type 2a noise can both be eliminated or largely removed by changing the application environment in which pps-client is running. However, this is not always practical or desirable. More about this [at the end of this document](#test-notes). Also notice that Type 1a noise and Type 2a noise can only make the PPS delay longer than the intrinsic PPS interrupt delay and not shorter. This characteristic can help to identify these components.

Because Type 1b noise is slowly varying it is removed by the PPS-Client control loop.

The intrinsic PPS interrupt delay and the Type 1a variable delay component jointly determine the moment to moment zeroOffset. Testing has revealed that the Type 1a noise is [usually small](#feedback-controller) in most operating environments.

The intrinsic PPS interrupt delay rounded to the nearest microsecond is the `G.zeroOffset` correction value that is incorporated into the PPS-Client code. 

Type 3, described as [latency spikes](#latency-spikes), is removed by excluding it from the controller feedback loop.

### The Timeline{#the-timeline}

On the other hand, **the sum of Type 2b and Type 2c noise is an irreducible noise component of time error**. In fact these components characterize a continuous random variable that we will call the *timeline*. The *timeline* is the reported time at each instant on the local system clock. It is useful to think of the *timeline* as the kind of line that would result if one were to draw a straight line between two widely separated points on a white board. Unless the hand that drew the line was particularly skillful, that line might not be very straight. But the line *would* connect the two points. The two points that the *timeline* connects are the beginning and end of each second on the local clock. 

But in between those two points the *timeline* is ruled by Type 2b and 2c noise which is a slowly changing quantity over the interval of a second. The *timeline* cannot be corrected because it is a random value in each second that is observed only after it has occurred and the value in the next second will also be random and, consequently, cannot be predicted. That is what makes it necessary to characterize the *timeline* or the time precision of the local clock, as a statistical quantity. PPS-Client times and synchronizes the *timeline* only at the beginning of each second and PPS-Client synchronizes the *timeline* to the time of occurrence of the PPS only as reported on the local clock. 

However, the *timeline* *can* be characterized by statistical measurements and those measurements then define the accuracy of second-to-second clock synchronization to the PPS. For example, for ten RPi4 processors that were [tested](#test-results), the standard deviation of the *timeline* was very close to 0.5 microsecond SD and the *timeline* was found to be [normally distributed](https://en.wikipedia.org/wiki/Normal_distribution). This can be interpreted in various ways such as: With probability 0.68, a time measured on the local clock was within 0.5 microsecond of the PPS. Or, using the [three sigma rule](https://en.wikipedia.org/wiki/Normal_distribution), that the local clock error to the PPS was more than +/- 2.0 microseconds only about once in 15,789 timings.

To see how time errors present in practice, typical measured performance of Raspberry Pi processors is shown next.

### Raspberry Pi 3 {#raspberry-pi-3}

Raspberry Pi 3 Type 2 noise is shown in Figure 2a for an RPi 3 unit showing typical characteristics from a batch of 10 test units. 

<a name="timed-event"></a>
<center>![Raspberry Pi 3 Jitter Distribution](pps-jitter-distrib-RPi3.png)</center>

Figure 2a is data that was captured from the test unit over 24 hours to the file <b>/var/local/pps-jitter-distrib</b> by setting *jitter-distrib=enable* in <b>/etc/pps-client.conf</b> and is typical data that can be easily generated on any RPi.

Figure 2a shows a delay peak at zero (relative to the `G.zeroOffset` value) followed by infrequent sporadic Type 3 latency spikes in the log plot. The nearly [normal](https://en.wikipedia.org/wiki/Normal_distribution) distribution of the *timeline* is also quite evident. This distribution is a combination of all three components of Type 2 noise..

### Raspberry Pi 4 {#raspberry-pi-4}
Raspberry Pi 4 PPS Type 2 noise is shown in Figure 2b for an RPi 4 unit showing typical characteristics from a batch of ten test units.

<center>![Raspberry Pi 4 Jitter Distribution](pps-jitter-distrib-RPi4.png)</center>

Figure 2b is PPS jitter that was captured from the test unit over 24 hours to the file <b>/var/local/pps-jitter-distrib</b> by setting `jitter-distrib=enable` in <b>/etc/pps-client.conf</b> and is typical data that is easily generated on any RPi 4.

Of the three components of Type 2 noise, it is sometimes possible to see the Type 2a noise separately from the others in a plot of interrupt delay <I>d<sub>int</sub></I> as in Figure 2c (from RPi4#7 below). Unfortunately, there is a still a small amount of jitter in the direction of earlier time introduced by the measurement which makes it impossible to tell whether the bin at 4.5 microseconds is mostly noise or a part of the Type 2a distribution. But ignoring that, this is a good illustration of Type 2a noise.

Because this interrupt is generated entirely within Raspberry Pi 4, it is unaffected by the *timeline* because the measurement is made internal to the processor where time appears to be linear. Any time measurement made totally within the processor is blind to the *timeline* variations of its own clock oscillator because time is being measured relative to a time reference warped by that same *timeline*.

<center>![Interrupt Delay](interrupt-delay.png)</center>

The interrupt delay distribution was generated with a utility designed to measure delays. The delay noise is almost entirely Type 2a noise with a standard deviation of 1 microsecond on this processor. The significance of interrupt delay is that it is always added to any interrupt measurement made on a GPIO pin. The maximum value for this distribution is 4.5 microseconds. This value must subtracted from the recorded clock time to get a true time for the interrupt on the local clock.

### Latency Spikes {#latency-spikes}

A typical latency spike is evident in the PPS-Client status printout shown below which shows low jitter values except for the latency spike in the middle line of the image.

<center>![Jitter Spike in Status Printout](jitter-spike.png)</center>


Since latency spikes are easily identified by the length of delay, they are removed by suspending controller time and frequency updating when delay duration equals or exceeds the controller `NOISE_LEVEL_MIN` value which is hard coded to 4 microseconds. From the SD values of the [normally-distributed](https://en.wikipedia.org/wiki/Normal_distribution) Type 2 noise determined above, that is jitter starting at 3 to 4 standard deviations from the center of the [normally-distributed](https://en.wikipedia.org/wiki/Normal_distribution) Type 2 noise region.

# The PPS-Client Controller {#the-pps-client-controller}

This section will only be of interest to people who want to understand how the software functions. With previous discussion as background, the sections below provide a cross-referenced summary of the source code. While Doxygen does a credible job of cross-referencing, this summary probably only makes sense while referencing a side-by-side open copy of the source code set up as a project in a cross-referencing editor like [Eclipse](http://www.eclipse.org/cdt/).

The PPS-Client controller can be thought of as consisting of two conceptually different components: A feedback controller and a feedforward compensator.

## Feedback Controller {#feedback-controller}

The PPS-Client controller algorithm processes timestamps of interrupts from a hardware GPIO pin triggered by the rising edge of a PPS signal. These PPS timestamps are recorded by the `pps-gpio` Linux kernel device driver. However, there is a time delay between the assertion of the PPS signal edge and the time recorded by the timestamp. We can call this the intrinsic PPS delay <I>d<sub>pps</sub></I>. Exclusive of this delay there is also, in principle, an offset introduced by type 2a noise that can prevent the servo from fully settling to a true zero time point. We can call this the settling offset <I>d<sub>noise</sub></I>. This means that in the absence of any correction value, the system time clock offset is 

<center><I>t<sub>ofs</sub></I> = <I>d<sub>pps</sub></I> + <I>d<sub>noise</sub></I></center>

However, even under noisy conditions where there are many delay spikes, <I>d<sub>noise</sub></I> is very low. The reason for that can be explained with a jitter distribution collected from a Raspberry Pi 3 over 24 hours:

	-6 0
	-5 9
	-4 18
	-3 43
	-2 1391
	-1 17808
	0 47557
	1 15421
	2 3578
	3 332
	----------
	4 39
	5 25
	6 21
	7 22
	8 34
	9 37
	10 37
	11 11
	12 6
	13 3
	14 0
	15 0
	16 1
	17 0

Although there are many jitter spikes, PPS-Client ignored all that were beyond 3 microseconds (indicated by the dotted line) and settled on the median of the time samples below 4 microseconds. Now in the absence of any Type 2a noise, the distribution would have been perfectly symmetric around zero and the median of the distribution would have agreed perfectly with its maximum value. We see that is not quite the case. Because the value at -1 is larger than the the value at 1, we can expect that the maximum value is actually earlier than zero. With a software tool like the [normal-params](#normal-params-utility) utility that can accurately estimate the position of the maximum we get 

	$ normal-params -1 17808 0 47557 1 15421 85825
	Relative to the best fit normal distribution:
	maximum:  -0.033169
	stddev: 0.639956
	Relative fit of samples: 0.986724

Which establishes that the median at zero is actually delayed from the true maximum of a normal distribution by about 0.03 microsecond. That is the average value of the settling noise, <I>d<sub>noise</sub></I>. This is typical performance of PPS-Client running on Raspberry Pi processors in unmodified process environments. 

Because the settling noise is so low it can be ignored. Thus, a constant `G.zeroOffset`, equal to the intrinsic PPS delay, is inserted into the controller feedback loop to remove <I>t<sub>ofs</sub></I>. So that we have,

<center><I>`G.zeroOffset`</I> = <I>d<sub>pps</sub></I></center>

which may alternatively expressed as

<center><I>`G.zeroOffset`</I> = <I>`G.ppsTimestamp`</I> - <I>t<sub>pps</sub></I></center>

where <I>t<sub>pps</sub></I> is the assertion time of the PPS interrupt on the uncorrected local clock. But when the `G.zeroOffset` correction is made and the servo is in equilibrium then the assertion time of the PPS <I>t<sub>pps</sub></I> is zero on the system clock so that

<center><I>`G.zeroOffset`</I> = <I>`G.ppsTimestamp`</I></center>

With that equation in mind, what happens in the controller? The `makeTimeCorrection()` routine is the central controller routine and it sleeps in the timer loop of the function, `waitForPPS()` and inside the `readPPS_SetTime()` routine, until a PPS timestamp becomes available from the <b>pps-gpio</b> device driver. At that instant the timestamp is passed into `makeTimeCorrection()` where the fractional part of the second becomes available as the variable `G.ppsTimestamp` which is converted to the controller error variable as,

<center><I>`G.rawError` = `G.ppsTimestamp` - `G.zeroOffset`</I></center>

After cleaning up `G.rawError` as described below, the feedback loop in the servo reduces `G.rawError` to zero,

<center><I>0 = `G.ppsTimestamp` - `G.zeroOffset`</I></center>

effectively setting system time to zero at the actual time of occurrence of the asserted PPS interrupt. How `G.zeroOffset` is determined is described in the last half of this document.

Each `G.rawError` is a time measurement corrupted by jitter. Thus the value of `G.rawError` generated each second can be significantly different from the true time correction. To extract the time correction, `G.rawError` is passed into the `removeNoise()` routine that contains the first noise processing routine, `detectDelaySpike()`, that determines (when `G.rawError` is sufficiently small) whether `G.rawError` is spike noise, in which case further processing in the current second is skipped. If it's not spike noise, the average time slew, `G.avgSlew` is updated by `G.rawError`. The `G.avgSlew` value along with the average time correction up to the current second, `G.avgCorrection`, determines the current hard limit value that will be applied in the final noise removal routine, `clampJitter()`. Then `G.rawError`, limited by `clampJitter()`, is returned from `removeNoise()` as `G.zeroError` which is then modified by the proportional gain value and then sign reversed to generate `G.timeCorrection` for the current second.

The sign reversal on `G.timeCorrection` is necessary in order to provide a proportional control step that subtracts the time correction from the current time slew, making a time slew that is too large smaller and vice versa. That happens by passing `G.timeCorrection` to the system `adjtimex()` function which slews the time by exactly that value unless the magnitude is greater than about 500 μsecs, in which case the slew adjustment is restricted to 500 μsecs by `adjtimex()`. This is usually what happens when PPS-Client starts. After several minutes of 500 μsec steps, `G.timeCorrection` will be in a range to allow the integral control step to begin.

But before the integral control step can begin, an average of the second-by-second time corrections over the previous minute must be available to form the integral. That average is constructed in the `getMovingAverage()` routine which sequences the time corrections through a circular buffer `G.correctionFifo` and simultaneously generates a rolling sum in `G.correctionAccum` which is scaled to form a rolling average of time corrections that is returned as `G.avgCorrection` by `getMovingAverage()` each second. At feedback convergence, the rolling sum of *unit* `G.timeCorrection` values makes `G.avgCorrection` the *median* of `G.timeCorrection` values. 

At the end of each minute the integral control step in the `makeAverageIntegral()` routine sums `G.avgCorrection` into one of 10 accumulators `G.integral[i]` each of which accumulates a separate integral that is offset by one second from the others. At the end of the minute those are averaged into `G.avgIntegral`.

Also at the end of the minute (actually after 60 time corrections have been averaged as determined by `integralIsReady()`), `G.avgIntegral` is returned from `getIntegral()` and multiplied by `G.integralGain` to create `G.freqOffset` which, after scaling by `ADJTIMEX_SCALE` that is required by `adjtimex()`, is passed to `adjtimex()` to provide the integral control. 

## Feedforward Compensation {#feedforward-compensation}

The specific purpose of the feedback controller described above is to adjust the system time second by second to satisfy this local "equation of time":

<center><I>0 = median(G.ppsTimestamp) - G.zeroOffset</I></center>

It does that by setting the local clock so that the difference between `G.zeroOffset` and the median of `G.ppsTimestamp` is zero. For this to succeed in adjusting the local time to the PPS, the median of the time at which the system received the rising edge of the PPS interrupt must be `G.zeroOffset` where `G.zeroOffset` is a positive number. In other words the system received the interrupt *earlier* by `G.zeroOffset` microseconds than the recorded time returned by the timestamp. 

While it would be desirable to have a measurement of `G.zeroOffset`, that can't be done by the feedback controller. As indicated by the equation, all the controller can do is satisfy the equation of time. In earlier versions of PPS-Client, `G.zeroOffset` was measured by the driver. However the Linux driver does not provide that function. 

So, in the current incarnation of PPS-Client, `G.zeroOffset` is set to constant value (which can be changed in <b>/etc/pps-client.conf</b>). It will be shown in subsequent sections how the value was determined. Consequently, so called feedfoward compensation is provided by a fixed delay value.

Default values, accurate to 1 microsecond, have been determined for Raspberry Pi 3 and Raspberry Pi 4 processors and are automatically used. But the value may be different for other application processors. Or it may be desirable to set a more accurate value. In those cases, the `G.zeroOffset` would have to be separately measured and the value supplied in <b>/etc/pps-client.conf</b>. How to do that is described in [Measuring zeroOffset with pps-timer](#measuring-zerooffset-with-pps-timer).

## Driver {#driver}

The PPS-Client daemon was written entirely with user space code. It uses the <b>pps-gpio</b> driver provided in the Linux kernel.
 
## Controller Behavior on Startup {#controller-behavior-on-startup}

Figure 3 shows the behavior of the controller when PPS-Client is started. The figure shows frequency offset and corresponding time corrections recorded to <b>/etc/local/pps-offsets</b> when saving this file is requested from the [command line](#command-line) as <b>pps-offsets</b>.

During the first 120 seconds (not shown in the figure), the controller made time corrections to get the time offset into a reasonable range but made no frequency corrections. Frequency offset correction was enabled at sequence number (second) 120. Over the next 600 seconds the PPS-Client controller adjusted the frequency offset to bring the system clock frequency into sync with the PPS.

<center>![Offsets to 720 secs](pps-offsets-to-720.png)</center>

The expanded view of the same data over the first 300 seconds in Figure 4 shows in more detail what is happening. In general, proportional feedback is correcting the time by the (gain-scaled and limited) time error measured each second. The integral feedback is updated each minute with the average of the time corrections over the previous minute. As the integral is formed the result is to move the frequency offset minute by minute towards synchronization. It should be clear that in any minute where the system clock frequency is not perfectly in sync with the PPS, the average length of the second will be either longer or shorter than one second. For example, if the average length of the second differed by 1 microsecond from the true length of the second, that would indicate that the system clock was in error by 1 part per million, in which case an adjustment of the frequency offset by 1 ppm might be expected. 

<center>![Offsets to 300 secs](pps-offsets-to-300.png)</center>

Now notice that, in the minute between sequence number 120 and 180, there is a clearly visible positive bias in the time correction values. Averaging those time corrections (literally adding them from the figure and dividing by 60), gets a 3.22 microsecond average bias indicating that the system clock is in error by 3.22 parts per million over that minute. However, in the next minute (between 180 and 240), the frequency offset is changed by only about 2 ppm. In other words, frequency offset change is only about 0.632 of the amount needed to do a full frequency correction and, moreover, it is easily verified that same fractional correction is made in every succeeding minute. 

The 0.632 fractional adjustment is the damping ratio fixed by the integral gain of the PI controller. That damping value has been chosen to keep the loop stable and, in fact, to set it below the maximum acquire rate provided by a ratio of 1 which corresponds to the full frequency correction.

But why not apply the full frequency correction each second? The reason is that the correction is always made too late. It would have been correct for the minute in which it was measured but by the time it has been measured it can only be an estimate for the next minute. If the estimate is even slightly too large then the estimation error will be integrated along with the estimate and will become progressively larger in each succeeding second until the result is an oscillation of the frequency around its stable value. The design decision is that, considering noise and other system uncertainties, it is better to have the controller acquire more slowly with a lower damping value than to risk oscillation with a higher value.

Once the controller has acquired, it continues to average the time errors that occurred over the past minute and to apply the scaled integral of the average as the frequency correction for the next minute. So theoretically the controller never acquires. Rather it is constantly chasing the value to be acquired with a somewhat low estimate of that value. This seems to argue for a [Zeno's paradox](https://en.wikipedia.org/wiki/Zeno%27s_paradoxes). In practice, however, the difference between the estimate and the target value soon drops below the noise level so that any practical measurement would indicate that the controller had, indeed, acquired.

The startup transient in Figure 3 is the largest adjustment in frequency the controller ever needs to make and in order to make that adjustment relatively large time corrections are necessary. Once the control loop has acquired, however, then by design the time corrections will exceed 1 microsecond only when the controller must make larger than expected frequency offset corrections. In that case, the controller will simply adjust to larger corrections by raising its hard limit level. 

## Performance Under Stress {#performance-under-stress}

To get some idea of what the worst case corrections might be, Figure 5 demonstrates how the PPS-Client control loop responds to stress. In this case a full processor load (100% usage on all four cores) was suddenly applied at sequence number 1900. The loading raised the processor temperature causing a total shift in frequency offset of about 1.7 ppm from onset to a stable value near sequence number 2500. The time corrections increased to 2 microseconds only in the region of steepest ascent. Since the transients caused by extreme load changes occur infrequently, it is likely that a time correction more than 1 microsecond would only occasionally occur in normal operation. Moreover it is unlikely that a precision time measurement would be required simultaneously with the onset of full processor load.

<center>![PPS offsets to Stress](pps-offsets-stress.png)</center>

## Error Handling {#error-handling}

All trapped errors are reported to the log file <b>/var/log/pps-client.log</b>. In addition to the usual suspects, PPS-Client also reports PPS dropouts. While most of the reported errors were intended for use in development, some are useful when things go wrong with the PPS signal. So the error file is the best first place to look when that happens.

# Testing and Calibrating {#testing-and-calibrating}

Before performing any test, please make sure that the test environment is clean. At a minimum, if not starting fresh, **reboot the RPi's that are being used in the tests**. This can eliminate a lot of unexpected problems.

The simplest test is to run PPS-Client and verify with the status printout that the controller locks to the PPS signal to a precision of one microsecond. From a terminal, that can be done at any time while PPS-Client is running with,

    $ pps-client -v

That runs a secondary copy of PPS-Client that just displays a status printout that the PPS-Client daemon continuously generates. When PPS-Client starts up you can expect to see something like the following in the status printout:

<center>![Status Printout on Startup](StatusPrintoutOnStart.png)</center>

The `jitter` value is showing the fractional second offset of the PPS signal according to the system clock. That value will decrease second by second as the controller locks to the PPS signal. Within 10 to 20 minutes the status printout will look like this:

<center>![Status Printout after 10 Min](StatusPrintoutAt10Min.png)</center>

The `jitter` is displaying small numbers. The time of the rising edge of the PPS signal is shown in the second column. The `clamp` value on the far right indicates that the maximum time correction applied to the system clock is being limited to +/- one microsecond. The system clock is synchronized to the PPS signal to a precision of one microsecond.

It can take as long as 20 minutes for PPS-Client to fully acquire the first time it runs. That happens if the `jitter` shown in the status printout is on the order of 100,000 microseconds or more. It's quite common for the NTP fractional second to be off by that amount or more. In this case PPS-Client may restart several times as it slowly reduces the `jitter` offset. That happens because the system function `adjtimex()` that PPS-Client calls internally prevents time changes of more than about 500 microseconds each second.

Here are the parameters shown in the status printout:

 * First two columns - date and time of the rising edge of the PPS signal.
 * Third column - the sequence number giving the total PPS interrupts received since PPS-Client was started.
 * `jitter` - the time deviation in microseconds recorded at the reception of the PPS interrupt.
 * `freqOffset` - the frequency offset applied to the system clock in parts per million of the system clock frequency in order to synchronize the system clock to the PPS signal.
 * `avgCorrection` - the time corrections (in microseconds) averaged over the previous minute.
 * `clamp` - the hard limit (in microseconds) applied to the raw time error to convert it to a time correction.

If NIST is being used to provide whole-second time of day then about every 17 minutes, an NIST time query will be made and the results of that will be shown, but will have no effect unless a time update is required. The infrequency of time checks is a requirement of using the NIST service. However, if GPS is used to provide whole seconds, time checks are made every 10 seconds but no message is displayed.

To stop the display type ctrl-c.

The PPS-Client daemon writes the timestamp and sequence number of the PPS rising edge to an in-memory file that changes every second. You can verify that the time is being controlled and that the controller is currently active by entering this a few times:

    $ cat /run/shm/pps-assert

Executing that twice in succession would generate something like this:

    pi@raspberrypi:~ $ cat /run/shm/pps-assert
    1460044256.000001#173028
    pi@raspberrypi:~ $ cat /run/shm/pps-assert
    1460044259.000000#173031

The timestamp is displayed in seconds to the nearest microsecond. This is probably the most foolproof way of determining that PPS-Client is currently running. If you get the same numbers for the whole second value twice in succession or none at all you know it's not.

Another way to tell that PPS-Client is running is to get the process id with,

    $ pidof pps-client

which will only return a PID if PPS-Client is an active process.

## Performance Evaluation {#performance-evaluation}

Data can be collected while PPS-Client is running either by setting specific data files to be saved in the PPS-Client configuration file or by requesting others from the command line of a terminal that is communicating with the RPi.

### Configuration File {#configuration-file}

Data that can be collected using the configuration file is enabled with settings in <b>/etc/pps-client.conf</b>. These instruct the PPS-Client daemon to generate data files, some of which provided the data used to generate the spreadsheet graphs shown on this page and in the project README file. Generating a particular file requires setting a flag. All of these files are disabled by default. But they can be enabled or disabled at any time, including while the PPS-Client daemon is running, by editing and saving the config file. Here are the flags you can use to enable them:

* <b>error-distrib=enable</b> generates <b>/var/local/pps-error-distrib-forming</b> which contains the currently forming distribution of time corrections to the system clock. When 24 hours of corrections have been accumulated, these are transferred to <b>/var/local/pps-error-distrib</b> which contains the cumulative distribution of time corrections applied to the system clock over 24 hours.

* <b>jitter-distrib=enable</b> generates <b>/var/local/pps-jitter-distrib-forming</b> which contains the currently forming distribution of jitter values. When this distribution is sufficiently free of jitter that exceeds 3 microseconds then this is also a distribution of the *timeline*. When 24 hours of corrections have been accumulated, these are transferred to <b>/var/local/pps-jitter-distrib</b> which contains the cumulative distribution of all time (jitter) values recorded at reception of the PPS interrupt over 24 hours.

Note that while the turnover interval for some of the files above is given as 24 hours, the interval will usually be slightly longer than 24 hours because PPS-Client runs on an internal count, `G.activeCount`, that does not count lost PPS interrupts or skipped jitter spikes.

### Command Line {#command-line}

Some of the data that can be saved by a running PPS-Client daemon is of the on-demand type. This is enabled by executing PPS-Client with the `-s` flag while the daemon is running. Please note that this data is dumped to a file only when you specifically request it as below. It is continuously updated in the daemon but the file is not automatically updated like the files requested in <b>/etc/pps-client.conf</b>. For example,

    $ pps-client -s frequency-vars

will return something like this

    pps-client v2.0.0 is running.
    Writing to default file: /var/local/pps-frequency-vars
    
The file will be copied from data in PPS-Client as the data was when the command above was issued. The same is true for the other files. If the data is of the 24 hour type the daemon must have run at least that long for you to get a complete file.

You can write to a different filename or location by using the `-f` flag followed by the 
desired path and filename:

    $ pps-client -s frequency-vars -f data/freq-vars-01.txt

These files will be incomplete if PPS-Client has not been running for some corresponding minimum time. The specified directories must already exist. 

You may also include the `-v` flag if you want the status display to start as soon as the requested file is written to disk.

As an aid to remembering what can be requested, omitting the type of data will print a list of what's available. Currently that would result in something like,

	PPS-Client v2.0.0 is running.
	Error: Missing argument for -s.
	Accepts any of these:
	rawError
	frequency-vars
	pps-offsets

described as,

* `rawError` writes an exponentially decaying distribution of unprocessed PPS jitter values as they enter the controller. These are relative to the current value of `G.zeroOffset`. Jitter values added to the distribution has a half-life of one hour. So the distribution is almost completely refreshed every four to five hours.

* `frequency-vars` writes the last 24 hours of **clock frequency offset** and [**Allan deviation**](https://en.wikipedia.org/wiki/Allan_variance) of one-minute samples in each five-minute interval indexed by the timestamp at each interval.

* `pps-offsets` writes the previous 10 minutes of recorded time offsets and applied frequency offsets indexed by the sequence number (`G.seq_num`) each second.

The **clock frequency offset** is the offset in parts per million of the clock oscillator frequency that was applied to the clock oscillator to keep the clock synchronized to the PPS signal. 

The [**Allan deviation**](https://en.wikipedia.org/wiki/Allan_variance) is also plotted in parts per million and can be interpreted to be the average ([RMS](https://en.wikipedia.org/wiki/Root_mean_square)) frequency drift (in parts per million per minute) between adjacent frequency samples one minute apart measured at each five minute interval. Parts per million of oscillator frequency drift corresponds directly to microseconds of error; so the Allan deviation can also be interpreted as average (RMS) microseconds of minute to minute clock drift between frequency updates.

## Determining Precision {#determining-precision}

Before we can determine time accuracy we first need to determine precision. Precision is determined by the [*timeline*](#the-timeline). With sufficient precision, we can measure *time accuracy* which is defined as the absolute time error at any point in time relative to the PPS time clock provided by GPS satellites.

<center>![Interpretation of accuracy and precision](time.png)</center>

Figure 7 is a distribution of time corrections made by the PPS-Client controller to the system clock. This data was collected over a 24 hour period by removing the comment tag from <b>#error-distrib=enable</b> in <b>/etc/pps-client.conf</b>. After 24 hours the filename would be <b>/var/local/pps-error-distrib</b>.

<center>![24 Hour Offset Distribution](offset-distrib.png)</center>

This data was captured from a Raspberry Pi 4 running Raspberry Pi OS on a standard 5.4.51-v7l+ Linux kernel. Unlike many of the other plots of a similar appearance scattered throughout this document, this is a plot of a discrete variable that can only take on integer values corresponding to whole second corrections made by the proportional element of the PPS-Client controller. 

The prominent central peak reports the number of times in 24 hours that *timeline* wandering remained within a boundary of +/- 1 microsecond. The peak to the left reports the number of times a microsecond had to be subtracted from the *timeline* to keep it within those bounds and added, similarly, for the peak on the right. The important point is that the time corrections required to keep the *timeline* synchronized to the rising edge of the PPS signal never exceeded 1 microsecond in this 24 hour period. This was true for all Raspberry  Pi units tested.

The second important point is that the *timeline* determines time measurement *precision*. As the PPS-Client controller maintained an average straight time line at the beginning of each second, the *timeline*, which is modified by the variation in the clock oscillator frequency (and PPS variation), shifted the local time above and below a straight line by the value of the the *timeline* at every other point in the second. However at the end of each second if the the *timeline* was greater than 1 microsecond then the negative 1 microsecond correction made by the PPS-Client controller reduced the value of the the *timeline* by 1 microsecond and conversely. By this means the *timeline* was "gently encouraged" to remain within the one microsecond boundaries most of the time.  

As described [above](#the-timeline), the *timeline* is a random variable best described as normally distributed with a [standard deviation](#test-results) of 0.5 microsecond for RPi4 and 0.75 microsecond for RPi3. Because the *timeline* is forced to remain near zero relative to the PPS each time it occurs, that statistical description is also the *precision*.


However the *timeline* has no knowledge of the whole second time of day. To the extent *that* is properly determined then the *precision* is also the *accuracy* of the time.


## Determining Accuracy {#determining-accuracy}

This version of PPS-Client uses the PPS driver (<b>pps-gpio.ko</b>) provided in Linux. That driver does not provide a mechanism for measuring the time delay between the asserted edge of the PPS signal and the time recorded for it in the kernel. In most of the applications of PPS-Client, the uncertainty of that value probably won't matter. But there *are* some applications that use processor GPIO pins to measure the times of external events. 
In such applications it can be useful to determine the time difference between the asserted PPS edge and its reported timestamp as accurately as possible. Fortunately, that value, the PPS delay, appears to be nearly constant for a particular choice of processor and operating system and can be measured. So once we determine the PPS delay we can use that delay value as a constant correction for `G.zeroOffset`. 
Most users will not be interested in performing these tests. This information is provided to show how accuracy was determined and, for those who are interested, how to replicate the measurements on the Raspberry Pi or, with appropriate modifications, other processors.
The next section describes the method used to determine `G.zeroOffset`.

### Measuring ZeroOffset {#measuring-zerooffset}

The goal in the following sections is to measure `G.zeroOffset` to the nearest microsecond. Consequently, components of and corrections to the value of `G.zeroOffset` are timed to tenths of a microsecond. The value of `G.zeroOffset` that we are seeking is exclusive of any jitter that the system will add to the value. For that reason, long averages of parameters are made specifically to determine mean values that average out latency and jitter. It might seem that this is a lot of trouble to go to just establish a microsecond value for `G.zeroOffset`. However the measurements have established that the Raspberry Pi 4 is a very good time keeper, capable of measuring the time of an external event on a gpio pin to microsecond accuracy.

On the Raspberry Pi and other Linux processors as well, the `G.zeroOffset` delay can be measured entirely in software. That means that, at least in principle, <b>`G.zeroOffset` can be measured and calibrated on any Linux system</b>. 

For purposes of this discussion, the PPS interrupt is defined as the time of the rising edge of the PPS signal. The software application that measures `G.zeroOffset` is [**pps-timer**](#the-pps-timer-utility). The app uses a kernel time function, *ktime_get_real_fast_ns()* to probe the earliest time that the PPS interrupt triggers its interrupt service routine. The ISR suspends other processes running on the same core and, in particular, suspends **pps-timer**. The *ktime_get_real_fast_ns()* execution time is on the order of 0.2 microseconds or less if it executes before the PPS interrupt service routine, but is delayed and is much longer if it begins to execute after the PPS ISR begins executing. The time reported by *ktime_get_real_fast_ns()* before it was delayed is recorded and is interpreted to be the time that the PPS interrupt occurred to within about a tenth of a microsecond. 

When the time of the PPS interrupt is timed with pps-timer on a processor running with **zeroOffset=0** in <b>/etc/pps-client.conf</b>, the PPS interrupt time will be a time somewhere in the range of about -3 to -12 microseconds.

The plot in Figure 8 shows how the PPS interrupt time looks on a typical Raspberry Pi 4. The graph appears to be a normal distribution. But that is incorrect. It is actually a super-position of two half normal distributions. The distribution was collected by the pps-timer utility while monitoring the PPS interrupt time. Neither pps-timer nor pps-client can actually cause a distribution tail that moves in the direction of decreasing clock time. The negative-going tail is caused by pps-timer being bumped by jitter toward later times relative to pps-client, causing time values to appear earlier in time. Times later than the maximum of the distribution are introduced by jitter experienced by pps-client but not by pps-timer. 

The maximum of the forward half-normal distribution is -4.74 microseconds and the standard distribution is 0.51 second. Those values were determined by the [normal-params utility](#normal-params-utility).

<center>![PPS Time RPi 4](pps-time.png)</center>

Figure 9 is typical of a Raspberry Pi 3. It is easier to see the two separate distributions in this figure. The maximum of the forward half-normal distribution in this case is -7.32 and the standard deviation is 0.62.

<center>![PPS Time RPi 3](pps-time-rpi3.png)</center>

These plots are of 86,400 accumulated PPS interrupt times collected over a period of 24 hours. They are representative of the data that is used to determine `G.zeroOffset`. 

The local clock is not corrected which means that it is literally synchronized to the timestamp of the PPS signal which is taken to be time zero. Clearly, from Figure 8, the actual time of occurrence of the PPS signal for this Raspberry Pi 4 is -4.74 microseconds earlier. To correct the local clock so that the time of the PPS signal is at zero, we need to add the positive value corresponding to the negative offset seen in Figure 8.

Evaluating distributions of the kind shown in Figure 8 and Figure 9 is described in [Measuring zeroOffset with pps-timer](#measuring-zerooffset-with-pps-timer).

## Test Software {#test-software}

With one exception, [the normal-params utility](#normal-params-utility), the test software requires kernel drivers that can only be compiled by the kernel build system. Since most users will not be interested in performing the measurements made by the test software themselves, applications are provided only in source form. To get the kernel build system, a Linux kernel must be downloaded, compiled and installed. On a Raspberry Pi 4, this can be done in about an hour.

### Compiling the Kernel {#compiling-the-kernel}

The build steps below are intended for a kernel that has been compiled with CONFIG_MODVERSIONS enabled. This includes current versions of Raspbian and Raspberry Pi OS. To verify that a specific version has CONFIG_MODVERSIONS enabled run,

	~ $ sudo modprobe configs
	~ $ zgrep MODVERSIONS /proc/config.gz

In order to use the build steps the result must be,

	~ $ CONFIG_MODVERSIONS=y

Before compiling the kernel insure that your system is up to date.

	~ $ sudo apt-get update
	~ $ sudo apt-get upgrade
	~ $ sudo reboot

In your home folder on your Raspberry Pi, you might want to first set up a build folder:

	~ $ mkdir rpi
	~ $ cd rpi

Then get missing dependencies required for compiling:

	~/rpi $ sudo apt-get install bc git bison flex libssl-dev make build-essential

For retrieving the Linux source get the rpi-source script:

	~/rpi $ sudo wget https://raw.githubusercontent.com/notro/rpi-source/master/rpi-source -O /usr/bin/rpi-source && sudo chmod +x /usr/bin/rpi-source && /usr/bin/rpi-source -q --tag-update

Run the script as shown next. This will download the Linux source that matches the installed version of Linux on your RPi:

	~/rpi $ rpi-source -d ./ --nomake --delete

You may get a message that "ncurses-devel is NOT installed." You won't need it. The setup doesn't use menuconfig.

On the RPi,

	~/rpi $ cd linux

For Raspberry Pi 4 do,

	~/rpi/linux $ KERNEL=kernel7l

For all previous Raspberry Pi hardware versions do,

	~/rpi/linux $ KERNEL=kernel7

Then,

	~/rpi/linux $ make olddefconfig

Now compile and install the kernel (takes about an hour on RPi 4, two hours on RPi 3):

	~/rpi/linux $ make -j4 zImage modules dtbs

If there are no compile errors, the Linux directory will contain a *vmlinux* executable. Then in succession do,

	~/rpi/linux $ sudo make modules_install
	~/rpi/linux $ sudo cp arch/arm/boot/dts/*.dtb /boot/
	~/rpi/linux $ sudo cp arch/arm/boot/dts/overlays/*.dtb* /boot/overlays/
	~/rpi/linux $ sudo cp arch/arm/boot/dts/overlays/README /boot/overlays/
	~/rpi/linux $ sudo cp arch/arm/boot/zImage /boot/$KERNEL.img

Finally, reboot the Pi.


### The pps-timer Utility {#the-pps-timer-utility}

Assuming that you have the PPS-Client directory in the `~/rpi` directory, then to install pps-timer,

	~ $ cd ~/rpi/PPS-Client/utils/pps-timer
	~/rpi/PPS-Client/utils/pps-timer $ make
	~/rpi/PPS-Client/utils/pps-timer $ sudo make install

If you need to recompile pps-timer first do,

	~/rpi/PPS-Client/utils/pps-timer $ sudo make clean

This utility will directly measure `G.zeroOffset`. The measurement errors introduced by pps-timer are much less than a microsecond. The second-to-second accuracy of pps-timer is +/- 0.1 microsecond for the RPi4 and +/- 0.2 microsecond for the RPi3.

Systematic errors were determined by using pps-timer to time an interrupt generated on a GPIO pin at a precisely known time. This was done for ten RPi3 and ten RPi4 processors. Two uncorrected errors were found. The first error is a read delay between the interrupt rising edge time on the GPIO pin and the time recorded by pps-timer. The second error is an increase in the interrupt timestamp reported by the system when pps-timer is timing the interrupt.

For RPi3 the read delay averages 0.12 microsecond and the interrupt time increase averages 0.2 microsecond. Because these errors are in opposite directions, the net error is 0.08 microsecond.

For RPi4 the read delay averages 0.04 microsecond and the interrupt timer increase averages 0.10 microsecond for a net error of 0.06 microsecond.

Because these errors are very small, they are ignored. A LibreOffice Calc spreadsheet, PPS-Timer-Errors.ods, containing the data collected for the twenty Raspberry Pi processors is included in the pps-timer folder.

Measurements collected by pps-timer are written to <b>/var/local/pps-time-distrib-forming</b> and <b>/var/local/prop-delay-distrib-forming</b>. After 24 hours these results are transferred to <b>/var/local/pps-time-distrib</b> and <b>/var/local/prop-delay-distrib</b>.

The pps-timer utility requires no arguments and automatically loads its kernel driver. To run it,

	$ sudo pps-timer

and stop it with ctrl-c. Or run it detached with, 

	$ sudo pps-timer &
	
and stop it from a different terminal with

	$ sudo kill `pidof pps-timer`

### The normal-params Utility {#normal-params-utility}

The normal-params utility, which requires no driver, is automatically installed when pps-client is installed.

The distributions obtained in testing PPS-Client are usually narrow which makes it difficult to estimate peaks and standard deviations. Moreover there is ample evidence that the random component of the PPS-Client distributions is well-modeled by a [normal distribution](https://en.wikipedia.org/wiki/Normal_distribution) but is also binned over a small number of bins. The `normal-params` program makes it possible to directly compute normal distribution parameters from binned values of a sample distribution. 

The program uses a Monte Carlo simulation to fit an ideal [normal distribution](https://en.wikipedia.org/wiki/Normal_distribution) to three binned sample values from the distribution. If the sample distribution departs from normal, there will be a conformance error listed as relative fit which is a measure of the reliability of the calculated values of both ideal mean and ideal SD. Roughly speaking, relative fit is the probability that the samples are actually drawn from a normal distribution.

Bins are centered on the sample x-coordinate values entered to the program. For example the bin at 800000 in the example below extends from 799999.5 to 800000.5. 

The program can be used to determine mean and SD for any of the sample distributions collected in testing PPS-Client including positive-only distributions which fit a [half-normal distribution](https://en.wikipedia.org/wiki/Half-normal_distribution) in the direction of increasing delay or forward time. 

If the distribution is half-normal then enter only completely filled bins in the direction away from the maximum and **use double the number of actual samples** to make normal-params treat the bins as those from the right side of a normal distribution. For example, the interrupt delay distribution in [Figure 2c](#raspberry-pi-4) was evaluated for mean and standard deviation by providing the sample numbers for the sample bins from 5 forward like this (only 43,200 actual samples were actually collected),

	$ normal-params 5 29247 6 11742 7 1846 86400
	Relative to the best fit normal distribution:
	maximum:  4.493750
	stddev: 1.007466
	Relative fit of samples: 0.999728

Bear in mind that it's only a half-normal distribution if nearly all of the samples are in one direction or the other from the maximum.

For a distribution expected to be nearly [normally distributed](https://en.wikipedia.org/wiki/Normal_distribution) the points would usually be selected around the center of the distribution like this:

	$ normal-params 799999 10212 800000 17382 800001 10275 43200
	Relative to the best fit normal distribution:
	maximum:  800000.002945
	stddev: 0.947150
	Relative fit of samples: 0.995888

There is also a third possibility and this is the most common case: Two mixed half-normal distributions in opposite directions from the maximum. This applies to distributions collected by the pps-timer utility where the half of the distribution in the direction of increasing time is the result of Type 2a noise in PPS-Client and the half of the distribution in the direction of decreasing time is the result of Type 2a noise in the pps-timer utility. In this case no doubling of the count for the two half-normal distributions is necessary. For example, for this pps-timer distribution the two half-normal distributions are visually quite evident,

	-8.5 0
	-8.0 0
	-7.5 0
	-7.0 0
	-6.5 20
	-6.0 268
	-5.5 2361
	-5.0 19598
	-4.5 35125
	-4.0 24724
	-3.5 4243
	-3.0 60
	-2.5 0
	-2.0 0
	-1.5 0
	-1.0 0

In the direction of later time (PPS-Client half-normal jitter distribution),

	$ normal-params -4.0 24724 -3.5 4243 -3.0 60 86400
	Relative to the best fit normal distribution:
	maximum:  -4.422881
	stddev: 0.411773
	Relative fit of samples: 0.999139

In the direction of earlier time (pps-timer half-normal jitter distribution),

	$ normal-params -6.5 20 -6.0 268 -5.5 2361 86400
	Relative to the best fit normal distribution:
	maximum:  -4.142910
	stddev: 0.591466
	Relative fit of samples: 0.999969

Because there is an abundance of samples, the normal-params utility accurately resolves maximum and SD of the separate half-normal distributions. But, of course, this assumes that each distribution had 43,200 samples in its half of the distribution which might not be the case. Nevertheless, the approximation is good if the difference in the two distributions is not extreme because the normal-params utility is relatively insensitive to the number of samples in the distribution. For example, changing the number of samples in the distribution by +/- 2000 only changes the position of the maximum by +/- 0.01 microsecond in both of the distributions above.

For very narrow distributions only one or two sample bins might be filled. If there are two bins, the normal-params utility can handle that too. In this case it uses the standard [center of mass](https://en.wikipedia.org/wiki/Center_of_mass) calculation for a system of (two) particles. For example,

	$ normal-params 4519 800000 2301 800001
	Center of mass of the pair of points is 800000.337390.

Of course if only one bin is filled with fewer than one percent of the points scattering on either side then you can be sure that the distribution is entirely within the bin around that point.

## Measuring zeroOffset with pps-timer {#measuring-zerooffset-with-pps-timer}

Open a terminal to the processor you are testing. Make sure that you have compiled the application as described [above](#the-pps-timer-utility). Then verify that PPS Client is running,

	$ pps-client -v
	
If you get the typical pps-client output, ctrl-c, then the first thing you want to do is to set the current `G.zeroOffset` value to zero,

	$ sudo nano /etc/pps-client.conf

Scroll down to 

	#zeroOffset=0

and uncomment it,

	zeroOffset=0
	
Then ctrl-s ctrl-x to save and exit nano. The new value of `G.zeroOffset` will be immediately updated by the pps-client daemon. The pps-timer utility requires no parameters. It will load its driver and immediately start recording samples of the PPS interrupt triggering its ISR.

For for any processor, to load and start the app, just do,

	$ sudo pps-timer &
	
At this point you should see something like,
	
<center>![pps-timer starting](pps-timer-starting.png)</center>

To determine the median PPS time the app collects time samples to a file <b>/var/local/pps-time-distrib-forming</b>. 24 hours later, the distribution is copied to <b>/var/local/pps-time-distrib</b>. Interpreting the file distribution will be discussed next.


## Test Results {#test-results}

Ten Raspberry Pi 3 and ten Raspberry Pi 4 processors were evaluated with pps-timer. The test file of interest is <b>/var/local/pps-time-distrib</b>. After 24 hours, the pps-time-distrib file has accumulated a distribution of 86,400 samples of PPS time. **All of these distributions are mixed half-normal** and must be evaluated as such. In all cases there is a half-normal distribution in the direction of later time caused by Type 2a noise that is jitter in pps-client. That is the distribution corresponding to PPS-Client. There is also a half-normal distribution in the opposite direction corresponding to jitter in pps-timer.

The maximum value of the half-normal distribution provides the jitter-free estimate of the PPS time. The maximum value for each processor was determined with the normal-params utility. For example, for Raspberry Pi 4 unit #1 the <b>/var/local/pps-time-distrib</b> file provided the following distribution,

	-8.0 0
	-7.5 0
	-7.0 0
	-6.5 20
	-6.0 268
	-5.5 2361
	-5.0 19598
	-4.5 35125
	-4.0 24724
	-3.5 4243
	-3.0 60
	-2.5 0
	-2.0 0
	-1.5 0

This is a mixed half-normal distribution in both directions that was plotted as the graph in Figure 8. Three bin values from the later time half-normal distribution were provided to the [normal-params](#normal-params-utility) utility resulting in,

	$ normal-params -4.0 24724 -3.5 4243 -3.0 60 86400
	Relative to the best fit normal distribution:
	maximum:  -4.422881
	stddev: 0.411773
	Relative fit of samples: 0.999139

The maximum value is the best estimate of the PPS rising edge time and that value was rounded and sign inverted to provide `G.zeroOffset`. That is the first line in the table of Raspberry Pi 4 times below. Data for the Raspberry Pi 3 was processed identically.


	Raspberry Pi 3 times in microseconds (Linux v5.4.41):

	UNIT#   PPS_time  time_SD fit  zeroOffset  Timeline_SD   Generation
	---------------------------------------------------------------------------
	RPi3#1   -6.93    0.491  0.999     7          0.708     Model B Rev 1.2
	RPi3#2   -5.67    0.479  0.999     6          0.770     Model B+ Rev 1.3
	RPi3#3   -7.45    0.546  0.999     7          0.729     Model B Rev 1.2
	RPi3#4   -7.31    0.459  0.999     7          0.719     Model B Rev 1.2
	RPi3#5   -6.74    0.493  0.999     7          0.719     Model B Rev 1.2
	RPi3#6   -5.92    0.483  0.998     6          0.776     Model B+ Rev 1.3
	RPi3#7   -7.96    0.450  0.999     8          0.761     Model B Rev 1.2
	RPi3#8   -8.24    0.455  0.999     8          0.752     Model B Rev 1.2
	RPi3#9   -8.07    0.431  0.999     8          0.758     Model B Rev 1.2
	RPi3#10  -8.18    0.454  0.999     8          0.740     Model B Rev 1.2


The average PPS time and range for the ten Raspberry Pi 3 units is -7.24 microseconds. The two low values are are Model B+ Rev 1.3 units. These probably average 6 microseconds to the nearest second. The Model B Rev 1.2 units alone average -7.67 or 8 microseconds to the nearest second. The average of the *timeline* SD is 0.743.

The results for the Raspberry Pi 4 processors is shown next.

	Raspberry Pi 4 times in microseconds (Linux v5.4.41):
	
	UNIT#   PPS_time  time_SD fit  zeroOffset  Timeline_SD    Generation 
	------------------------------------------------------------------------------------
	RPi4#1   -4.42    0.412  0.999     4          0.506     Model B Rev 1.1
	RPi4#2   -4.19    0.411  0.999     4          0.493     Model B Rev 1.1
	RPi4#3   -3.97    0.388  0.999     4          0.494     Model B Rev 1.1
	RPi4#4   -4.09    0.356  0.999     4          0.499     Model B Rev 1.1
	RPi4#5   -3.97    0.356  0.996     4          0.524     Model B Rev 1.2
	RPi4#6   -4.04    0.380  0.999     4          0.461     Model B Rev 1.2
	RPi4#7   -4.09    0.429  0.999     4          0.521     Model B Rev 1.2
	RPi4#8   -3.87    0.409  0.999     4          0.487     Model B Rev 1.2
	RPi4#9   -3.90    0.416  0.999     4          0.522     Model B Rev 1.2
	RPI4#10  -4.06    0.401  0.999     4          0.526     Model B Rev 1.2

The average PPS time of the ten Raspberry Pi 4 units is -4.06 or -4 microseconds rounded to the nearest microsecond. The average the *timeline* (Type 2b + 2c noise) standard deviation is 0.503.

## Test Notes {#test-notes}

It is worth noting that Raspberry Pi 4 processors provided extremely clean time distributions. For example the distribution for RPi4#1 shown at the head of this section contains all but one of the collected time values. Two or three time values falling outside of the distribution was typical of the RPi4 processors. The RPi3 processors, on the other hand, typically had dozens. This is puzzling because exactly the same programs and test procedures were used for both. Could the reason be architectural differences in the processors?
 
The testing was done by segregating PPS-Client and the pps-timer utility from the other processes running on the RPi processors. These programs were confined to a single core of the processor with the *system taskset utility* and as much as possible the remaining processes running on the processor were pushed onto the three remaining cores. This was not successful for all processes because some are not movable. Nevertheless, this worked well for the Raspberry Pi 4 processors. For example, this is the [PPS jitter](#configuration-file) distribution recorded for RPi4#1

	-7 0
	-6 0
	-5 1
	-4 6
	-3 38
	-2 247
	-1 12316
	0 61154
	1 11908
	2 657
	3 60
	4 10
	5 2
	6 0
	7 1
	8 0
	9 0

All 86,400 time values are displayed in this section of the distribution. You might want to compare this with the distribution provided in the [Feedback Controller](#feedback-controller) section of this document which is typical when no segregation is done. Segregation is not always practical because it effectively reduces the number of working cores by one. But if the sole purpose of the processor is its time measuring capability this is clearly quite effective for RPi4 processors but not, as noted above, for RPi3 processors.



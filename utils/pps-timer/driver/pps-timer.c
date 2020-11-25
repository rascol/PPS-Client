/*
 * pps-timer.c
 *
 * Created on: Jul 30, 2020
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
 *
 * On the Raspberry Pi pps-timer.ko is copied to
 *  /lib/modules/`uname -r`/extra/pps-timer.ko
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/delay.h>	/* udelay */
#include <linux/kdev_t.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/param.h>
#include <asm/gpio.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <../kernel/time/timekeeping.h>
#include <linux/time64.h>
#include <linux/hardirq.h> 	/* for irq_enter and irq_exit */
#include <linux/irqdesc.h> 	/* for generic_handle irq() */
#include <linux/irqflags.h> /* for local_irq_disable() and local_irq_enable() */

/* The text below will appear in output from 'cat /proc/interrupt' */
#define MODULE_NAME "pps-timer"

const char *version = "pps-timer-driver v1.0.0";

static int major = 0;								/* dynamic by default */
module_param(major, int, 0);						/* but can be specified at load time */

struct pt_regs regs;

int *timer_buffer = NULL;

DECLARE_WAIT_QUEUE_HEAD(timer_queue);
int wq_var = 0;

int read1_OK = 0;

MODULE_AUTHOR ("Raymond Connell");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION("2.0.0");

static atomic_t driver_available = ATOMIC_INIT(1);

/**
 * Permits open() only by the first caller until
 * that caller closes the driver.
 */
int ppstimer_open (struct inode *inode, struct file *filp)
{
	/**
	 * The following statement fails if driver_available
	 * is 1 and driver_available then gets set to zero.
	 *
	 * The statement succeeds if driver_available is 0
	 * and driver_available then gets set to -1.
	 */
    if (! atomic_dec_and_test(&driver_available)) {

    	/**
    	 * If driver_available was initially 0 then got
    	 * here and driver_available was set to -1. But
    	 * the next statement sets driver_available back
    	 * to 0. So every subsequent open call comes here.
    	 */

        atomic_inc(&driver_available);
        return -EBUSY; 						/* already open */
    }

    /**
     * If driver_available was initially 1 then got
     * here and driver_available was set to 0
     * by atomic_dec_and_test().
     */
    return 0;
}

/**
 * Closes the driver but keeps it active so the next
 * caller can open it again.
 */
int ppstimer_release(struct inode *inode, struct file *filp)
{
	/**
	 * Sets driver_available to 1 so the next caller
	 * can open the driver again after this close.
	 */
	atomic_inc(&driver_available);
	return 0;
}


/**
 * Generates a stream of calls to ktime_get_real_fast_ns()
 * overlapping the time when the PPS interrupt is expected
 * to occur. If the PPS interrupt occurs during one of the
 * calls, ktime_get_real_fast_ns() will return a time that
 * is significantly later time than the time it returned on
 * the last call. The time of the previous call is taken to
 * be the time that the PPS interrupt triggered the PPS ISR
 * that suspended program operation.
 *
 * @param[in] timeData
 * @param[in] count The length of the timeData array.
 */
int generate_probe(int *timeData, int count)
{
	struct timespec64 ts;
	u64 time_ns1, time_ns2, endTime;
	u64 delta;
	int64_t currentTime, time1;
	int riseTime = timeData[2];
	int i = 0;

	timer_buffer[1] = 0;
	timer_buffer[2] = 0;
	time_ns1 = 0;
	time_ns2 = 0;


	if (riseTime < 0){
		riseTime = 1000000000 + riseTime;
	}

	local_bh_disable();							// Temporarily disable soft irqs and tasklets.
												// These definitely disturb the timing.
	do {										// Spin until time of day approaches riseTime
		ktime_get_real_ts64(&ts);
		time1 = ts.tv_nsec;
	} while (time1 < (riseTime - 1000));

	currentTime = ts.tv_nsec;

	if (currentTime > riseTime){ 				// Must be at least 15 microseconds early
		printk(KERN_INFO "pps-timer: Requested riseTime is not later than currentTime\n");
		printk(KERN_INFO "pps-timer: seq_num: %d  time: %lld:%ld\n", timeData[3], ts.tv_sec, ts.tv_nsec);

		local_bh_enable();
		wait_event_hrtimeout(timer_queue, wq_var == 1, 1000000);

		return 0;
	}

	time_ns2 = ktime_get_real_fast_ns();
	endTime = time_ns2 + 25000;

	do {
		time_ns1 = ktime_get_real_fast_ns();

		delta = time_ns1 - time_ns2;
		if (delta > 3000){

			ts = ns_to_timespec64(time_ns2);

			timer_buffer[1] = (int)ts.tv_sec;
			timer_buffer[2] = (int)ts.tv_nsec;

			printk(KERN_INFO "pps-timer: returning at i: %d  delta: %lld\n", i, delta);

			local_bh_enable();
			wait_event_hrtimeout(timer_queue, wq_var == 1, 1000000);	// Sleep for at least 1 msec only using the hrtimeout.
																		// This is to allow pps-client to process the PPS interrupt
			return count;												// and save params without interference.
		}

		time_ns2 = time_ns1;

		i += 1;

	} while (time_ns2 < endTime && i < 200);


	ts = ns_to_timespec64(time_ns2);
	printk(KERN_INFO "pps-timer: seq_num: %d  time: %lld:%ld\n", timeData[3], ts.tv_sec, ts.tv_nsec);

	local_bh_enable();
	wait_event_hrtimeout(timer_queue, wq_var == 1, 1000000);

	return 0;
}


/**
 * Starts timing the PPS interrupt at the fractional
 * second time requested by buf as a pair of integers.
 *
 * @param[in] buf The buffer containing the time.
 * @param[in] count The size of the buffer which must be
 * 4 * sizeof(int).
 */
ssize_t ppstimer_i_write (struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	ssize_t wr = 0;
	int *timeData;

	timeData = (int *)buf;				 	// timeData[0] : 0 to indicate generate probe
											// timeData[1] : Assert probe time seconds
											// timeData[2] : Assert probe time nanoseconds
											// timeData[3] : sequence count for debugging
	read1_OK = 0;

	if (timeData[0] == 0){
		wr = generate_probe(timeData, count);
	}

	return wr;
}

ssize_t ppstimer_i_read (struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	if (copy_to_user((void *)buf, timer_buffer, count) != 0){
		printk(KERN_INFO "pps-timer: copy_to_user() failed\n");
		return -1;
	}

	return count;
}

struct file_operations pulsegen_i_fops = {
	.owner	 = THIS_MODULE,
	.write   = ppstimer_i_write,
	.read	 = ppstimer_i_read,
	.open	 = ppstimer_open,
	.release = ppstimer_release,
};

void ppstimer_cleanup(void)
{
	flush_scheduled_work();

	unregister_chrdev(major, "pps-timer");

	if (timer_buffer){
		free_page((unsigned long)timer_buffer);
	}

	printk(KERN_INFO "pps-timer: removed\n");
}

int ppstimer_init(void)
{
	int result = 0;

	result = register_chrdev(major, "pps-timer", &pulsegen_i_fops);
	if (result < 0) {
		printk(KERN_INFO "pps-timer: can't get major number\n");
		return result;
	}

	if (major == 0)
		major = result; /* dynamic */

	timer_buffer = (int *)__get_free_pages(GFP_KERNEL,0);

	printk(KERN_INFO "pps-timer: installed\n");

	return 0;
}

module_init(ppstimer_init);
module_exit(ppstimer_cleanup);





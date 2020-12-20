# PPS Timer driver makefile v1.0.0

# Comment/uncomment the following line to disable/enable debugging
#DEBUG = y

export ARCH=arm

# Add your debugging flag (or not) to EXTRA_CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DSHORT_DEBUG # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

EXTRA_CFLAGS += $(DEBFLAGS)
EXTRA_CFLAGS += -I..
EXTRA_CFLAGS += -static

ifneq ($(KERNELRELEASE),)

obj-m := pps-timer.o

else

default:
	$(MAKE) -C /XXXX M=$$PWD modules
	$(MAKE) -C /XXXX M=$$PWD modules_install
all:
	$(MAKE) -C /XXXX M=$$PWD modules
	$(MAKE) -C /XXXX M=$$PWD modules_install
endif


clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions Module.symvers modules.order

depend .depend dep:
	$(CC) $(EXTRA_CFLAGS) -M *.c > .depend


ifeq (.depend,$(wildcard .depend))
include .depend
endif
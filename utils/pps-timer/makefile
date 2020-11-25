# PPS Timer makefile v1.0.0

RM := rm -rf

# All of the sources participating in the build are defined here
-include subdir.mk

# All Target
all: pps-timer

# Tool invocations
pps-timer: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: G++ Linker'
	g++ -static -o "pps-timer" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
install:
	cp pps-timer /XXXX/pps-timer
	cd driver && $(MAKE) all
	
clean:
	-$(RM) $(OBJS) $(CPP_DEPS) $(EXECUTABLES) pps-timer
	-@echo ' '
	cd driver && $(MAKE) clean

.PHONY: all clean dependents
.SECONDARY:

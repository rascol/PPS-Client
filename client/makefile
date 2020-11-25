# PPS-Client makefile v2.0.0

RM := rm -rf

# All of the sources participating in the build are defined here
-include subdir.mk

LIBS := -lrt

# All Target
all: pps-client

# Tool invocations
pps-client: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: G++ Linker'
	g++ -static -pthread -LXXXX -o "pps-client" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) $(OBJS) $(CPP_DEPS) $(EXECUTABLES) pps-client
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:


RM := rm -rf

# All of the sources participating in the build are defined here
-include subdir.mk

# All Target
all: udp-time-client

# Tool invocations
udp-time-client: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: G++ Linker'
	g++ -o "udp-time-client" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
install:
	cp udp-time-client /XXXX/udp-time-client

clean:
	-$(RM) $(OBJS) $(CPP_DEPS) $(EXECUTABLES) udp-time-client
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:


RM := rm -rf

# All of the sources participating in the build are defined here
-include subdir.mk

# All Target
all: pps-client-remove

# Tool invocations
pps-client-remove: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: G++ Linker'
	g++ -o "pps-client-remove" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) $(OBJS) $(CPP_DEPS) $(EXECUTABLES) pps-client-remove
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:

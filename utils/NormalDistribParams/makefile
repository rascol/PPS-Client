
RM := rm -rf

# All of the sources participating in the build are defined here
-include subdir.mk

# All Target
all: normal-params

# Tool invocations
normal-params: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: G++ Linker'
	g++ -static -o "normal-params" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
install:
	cp normal-params /XXXX/normal-params
	
clean:
	-$(RM) $(OBJS) $(CPP_DEPS) $(EXECUTABLES) normal-params
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:

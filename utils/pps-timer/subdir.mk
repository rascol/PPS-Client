
# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
./pps-timer.cpp 

OBJS += \
./pps-timer.o

CPP_DEPS += \
./pps-timer.d

# Each subdirectory must supply rules for building sources it contributes
%.o: ./%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: G++ Compiler'
	g++ -O3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

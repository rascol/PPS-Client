
# Add inputs and outputs from these tool invocations to the build variables 
OBJS += \
./pps-client.o \
./pps-files.o \
./pps-sntp.o \
./pps-serial.o

CPP_DEPS += \
./pps-client.d \
./pps-files.d \
./pps-sntp.d \
./pps-serial.d

# Each subdirectory must supply rules for building sources it contributes
%.o: ./%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: G++ Compiler'
	g++ -Wno-restrict -O3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '



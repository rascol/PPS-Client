
# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
./normal-params.cpp 

OBJS += \
./normal-params.o

CPP_DEPS += \
./normal-params.d

# Each subdirectory must supply rules for building sources it contributes
%.o: ./%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: G++ Compiler'
	g++ -O3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

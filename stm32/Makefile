# InduLogger firmware — STM32F411RE + FreeRTOS (CubeMX-generated sources)
#
#   make           build stm32/build/InduLogger.elf (+ .bin + size report)
#   make flash     flash over ST-LINK via STM32CubeProgrammer CLI (Windows exe,
#                  called from WSL via interop — plug the Nucleo into Windows USB)
#   make clean
#
# Canonical toolchain: arm-none-eabi-gcc (Ubuntu apt) — this same Makefile runs in CI.
# STM32CubeIDE is the .ioc code generator and graphical debugger only; its managed
# build is retired (makefiles generated for the UNC workspace embed \\wsl.localhost
# absolute paths that break make).

TARGET  := InduLogger
BUILD   := build

CC      := arm-none-eabi-gcc
SIZE    := arm-none-eabi-size
OBJCPY  := arm-none-eabi-objcopy

CUBEPROG ?= /mnt/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe

CPU  := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS := -DUSE_HAL_DRIVER -DSTM32F411xE
INC  := -ICore/Inc -IFATFS/Target -IFATFS/App \
        -IDrivers/STM32F4xx_HAL_Driver/Inc -IDrivers/STM32F4xx_HAL_Driver/Inc/Legacy \
        -IMiddlewares/Third_Party/FreeRTOS/Source/include \
        -IMiddlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS \
        -IMiddlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F \
        -IMiddlewares/Third_Party/FatFs/src \
        -IDrivers/CMSIS/Device/ST/STM32F4xx/Include -IDrivers/CMSIS/Include

# CubeMX-generated + vendor code: -Wall
GEN_SRC := $(wildcard Core/Src/*.c) \
           $(wildcard FATFS/App/*.c) $(wildcard FATFS/Target/*.c) \
           $(wildcard Middlewares/Third_Party/FreeRTOS/Source/*.c) \
           $(wildcard Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS/*.c) \
           $(wildcard Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/*.c) \
           $(wildcard Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/*.c) \
           $(wildcard Middlewares/Third_Party/FatFs/src/*.c) \
           $(wildcard Middlewares/Third_Party/FatFs/src/option/*.c) \
           $(wildcard Drivers/STM32F4xx_HAL_Driver/Src/*.c)
# Hand-written code (Task 6+): -Wall -Wextra
OUR_SRC := $(wildcard app/*/*.c platform/*/*.c drivers/*/*.c)

S_SRC   := Core/Startup/startup_stm32f411retx.s
GEN_OBJ := $(GEN_SRC:%.c=$(BUILD)/%.o)
OUR_OBJ := $(OUR_SRC:%.c=$(BUILD)/%.o)
S_OBJ   := $(S_SRC:.s=.o)
OBJ     := $(GEN_OBJ) $(OUR_OBJ) $(BUILD)/$(S_OBJ)
ELF     := $(BUILD)/$(TARGET).elf

CFLAGS := $(CPU) $(DEFS) $(INC) -Wall -g -O0 -ffunction-sections -fdata-sections

.PHONY: all clean flash
all: $(ELF) $(BUILD)/$(TARGET).bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $< $(CFLAGS) $(CFLAGS_EXTRA) -MD -MP -MF $(@:.o=.d) -o $@

# hand-written code gets the stricter warning set
$(BUILD)/app/%.o:     CFLAGS_EXTRA := -Wextra
$(BUILD)/platform/%.o: CFLAGS_EXTRA := -Wextra
$(BUILD)/drivers/%.o:  CFLAGS_EXTRA := -Wextra

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) -x assembler-with-cpp -c $< $(CPU) -o $@

$(ELF): $(OBJ)
	$(CC) $(CPU) --specs=nano.specs -u _printf_float -T STM32F411RETX_FLASH.ld \
	  -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map $^ -o $@
	$(SIZE) $@

$(BUILD)/$(TARGET).bin: $(ELF)
	$(OBJCPY) -O binary $< $@

flash: $(ELF)
	@test -x "$(CUBEPROG)" || { echo "STM32_Programmer_CLI.exe not found — set CUBEPROG=<path>"; exit 1; }
	"$(CUBEPROG)" -c port=SWD -w $< -v -g 0x08000000

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d)

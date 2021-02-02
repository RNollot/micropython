# By default this board is configured to use mboot with packing (signing and encryption)
# enabled.  Mboot must be deployed first.
USE_MBOOT ?= 1

MCU_SERIES = wb
CMSIS_MCU = STM32WB55xx
AF_FILE = boards/stm32wb55_af.csv
STARTUP_FILE = lib/stm32lib/CMSIS/STM32WBxx/Source/Templates/gcc/startup_stm32wb55xx_cm4.o

ifeq ($(USE_MBOOT),1)
# When using Mboot all the text goes together after the bootloader
LD_FILES = boards/stm32wb55xg.ld boards/common_bl.ld
TEXT0_ADDR = 0x08004000
else
# When not using Mboot the text goes at the start of flash
LD_FILES = boards/stm32wb55xg.ld boards/common_basic.ld
TEXT0_ADDR = 0x08000000
endif

# MicroPython settings
MICROPY_PY_BLUETOOTH = 1
MICROPY_BLUETOOTH_NIMBLE = 1
MICROPY_VFS_LFS2 = 1

CFLAGS += -DARM_MATH_CM4 -DCORE_CM4 -D__FPU_PRESENT=1 -DUSE_STM32WBXX_NUCLEO
LIBS +=  -l:libPDMFilter_CM4_GCC_wc32.a -Lboards/$(BOARD)
# Mboot settings
MBOOT_ENABLE_PACKING = 1

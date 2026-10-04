APP_NAME = lbh_client
LDFLAGS := -lpthread

//====================CONFIG SETTING=========================
CONFIG_LBH_CODEC = y
//===========================================================

//==================PLATFORM SELECTION========================

CONFIG_PLATFORM_MIPS = n
CONFIG_PLATFORM_MIPS_UCLIBC = n
CONFIG_PLATFORM_MIPS_XX = n
CONFIG_PLATFORM_X86 = y

ifeq ($(CONFIG_PLATFORM_MIPS_UCLIBC), y)
OTHER_PARAM = -muclibc
CROSS_COMPILE = /home/zhenyadeng/D/AIC-LBH/toolchain/mips/T41/mips-gcc720-glibc229-r5.1.4/bin/mips-linux-gnu-
ARCH = mips
#because not support yet
CONFIG_LBH_CODEC = n
else ifeq ($(CONFIG_PLATFORM_MIPS), y)
CROSS_COMPILE = /home/zhenyadeng/D/AIC-LBH/toolchain/mips/T41/mips-gcc720-glibc229-r5.1.4/bin/mips-linux-gnu-
ARCH = mips
else ifeq ($(CONFIG_PLATFORM_X86), y)
CROSS_COMPILE = 
ARCH = x86_64
else ifeq ($(CONFIG_PLATFORM_MIPS_XX), y)
CROSS_COMPILE = /home/zhenyadeng/D/AIC-LBH/toolchain/mips/xx/mips-linux-gnu-ingenic-gcc7.2.0-glibc2.29-fp64/bin/mips-linux-gnu-
ARCH = mips
CONFIG_BT := y
CONFIG_BLE := y
CONFIG_LBH_CODEC = n
endif

//===========================================================

CONFIG_BT ?= y
CONFIG_BLE ?= y

STATICLAB = lbh

INCS += -I app/include \
	-I common/include \

CFILES += lbh_client.c
ifeq ($(CONFIG_BT), y)
	CFILES += app/src/lbh_bt.c
	CFLAGS += -DLBH_BT_ENABLE
endif
ifeq ($(CONFIG_BLE), y)
	CFILES += app/src/lbh_ble.c
	CFLAGS += -DLBH_BLE_ENABLE
endif

ifeq ($(CONFIG_LBH_CODEC), y)
	CFLAGS += -DCONFIG_LBH_CODEC \
		  -DA2DP_AAC_ENABLE

        CFILES += app/src/lbh_codec.c \
		common/src/lbh_wav.c

	CFILES += $(foreach dir, utils/aaccodec, $(wildcard $(dir)/*.c))
	CFILES += $(foreach dir, utils/sbccodec/src, $(wildcard $(dir)/*.c))
	CFILES += $(foreach dir, utils/sbccodec/sbc, $(wildcard $(dir)/*.c))

	INCS += -I utils/sbccodec/sbc \
	       -I utils/aaccodec \
	       -I utils/sbccodec/src
endif

ifeq ($(ARCH), x86_64)
	CFLAGS += -D__i386__ 
endif



all: $(APP_NAME)

lbh_client:
	$(CROSS_COMPILE)gcc $(OTHER_PARAM) $(INCS) -I ../lbh_lib -o $(APP_NAME) $(CFILES)  $(CFLAGS) -lm  $(LDFLAGS) -L../lbh_lib -l $(STATICLAB)
print:
	@echo $(CFILES)
clean:
	rm -rf *.o $(APP_NAME)

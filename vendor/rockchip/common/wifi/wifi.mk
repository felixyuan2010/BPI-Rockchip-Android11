ifeq ($(strip $(VISION_PRODUCTION_KERNEL_IMAGE)),)
WIFI_KO_FILES := $(shell find $(TOPDIR)kernel/drivers/net/wireless/rockchip_wlan -name "*.ko" -type f)
else
WIFI_KO_FILES := $(sort $(wildcard $(dir $(VISION_PRODUCTION_KERNEL_IMAGE))modules/*.ko))
endif

BOARD_VENDOR_KERNEL_MODULES += \
	$(foreach file, $(WIFI_KO_FILES), $(file))

PRODUCT_COPY_FILES += \
        $(CUR_PATH)/wifi/iwconfig:$(TARGET_COPY_OUT_VENDOR)/bin/iwconfig \
        $(CUR_PATH)/wifi/iwlist:$(TARGET_COPY_OUT_VENDOR)/bin/iwlist

WifiFirmwareFile := $(shell ls $(CUR_PATH)/wifi/firmware)
PRODUCT_COPY_FILES += \
        $(foreach file, $(WifiFirmwareFile), $(CUR_PATH)/wifi/firmware/$(file):$(TARGET_COPY_OUT_VENDOR)/etc/firmware/$(file))

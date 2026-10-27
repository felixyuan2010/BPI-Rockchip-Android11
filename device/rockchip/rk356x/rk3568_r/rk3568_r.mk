#
# Copyright 2014 The Android Open-Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# First lunching is R, api_level is 30
PRODUCT_SHIPPING_API_LEVEL := 30
PRODUCT_DTBO_TEMPLATE := $(LOCAL_PATH)/dt-overlay.in
PRODUCT_SDMMC_DEVICE := fe2b0000.dwmmc

include device/rockchip/common/build/rockchip/DynamicPartitions.mk
include device/rockchip/rk356x/rk3568_r/BoardConfig.mk
include device/rockchip/common/BoardConfig.mk
$(call inherit-product, device/rockchip/rk356x/device.mk)
$(call inherit-product, device/rockchip/common/device.mk)
$(call inherit-product, frameworks/native/build/tablet-10in-xhdpi-2048-dalvik-heap.mk)

DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/../overlay

PRODUCT_CHARACTERISTICS := tablet

# 2026-10-18: platform signing key (our own keypair; SMDT vendor private key
# unobtainable). Framework + all LOCAL_CERTIFICATE := platform apps + the
# vision_app APK share this key, granting signature-level permissions
# (INJECT_EVENTS / WRITE_SECURE_SETTINGS / CHANGE_NETWORK_STATE /
# WRITE_SETTINGS / MANAGE_EXTERNAL_STORAGE / ACCESS_ALL_EXTERNAL_STORAGE).
# Key files are NOT committed to git (private material); backup at
# firmware/platform-keys/ (Mac). NOTE: PRODUCT_* vars must live in the
# product makefile, NOT BoardConfig.mk (readonly after product config).
PRODUCT_DEFAULT_DEV_CERTIFICATE := $(LOCAL_PATH)/security/platform

PRODUCT_NAME := rk3568_r
PRODUCT_DEVICE := rk3568_r
PRODUCT_BRAND := rockchip
PRODUCT_MODEL := rk3568_r
PRODUCT_MANUFACTURER := rockchip
PRODUCT_AAPT_PREF_CONFIG := mdpi
#
## add Rockchip properties
#
PRODUCT_PROPERTY_OVERRIDES += ro.sf.lcd_density=160
PRODUCT_PROPERTY_OVERRIDES += ro.wifi.sleep.power.down=true
PRODUCT_PROPERTY_OVERRIDES += persist.wifi.sleep.delay.ms=0
PRODUCT_PROPERTY_OVERRIDES += persist.bt.power.down=true

# ----------------------------------------------------------------------------
# Build10 (2026-10-22): /data as plaintext ext4, no FBE/FDE.
# Root cause (docs/41): a differently-signed firmware had formatted /data with
# its own keymaster/weaver key; our vold could not unwrap it
# (cryptfs mountFstab /data Failed @8.0s) -> credstore crash loop ->
# system_server never starts -> boot stuck at Android logo. Removing the
# encryption flag from the fstab eliminates that dependency entirely.
# Scoped to rk3568_r only via a dedicated fstab template + fs-type override,
# leaving the shared fstab.in (f2fs+FBE) untouched for sibling targets.
PRODUCT_FSTAB_TEMPLATE := device/rockchip/rk356x/rk3568_r/fstab.rk3568_r.in
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := ext4
TARGET_USERIMAGES_USE_F2FS := false
# ----------------------------------------------------------------------------

# ----------------------------------------------------------------------------
# Build24 (2026-10-24): stop the PMU MCU from cutting power ~620 s after boot.
#
# docs/45: the STM8S00K3 on i2c-5/0x62 powers the SoC off unless it gets
# notify_power_on (ioctl 0xAA) plus a 1 Hz watchdog feed (ioctl 0xA3). The
# factory implements this in drivers/smdt/mcuinf.c; that driver is missing from
# this kernel tree (the DTS node mcuinf@62 exists, so /dev/McuCom never shows
# up and the factory mcu_notify_service cannot run). mcu_smdt reissues the same
# I2C transfers from userspace and is started by mcu_notify.rc.
#
# Keeps the hardware watchdog armed (0x32=1) instead of disabling it, so the
# only recovery layer that does not depend on the SoC still running — the MCU
# cutting power — remains intact.
PRODUCT_PACKAGES += mcu_smdt
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/mcu_notify.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/mcu_notify.rc
# ----------------------------------------------------------------------------

# ----------------------------------------------------------------------------
# 2026-10-27 (docs/53): expose the ethernet framework capability.
#
# The image currently declares neither android.hardware.ethernet nor
# android.hardware.usb.host, so SystemServer.startEthernet() is skipped
# (SystemServer.java:1533 gate), EthernetService never starts, EthernetManager
# comes back null and Settings' Ethernet page dies with "feature not
# available"; eth0 is also never brought up by the framework.
#
# ethernet-service itself is ALREADY in PRODUCT_PACKAGES and
# PRODUCT_SYSTEM_SERVER_JARS via the inherited product config (verified with
# get_build_var PRODUCT_SYSTEM_SERVER_JARS / PRODUCT_PACKAGES), so the missing
# piece is only this permission declaration. Declaring usb.host is NOT needed
# for this and is deliberately left alone (it would also change USB behaviour).
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.ethernet.xml:$(TARGET_COPY_OUT_SYSTEM)/etc/permissions/android.hardware.ethernet.xml
# ----------------------------------------------------------------------------

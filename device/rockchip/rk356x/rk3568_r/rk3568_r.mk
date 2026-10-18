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
PRODUCT_PROPERTY_OVERRIDES += ro.sf.lcd_density=320
PRODUCT_PROPERTY_OVERRIDES += ro.wifi.sleep.power.down=true
PRODUCT_PROPERTY_OVERRIDES += persist.wifi.sleep.delay.ms=0
PRODUCT_PROPERTY_OVERRIDES += persist.bt.power.down=true

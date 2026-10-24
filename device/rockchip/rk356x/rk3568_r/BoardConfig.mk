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
include device/rockchip/rk356x/BoardConfig.mk
BUILD_WITH_GO_OPT := false

# AB image definition
BOARD_USES_AB_IMAGE := false
BOARD_ROCKCHIP_VIRTUAL_AB_ENABLE := false

ifeq ($(strip $(BOARD_USES_AB_IMAGE)), true)
    include device/rockchip/common/BoardConfig_AB.mk
    TARGET_RECOVERY_FSTAB := device/rockchip/rk356x/rk3568_r/recovery.fstab_AB
endif

PRODUCT_UBOOT_CONFIG := rk3568
# 2026-10-18: product DTS (SMDT live-tree staticized: DSI0 1920x1200@VP0 +
# eDP 1200x1920@VP1 + smdt_param + 8 uboot live patches). Evb1 baseline kept
# as comment for rollback. See docs/34-preflash-dts-adaptation-checklist.md.
PRODUCT_KERNEL_DTS := rk3568-iot3568a
BOARD_GSENSOR_MXC6655XA_SUPPORT := true
BOARD_CAMERA_SUPPORT_EXT := true
BOARD_HS_ETHERNET := true

# 2026-10-18: disable PCBA factory-test toolchain (unstripped wifi .ko ~146MB
# uncompressed blew recovery ramdisk to 66MB -> recovery.img 97.5MB > 96MB
# partition limit). Reference: bananapi_r2pro / rk356x_box both set false.
TARGET_ROCKCHIP_PCBATEST := false

# 2026-10-24: rotate primary display to 270deg (user request: "主屏显示先尝试
# 旋转270度"). The touch panel is physically mounted 90deg off vs the panel; the
# GT9XX driver rotation=<1> in the gt1x DTS node handles the touch-axis swap,
# while SurfaceFlinger also rotates touch input by this amount.
SF_PRIMARY_DISPLAY_ORIENTATION := 270

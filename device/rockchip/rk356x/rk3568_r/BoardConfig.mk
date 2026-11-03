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

# 2026-11-03: 主屏从 eDP 换成 DSI 后，这里必须是 0。
#
# 硬件事实（实测，见 docs/67）：
#   DSI-1  1920x1200 原生横屏 —— 内置面板，唯一带触摸的屏（goodix-ts @i2c fe5a0000）
#   eDP-1  1200x1920 原生竖屏 —— 被检者副屏，**物理横放**，因此需要 270° 补偿
#
# 历史：2026-10-24 曾把这里设成 270，是因为当时 HWC 把 eDP 选成了 primary
# （vendor.hwc.device.primary 未设置，HWC 按 connector 枚举顺序取第一个 internal），
# 而 eDP 原生竖屏 + 物理横放，必须靠这个全局 270° 才看着正常。
# 副作用：带触摸的 DSI 变成了 external → 操作员屏没有触摸，触摸事件还被按
# displayId=0 回落到 eDP 上（"点 A 屏、B 屏响应"）。
#
# 2026-11-03 改法：用 vendor.hwc.device.primary=DSI 把 primary 换回触摸屏
# （见 rk3568_r.mk），此处随之归零。eDP 副屏的 90° 由 App 层
# PatientScreenPresentation.RotatedStage 承担 —— 本树 kernel/HWC 未实现 DRM
# 标准 panel orientation，外接屏没有按屏旋转能力。
SF_PRIMARY_DISPLAY_ORIENTATION := 0

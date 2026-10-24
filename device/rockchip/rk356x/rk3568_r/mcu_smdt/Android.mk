# 2026-10-24 (Build24): SMDT PMU MCU watchdog keeper — see docs/45.
#
# The MCU (STM8S00K3 @ i2c-5/0x62) cuts SoC power ~620 s after boot unless it
# receives "boot completed" (ioctl 0xAA -> write_cmd(0x51,0x33)) *and* a 1 Hz
# feed (ioctl 0xA3 -> write_cmd(0x33,0xAB)). The factory does this in
# drivers/smdt/mcuinf.c, which is absent from this kernel tree, so /dev/McuCom
# never appears and the factory mcu_notify_service cannot run. This binary
# issues the identical I2C frames straight from userspace via /dev/i2c-5.
#
# Built from source (not a checked-in prebuilt) so it tracks the tree toolchain.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := mcu_smdt
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := mcu_smdt.c
# usleep() is deprecated in bionic but is exactly what the kernel-side
# mcu_run_thread does (msleep); keep the call sites identical to the factory.
LOCAL_CFLAGS := -Wall -Wno-unused-parameter -Wno-deprecated-declarations
LOCAL_VENDOR_MODULE := true
include $(BUILD_EXECUTABLE)

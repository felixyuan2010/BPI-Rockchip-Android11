LOCAL_PATH := $(call my-dir)

# vision_app（Tier3 功能应用）的固件预装资产，目标路径 device/rockchip/rk356x/visionapp/。
# 二级目录里的 prebuilt/Android.mk 需要这一层转发（与 deviceagent/firmware/Android.mk 同理：
# all-makefiles-under 只展开一层，prebuilt/ 在两层下扫不到，否则报 unknown target VisionApp）。
include $(call all-makefiles-under,$(LOCAL_PATH))

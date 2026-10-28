# 转发层：device/rockchip/rk356x/Android.mk 的 all-makefiles-under 只展开一层，
# 而 prebuilt 模块位于 deviceagent/prebuilt/Android.mk（两层），故在此再展开一次。
LOCAL_PATH := $(call my-dir)

include $(call all-makefiles-under,$(LOCAL_PATH))

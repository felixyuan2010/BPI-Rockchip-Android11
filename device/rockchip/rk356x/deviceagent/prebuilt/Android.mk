# Prebuilt 模块：把 DeviceAgent.apk 预装为特权系统应用并平台签名。
# 源 APK 由 vision_app 仓库 `./gradlew :deviceagent:assembleRelease` 产出，
# 再由固件构建管线复制到本目录并命名为 DeviceAgent.apk（不进 git，见 README）。

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := DeviceAgent
LOCAL_MODULE_CLASS := APPS
LOCAL_MODULE_SUFFIX := $(COMMON_ANDROID_PACKAGE_SUFFIX)
LOCAL_SRC_FILES := DeviceAgent.apk

# 平台签名：DeviceAgent 需要 WRITE_SECURE_SETTINGS / 修改系统设置等特权。
LOCAL_CERTIFICATE := platform
# 放进 /system/priv-app（而非 /system/app），privileged 权限才生效。
LOCAL_PRIVILEGED_MODULE := true
LOCAL_MODULE_TAGS := optional
# 不作为系统替身覆盖任何既有包。
LOCAL_OVERRIDES_PACKAGES :=

include $(BUILD_PREBUILT)

LOCAL_PATH := $(call my-dir)

# vision_app 预装模块（docs/52 §八「生产化缺口」）。
# APK 由 ./gradlew :automotive:assembleRelease 产出，已用 VISUAL_RELEASE_* 平台签名。
# 这里用 PRESIGNED 保留原签名：它与 PRODUCT_DEFAULT_DEV_CERTIFICATE 是同一把 key
# （SHA-256 F7:4E:D8:9C…），因此 WRITE_SECURE_SETTINGS / REBOOT 等 signature 级权限仍成立；
# 同时避免对 200+MB 的 APK 重签。
include $(CLEAR_VARS)
LOCAL_MODULE := VisionApp
LOCAL_MODULE_OWNER := jsy
LOCAL_SRC_FILES := VisionApp.apk
LOCAL_MODULE_CLASS := APPS
LOCAL_MODULE_SUFFIX := $(COMMON_ANDROID_PACKAGE_SUFFIX)
LOCAL_MODULE_TAGS := optional
LOCAL_PRIVILEGED_MODULE := true
LOCAL_CERTIFICATE := PRESIGNED
include $(BUILD_PREBUILT)

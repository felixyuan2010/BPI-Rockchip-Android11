# ============================================================
# DeviceAgent 集成（Tier2 配置层 / kiosk 锁屏）
# 详见 vision_app/docs/52-device-integration-architecture.md
#
# 接线步骤：
#   1. 把本 firmware/ 目录整体拷贝到 rk3568-src 设备配置下，
#      例如 device/rockchip/rk3568/jsy/deviceagent/。
#   2. 在对应设备的 device.mk 末尾 `include` 本文件：
#         $(call inherit-product-if-exists, device/rockchip/rk3568/jsy/deviceagent/device.mk)
#   3. 构建管线先产出 DeviceAgent.apk 并放到 prebuilt/DeviceAgent.apk。
#   4. `chmod 0755` setup_deviceagent_do.sh（PRODUCT_COPY_FILES 保留源权限）。
# ============================================================

# 注意：product 配置里不能用 $(call my-dir)——它依赖 LOCAL_PATH，在 product mk
# 上下文为空，会把 overlay 路径拼成 "/"（Soong overlay singleton 直接 panic）。
DEVICEAGENT_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

# 1) 预装 APK（priv-app + platform 签名）。
#    注意：**不能**在这里 include prebuilt/Android.mk —— product 配置阶段展开
#    BUILD_PREBUILT 会让 partition_tag/install path 错乱（实测报
#    "unhandled install path TARGET_OUT_DATA_APPS_PRIVILEGED"）。
#    该 Android.mk 由 build/make/core/main.mk 的 Android.mk 扫描自动发现。
#    仅当 APK 存在时才加入 PRODUCT_PACKAGES，避免 CI 在未放置 APK 时报错。
ifneq ($(wildcard $(DEVICEAGENT_DIR)/prebuilt/DeviceAgent.apk),)
    PRODUCT_PACKAGES += DeviceAgent
endif

# 2) 全局隐藏导航栏 overlay（kiosk 锁屏系统级路径，比 immersive-sticky 彻底）。
#    风险见 docs/52 M5/N5：lockTask 失效 + 导航隐藏 = 设备无退回，须保留
#    adb/串口恢复 + 隐藏工程菜单。
#
#    分阶段（IA2 验收顺序）：首次烧录只验 DO 授权，此时**不要**隐藏导航栏——
#    否则 DO 失败时设备既无导航也无返回路径。DO 确认生效后再用
#      make DEVICEAGENT_HIDE_NAV_BAR=true
#    出第二阶段包。默认 false。
DEVICEAGENT_HIDE_NAV_BAR ?= false
ifeq ($(DEVICEAGENT_HIDE_NAV_BAR),true)
DEVICE_PACKAGE_OVERLAYS += $(DEVICEAGENT_DIR)/overlay
endif

# 3) 首启 DO 授权脚本 + init 服务（在 SetupWizard 之前完成 DO 授权）。
PRODUCT_COPY_FILES += \
    $(DEVICEAGENT_DIR)/setup_deviceagent_do.sh:system/bin/setup_deviceagent_do.sh \
    $(DEVICEAGENT_DIR)/deviceagent_do.rc:system/etc/init/deviceagent_do.rc

# 4) 禁用 SetupWizard / kiosk 运维通道（ro.adb.secure、persist.adb.tcp.port）。
#    ⚠️ 这些属性**不能**放 PRODUCT_PROPERTY_OVERRIDES：本树开了
#    BOARD_PROPERTY_OVERRIDES_SPLIT_ENABLED，它们会被改投 /vendor/build.prop，
#    而 vendor_init 读不到 default_prop → 属性写了但 getprop 为空（静默失效）。
#    统一写进设备树的 system.prop（device/rockchip/rk356x/rk3568_r/system.prop），
#    由 build/make/core/Makefile 自动 append 到 /system/build.prop。
#    当前值（见该文件）：
#        ro.setupwizard.mode=DISABLED
#        ro.adb.secure=0           # 免「允许 USB 调试」授权框
#        persist.adb.tcp.port=5555 # adbd 开机监听 TCP，adb wifi 可用

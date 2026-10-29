# vision_app（Tier3 功能应用）预装到 /system/priv-app（docs/52 §八）。
# 由 rk3568_r.mk 用 inherit-product 引入；APK 与模块定义见 prebuilt/。
PRODUCT_PACKAGES += VisionApp

# 品牌开机动画（kiosk 单品牌启动链，docs/52）。
# 本产品 TARGET_BOARD_PLATFORM_PRODUCT=tablet，device/rockchip/common/device.mk 里
# bootanimation.zip 的拷贝规则只在 atv 分支，因此设备上原本没有 /system/media/bootanimation.zip，
# BootAnimation 退回到内置的 Android 四彩点动画。这里显式安装自己的 zip：
# 黑底 karisyn（与内核 logo 同尺寸同位置）→ 底部渐亮到 #0F4982 → 满屏定格，
# 末帧与 App 启动窗口（Theme.Visual 的 windowBackground）逐像素一致。
# zip 由 automotive/src/main/res/drawable/app_splash_logo.xml 栅格化后合成，见 docs/52。
PRODUCT_COPY_FILES += \
    device/rockchip/rk356x/visionapp/prebuilt/bootanimation.zip:$(TARGET_COPY_OUT_SYSTEM)/media/bootanimation.zip

#!/system/bin/sh
#
# 首启一次性 Device Owner 授权。
# 意图：把 com.jsy.deviceagent 设为设备所有者（DO），从而能用 lockTask
#       真正锁死 kiosk，而不是仅靠 immersive-sticky（可滑出）。
#
# 关键前提（详见 docs/52 §五/§八）：
#   1. 设备必须无账户、无次要用户。root/adb 调用走 framework 的 isAdb 分支，
#      「已过完设置向导」并不阻塞；但 accounts 非空且 admin 非 test-only 会直接拒绝。
#      （固件侧仍 DISABLE SetupWizard，避免开机向导抢在 DO 之前占用界面。）
#   2. DeviceAgent 必须已预装为 /system/priv-app 且平台签名（见 device.mk）。
#   3. 本脚本由 init 服务 deviceagent_do 在 sys.boot_completed=1 时触发。
#      防重入以「DO 是否真的存在」为准，标记文件只作辅助：保数据刷机（--keep-data）
#      会保留 /data，标记还在但 DO 可能已被清掉，只认标记会导致永远不再尝试。
#
# 注意：dpm set-device-owner 在 Android 高版本仍可用，但官方推荐
#       organization-owned 设备的 mark-device-owner-on-organization-owned-device。
#       对"我们一次性烧录、已禁向导"的场景，set-device-owner 最省事。

FLAG=/data/local/deviceagent_do_done
PKG=com.jsy.deviceagent
ADMIN=${PKG}/.admin.AdminReceiver
# DO 的真实状态由 framework 持久化在这里（device_owner_2.xml 是现版本写入的文件，
# device_owner.xml 是旧版本遗留）；读文件比解析 dumpsys 文本稳，也不依赖 shell 命令。
OWNER_XML="/data/system/device_owner_2.xml /data/system/device_owner.xml"

# 已经是本包做 DO：补写标记后退出（标记缺失多半是 /data 被保数据刷机从旧盘带来）。
for f in $OWNER_XML; do
    if grep -q "<device-owner [^>]*package=\"$PKG\"" "$f" 2>/dev/null; then
        [ -f "$FLAG" ] || touch "$FLAG"
        echo "deviceagent_do: already device owner, skip" > /dev/kmsg
        exit 0
    fi
done

if [ -f "$FLAG" ]; then
    exit 0
fi

# DO 已被别的包占用：再试也不会成功，记日志后放弃（不写标记，避免掩盖问题）。
for f in $OWNER_XML; do
    if grep -q "<device-owner " "$f" 2>/dev/null; then
        echo "deviceagent_do: device owner taken by another package" > /dev/kmsg
        exit 1
    fi
done

# 等 PackageManager 把预装包扫描完（priv-app 通常在 boot 早期就就绪，
# 但保守等待一下，避免 pm path 还没准备好）。
for i in $(seq 1 10); do
    pm path "$PKG" >/dev/null 2>&1 && break
    sleep 1
done

pm path "$PKG" >/dev/null 2>&1 || {
    # 包没装进来，记录并退出，等下次 boot 重试（不写 flag）。
    echo "deviceagent_do: $PKG not installed, retry next boot" > /dev/kmsg
    exit 1
}

dpm set-device-owner "$ADMIN"
rc=$?

if [ "$rc" -eq 0 ]; then
    touch "$FLAG"
    echo "deviceagent_do: DO set OK" > /dev/kmsg
else
    # 失败常见原因：设备有账户 / 存在次要用户 / DO 已被别的包占用。
    # 不写标记，下次 boot 再试；同时写日志便于现场排查。
    echo "deviceagent_do: set-device-owner failed rc=$rc" > /dev/kmsg
fi

exit $rc

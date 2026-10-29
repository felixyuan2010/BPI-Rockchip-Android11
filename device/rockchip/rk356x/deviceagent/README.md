# DeviceAgent 固件集成指南（IA2 锁屏加固）

本目录是 DeviceAgent 在 **rk3568-src 固件构建树** 中的权威源。vision_app 仓库只产出 APK 与这份配置，真正烧进 `update.img` 的动作在固件树完成。

> **main 合并说明（2026-10-28）**：本目录自 `feature/deviceagent` 分支合并，仅作阶段三（预装 / DO / lockTask）的**参考资产：未接入构建流程、未验证、默认不启用**。overlay 隐藏导航栏与 `ro.setupwizard.mode=DISABLED` 会改变整机行为，启用前必须先完成恢复通道与真机验收。
>
> **2026-10-28 更新**：`.admin.AdminReceiver` 已在 main 实现（`deviceagent/src/main/java/com/jsy/deviceagent/admin/AdminReceiver.kt` + `@xml/device_admin`），`dpm set-device-owner` 的目标组件已就绪；`prebuilt/DeviceAgent.apk` 已按下方步骤 3 生成并置于本目录（不进 git）。真机验证：`dpm set-active-admin` 成功、`dumpsys device_policy` 显示 `policies: force-lock`，证明元数据可被系统正确解析（验证后已清理，设备恢复无 admin 状态）。仍未验证的是**固件树接线与 DO 授权本身**——当前测试机 `Device provisioned: true`，须走新镜像（禁用向导）路径。
>
> 权限说明：本目录按「预装 + 平台签名」设计。实测平台签名的 `/data/app` 应用同样可以获得 `signature|privileged` 权限；priv-app 的主要价值是预装可靠性、不可卸载与首次开机默认授权，而不是这批权限的必要条件。

## 目录结构

```
firmware/
├── device.mk                       # 设备配置片段（include 到设备 device.mk）
├── prebuilt/
│   └── Android.mk                  # prebuilt 模块：priv-app + platform 签名
│   └── DeviceAgent.apk             # 构建管线放置（不进 git）
├── setup_deviceagent_do.sh         # 首启一次性 DO 授权脚本
├── deviceagent_do.rc               # init 服务（boot_completed 后触发）
├── overlay/frameworks/base/core/res/res/values/config.xml  # 隐藏导航栏
└── README.md
```

## 接线步骤（在 rk3568-src 固件树内）

1. 把 `firmware/` 整体拷到设备配置目录，例如 `device/rockchip/rk3568/jsy/deviceagent/`。
2. 在该设备的 `device.mk` 末尾加入：
   ```makefile
   $(call inherit-product-if-exists, device/rockchip/rk3568/jsy/deviceagent/device.mk)
   ```
3. 构建 DeviceAgent APK，放到 `prebuilt/DeviceAgent.apk`：
   ```bash
   cd <vision_app>
   ./gradlew :deviceagent:assembleRelease
   cp deviceagent/build/outputs/apk/release/deviceagent-release.apk \
      <rk3568-src>/device/rockchip/rk3568/jsy/deviceagent/prebuilt/DeviceAgent.apk
   ```
4. `chmod 0755 setup_deviceagent_do.sh`（PRODUCT_COPY_FILES 保留源权限，脚本须可执行）。
5. 正常 `build.sh`，出包。

## Device Owner 授权的两条路径

| 构建类型 | 做法 | 说明 |
|---|---|---|
| **userdebug** | `ro.setupwizard.mode=DISABLED` + 首启 `dpm set-device-owner`（本目录脚本） | 最简单，我们烧录机用这个 |
| **user** | QR/NFC provisioning，或出厂前脚本一次性授权 | 禁用向导在 user 版不被允许，`dpm set-device-owner` 可能受限；需走 managed provisioning |

> 关键约束（docs/52 §五/§八）：**一台设备只能有一个 DO**。上机前先 `adb shell dumpsys device_policy` 确认 `Device Owner:` 段下**没有** `admin=ComponentInfo{com.jsy.visual/...}`，否则 vision_app 已是 DO、DeviceAgent 永远当不了 DO，需先解绑。
> 注意：本机 Android 版本**已移除** `dpm list-owners`（报 unknown command），改用 `dumpsys device_policy`。若输出中 `Device provisioned: true`（SetupWizard 已完成），则当前这台已配置机器上 `dpm set-device-owner` 会直接失败，须先恢复出厂。

## 接线注意事项（2026-10-28 实测踩坑，已在资产内修正）

1. **product mk 里不能用 `$(call my-dir)`**：它依赖 `LOCAL_PATH`，在 product 配置上下文为空，会把 overlay 路径拼成 `/`，Soong `overlay` singleton 直接 panic（`unexpected relative path outside directory ... filepath.Rel(..., /)`）。改用 `$(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))`。
2. **product mk 里不能 `include prebuilt/Android.mk`**：在 product 配置阶段展开 `BUILD_PREBUILT` 会让 partition 判定错乱，实测报 `unhandled install path "TARGET_OUT_DATA_APPS_PRIVILEGED"`。正确做法是只在 device.mk 里 `PRODUCT_PACKAGES += DeviceAgent`，由主 Makefile 扫描 Android.mk 定义模块。
3. **`all-makefiles-under` 只展开一层**：`device/rockchip/rk356x/Android.mk` 找不到 `deviceagent/prebuilt/Android.mk`（两层深），需在 `deviceagent/Android.mk` 加一层转发（已提供）。
4. **签名验证**：`make DeviceAgent` 后用 `openssl pkcs7` 提取 APK 证书指纹，应与 `build/make/target/product/security/platform.x509.pem` 的 SHA-256 一致（本树实测 `2D:37:0C:21:F5:DF:D5:53:D2:A7:96:31:4B:70:92:5F:B3:8A:DE:EF:90:86:4C:92:0B:BB:BB:12:88:7D:35:22`），并确认安装到 `system/priv-app/DeviceAgent/`。

## 锁屏生效链

1. 系统启动 → `deviceagent_do.rc` 在 `sys.boot_completed=1` 触发 `setup_deviceagent_do.sh`。
2. 脚本调用 `dpm set-device-owner com.jsy.deviceagent/.admin.AdminReceiver`。重入判据以**实际 DO 状态**为准（`/data/system/device_owner_2.xml`），标记文件仅作辅助——保数据刷机会保留 `/data`，只认标记会导致 DO 被清掉后永不再授权。
3. `AdminReceiver` 成为 DO 后，在 `onEnabled` / `onProfileProvisioningComplete` 回调里 `setLockTaskPackages(["com.jsy.visual"])`（非 DO 时跳过，避免 SecurityException）。
4. vision_app 在前台 `Activity.startLockTask()` 进入真正锁屏（滑不出、无导航）。
5. 导航栏由 `overlay/config_showNavigationBar=false` 全局隐藏，作为双保险。
   **实测更正（2026-10-29，build45 真机）**：这版 RK 固件不遵循该开关（`framework-res` 里已是 `false`，导航栏仍显示）；`policy_control=immersive.full=*` 同样无效（new insets mode FULL，`InsetsPolicy` 不读 `PolicyControl`）。实际隐藏由 vision_app 在 `onActivityResumed` 调 `WindowInsetsController.hide(systemBars())` 完成，并与 `startLockTask()` 配合。

## 风险与回退（docs/52 M5/N5）

- 若 lockTask 未生效**且**导航栏被隐藏，设备将无退回手段。回退：
  - 调试期保留 adb（`192.168.1.247:5555`）/ 串口。
  - 生产机提供隐藏工程菜单（长按音量键 + 电源，进入后可选"解除锁屏/重启"）。
- DO 授权失败会在 `/dev/kmsg` 留 `deviceagent_do: ...` 日志，现场 `dmesg | grep deviceagent_do` 排查。

## 不在此次范围（IA3+）

- 背光 sysfs 写入（需实机确认主/副屏背光节点，生产机 priv-app 直写需 init 改权限或走 HAL）。
- 双屏同显/异显预设（构建期定两种模式，按属性切换重启生效）。
- 系统自检（串口复用 `mcu_smdt`、测试音、外设、镜轮标定）。
- 服务器同步与 Agent 自更新（Agent 必须保持最小，自身只能走整包 OTA）。

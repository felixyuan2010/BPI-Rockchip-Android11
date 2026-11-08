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
│   └── Android.mk                  # prebuilt 模块：保留平台签名的 priv-app
│   └── DeviceAgent.apk             # **本仓库为唯一源**（不进 git），出包时同步到固件树
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
3. 构建 DeviceAgent APK，并**放回本仓库的 `prebuilt/DeviceAgent.apk`**（它是固件的唯一 APK 源）：
   ```bash
   cd <vision_app>
   ./gradlew :deviceagent:assembleRelease
   cp deviceagent/build/outputs/apk/release/deviceagent-release.apk \
      deviceagent/firmware/prebuilt/DeviceAgent.apk        # ← 目标是**本仓库**，不是 rk3568-src
   ```
   `firmware/make-firmware.sh` 出包时会把它同步进 `device/rockchip/rk356x/deviceagent/prebuilt/`，
   **并逐文件比对 sha256，不一致直接中止出包**。

   > 2026-10-29 修正：此前文档写的是直接 cp 到 `<rk3568-src>/...`，而管线的实际行为是
   > 「gradle 产物 → 容器」的旁路覆盖。两边叠加的结果是**仓库副本从来不影响固件内容**，
   > 于是它静默旧了一整天（仓库 2 096 765 B / 容器 2 100 369 B）没人发现。
   > 现在改为「仓库 → 容器」单向同步 + 双向 sha256 校验；gradle 产物与仓库不一致时
   > 出包会硬失败，并打印该执行的 `cp` 命令。
4. 确认 BSP 已配置 `jsy_deviceagent_provision` init 域与脚本文件标签，脚本保持 0755 权限。
5. 按项目固件流水线构建。预装采用 `PRESIGNED`，宿主须验证输入 APK 的产品平台证书。

## Device Owner 首启与修复

`user` 和 `userdebug` 共用专用 init 域。脚本以 UID 0 调用 DPM，Android 11 在用户已设置的情况下仍会检查账号和额外用户。既有其他所有者或未知的设备策略输出会使流程停止。保数据刷机后按实时所有者状态检查，旧标记不参与判断。

## 接线注意事项（2026-10-28 实测踩坑，已在资产内修正）

1. **product mk 里不能用 `$(call my-dir)`**：它依赖 `LOCAL_PATH`，在 product 配置上下文为空，会把 overlay 路径拼成 `/`，Soong `overlay` singleton 直接 panic（`unexpected relative path outside directory ... filepath.Rel(..., /)`）。改用 `$(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))`。
2. **product mk 里不能 `include prebuilt/Android.mk`**：在 product 配置阶段展开 `BUILD_PREBUILT` 会让 partition 判定错乱，实测报 `unhandled install path "TARGET_OUT_DATA_APPS_PRIVILEGED"`。正确做法是只在 device.mk 里 `PRODUCT_PACKAGES += DeviceAgent`，由主 Makefile 扫描 Android.mk 定义模块。
3. **`all-makefiles-under` 只展开一层**：`device/rockchip/rk356x/Android.mk` 找不到 `deviceagent/prebuilt/Android.mk`（两层深），需在 `deviceagent/Android.mk` 加一层转发（已提供）。
4. **签名验证**：用 `apksigner verify --print-certs` 检查输入 APK 的 SHA-256 证书指纹，与产品 BSP 实际配置的签名证书对比。确认最终 APK 安装到 `system/priv-app/DeviceAgent/`，并与输入 APK 字节一致。

## 锁屏生效链

1. 系统启动 → `deviceagent_do.rc` 在 `sys.boot_completed=1` 触发 `setup_deviceagent_do.sh`。
2. 脚本读取 `dumpsys device_policy`，当前没有 DO 时调用 `dpm set-device-owner com.jsy.deviceagent/.admin.AdminReceiver`。
3. Agent 在开机、授权及所有者变更时对账。确认为 DO 后下发 lockTask 白名单及 HOME 策略。
4. vision_app 在前台 `Activity.startLockTask()` 进入真正锁屏（滑不出、无导航）。
5. 导航栏由 `overlay/config_showNavigationBar=false` 全局隐藏，作为双保险。
   **实测更正（2026-10-29，build45 真机）**：这版 RK 固件不遵循该开关（`framework-res` 里已是 `false`，导航栏仍显示）；`policy_control=immersive.full=*` 同样无效（new insets mode FULL，`InsetsPolicy` 不读 `PolicyControl`）。实际隐藏由 vision_app 在 `onActivityResumed` 调 `WindowInsetsController.hide(systemBars())` 完成，并与 `startLockTask()` 配合。

## 风险与回退（docs/52 M5/N5）

- 若 lockTask 未生效**且**导航栏被隐藏，设备将无退回手段。回退：
  - 调试期保留 adb（`192.168.1.247:5555`）/ 串口。
  - 生产机提供隐藏工程菜单（长按音量键 + 电源，进入后可选"解除锁屏/重启"）。
- DO 授权日志可用 `logcat -s deviceagent_do` 查看。

## 不在此次范围（IA3+）

- 背光 sysfs 写入（需实机确认主/副屏背光节点，生产机 priv-app 直写需 init 改权限或走 HAL）。
- 双屏同显/异显预设（构建期定两种模式，按属性切换重启生效）。
- 系统自检（串口复用 `mcu_smdt`、测试音、外设、镜轮标定）。
- 服务器同步与 Agent 自更新（Agent 必须保持最小，自身只能走整包 OTA）。

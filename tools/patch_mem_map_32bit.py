#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build11 修复：把 USE_ART_LOW_4G_ALLOCATOR 从「所有非 aarch64/apple-64 目标=1」
  收窄为「仅 64 位 x86_64 构建宿主机=1」，使 32 位 arm 设备构建回到上游默认 0。

背景（docs/41 §12）：2026-10-18 为修宿主机 Rosetta 下 dex2oatd64 的 MAP_32BIT
失效，把 mem_map.h 的 #else 分支由 0 改成 1，但该分支同时覆盖 32 位 arm 设备
构建 -> app_process32 的 LOS 分配改走线性扫描低 4GB 分配器 -> rk3568 上失败 ->
zygote_secondary SIGABRT -> onrestart 连坐 -> 卡 logo。

用法（容器内）：python3 patch_mem_map_32bit.py [--check]
"""
import io
import os
import shutil
import sys

SRC = "/rk3568-src/bananapi_r2pro_android11_20230221/art/libartbase/base/mem_map.h"

OLD = """// Host-only change: device builds (aarch64/arm) are unaffected.
#define USE_ART_LOW_4G_ALLOCATOR 1
#endif"""

NEW = """// !! 2026-10-22 修正 (Build11) !! 本 #else 分支同时覆盖 32 位 arm 设备构建
// (app_process32 非 __LP64__)，原「device builds are unaffected」判断是错的。
// 整块置 1 会让 32 位 LOS(LargeObjectSpace) 的 mmap 改走线性扫描低 4GB 分配器，
// 在 rk3568 上稳定失败:
//   Check failed: mem_map.IsValid() Failed to allocate large object space mem map:
//   Failed anonymous mmap(0x0, 536870912, 0x3, 0x22, -1, 0): Out of memory.
// -> zygote_secondary SIGABRT -> init.zygote64_32.rc "onrestart restart zygote"
// 连坐 -> framework 每 ~5s 重启 -> 卡 logo（Build10 实测，tombstone_36 为证）。
// 规则: 仅 64 位 x86_64 构建宿主机（Rosetta 修复所需）置 1；其余目标
// （32 位 arm 设备 / 32 位 x86 / Fuchsia）一律回上游默认 0 = 普通 mmap。
#if defined(__x86_64__) && defined(__LP64__) && !defined(__Fuchsia__)
#define USE_ART_LOW_4G_ALLOCATOR 1
#else
#define USE_ART_LOW_4G_ALLOCATOR 0
#endif
#endif"""

MARKER = "// !! 2026-10-22 修正 (Build11) !!"


def main() -> int:
    check_only = "--check" in sys.argv
    if not os.path.exists(SRC):
        print("FAIL: not found: %s" % SRC)
        return 1
    src = io.open(SRC, encoding="utf-8").read()

    if MARKER in src:
        print("ALREADY_PATCHED (marker present)")
        return 0
    if src.count(OLD) != 1:
        print("FAIL: anchor count=%d (expect 1)" % src.count(OLD))
        return 1
    if check_only:
        print("CHECK_OK: anchor found, not patched yet")
        return 0

    bak = SRC + ".bak-pre32fix"
    if not os.path.exists(bak):
        shutil.copy2(SRC, bak)
        print("backup -> %s" % bak)

    io.open(SRC, "w", encoding="utf-8").write(src.replace(OLD, NEW, 1))
    print("PATCH_OK -> %s" % SRC)

    # 打印修改后的分支块以便核对
    out = io.open(SRC, encoding="utf-8").read().splitlines()
    for i, line in enumerate(out[:70], 1):
        if "USE_ART_LOW_4G_ALLOCATOR" in line or "x86_64__" in line:
            print("%3d: %s" % (i, line))
    return 0


if __name__ == "__main__":
    sys.exit(main())

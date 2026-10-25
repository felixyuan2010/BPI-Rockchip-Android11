/*
 * mcu_smdt.c —— 按 SMDT 工厂驱动语义操作 PMU MCU（STM8S00K3 @ i2c-5 / 0x62）
 *
 * ============================================================================
 * 双协议模型（反汇编工厂内核 drivers/smdt/mcuinf.c 得来，权威）
 * ============================================================================
 * A) 运行时协议（factory write_cmd / read_cmd）
 *     写：一条 i2c_transfer，单 msg {addr=0x62, flags=0, buf=[reg, val]}
 *         随后回读镜像 (reg+0x80) 校验，失败重试 5 次
 *         例外：0x33 喂狗的镜像 0xB3 不可读，故不校验
 *     读：写 1 字节 reg，再读 1 字节。可读区是**镜像区** reg+0x80
 *         （写侧 0x00–0x7F 直读返 0xff，故一律读 +0x80）
 *     适用寄存器：0x32 看门狗使能 / 0x33 喂狗 / 0x51 电源态 /
 *                 0x58–0x5A 关机标志（及其镜像）
 *
 * B) 指针模型（间接寻址，仅 0x01–0x05 有效）
 *     写选择子 0x05 = 目标 reg
 *     写数据窗 0x06 / 0x07 / 0x08
 *     读回显窗 0x85(echo=选择子) / 0x86–0x88(数据)
 *     ⚠ select 落在 0x01–0x05 之外（含 0xB1/0xB2）会被 MCU NAK → SDA 拉死、
 *       只能拔电。**因此指针操作被严格限制在 0x01–0x05**。
 *
 * 0x02 开机模式是**配置型**寄存器（落在 0x01–0x05），必须走模型 B：
 *   - mcu_smdt --set 0x02 0x00（模型 A）是**伪通过**：写了 [0x02,0x00]，校验读
 *     0x82 恒 0x00 恰好相等 → 报 OK，但 0x82 对该寄存器不维护，真值未变。
 *   - 真值只能经指针路径（select 0x05→读 0x86）读到/改到，且跨断电持久。
 *   - 0x00 = 上电自启；0xAA = 按键开机（=工厂态）。
 *   => 统一封装见 --boot-mode。
 *
 * ioctl 语义映射（来自 MCU_ioctl 跳转表）：
 *   0xAA notify_power_on -> write_cmd(0x51, 0x33)
 *   0xA2 setWatchDogEnable -> write_cmd(0x32, arg)   arg=1 开、0 关
 *   0xA3 setWatchDogFeed   -> write_cmd(0x33, 0xAB)
 *   0xA6 getMcuByCMD       -> read_cmd(arg)
 *   0xA8 setMcuByCMD       -> write_cmd(cmd, val)
 *
 * 工厂内核还起了 kthread mcu_run_thread：每 1000 ms 喂一次 (0x33,0xAB)，条件是
 * mcu_data+0x64 != 0（该标志由 ioctl 0xA2 设置）。probe 时会先 write_cmd(0x32,1)。
 *
 * 用法：
 *   mcu_smdt --notify              一次性上报"已开机"：写 [0x51,0x33]，回读 0xD1 校验
 *   mcu_smdt --feed                一次性喂狗：写 [0x33,0xAB]
 *   mcu_smdt --wdt on|off          开关 MCU 侧看门狗：写 [0x32,1|0]
 *   mcu_smdt --get <reg>           读寄存器（按寄存器类自动路由协议）
 *   mcu_smdt --set <reg> <val>     写寄存器（按寄存器类自动路由协议）
 *   mcu_smdt --boot-mode get               读 0x02 开机模式（指针路径，真值）
 *   mcu_smdt --boot-mode set auto          设 0x02=0x00（上电自启）
 *   mcu_smdt --boot-mode set button        设 0x02=0xAA（按键开机，=工厂态）
 *   mcu_smdt --boot-mode set factory       等价 set button
 *   mcu_smdt --watch [--ms 1000]   常驻：先 notify，再每 ms 喂狗（等价工厂 kthread）
 *   mcu_smdt --daemon [--ms 1000]  生产模式（init 服务用的就是它）：
 *                                  (1) 写 [0x32,1] 复位看门狗使能（等价 factory probe）
 *                                  (2) 写 [0x51,0x33] 上报已开机（等价 mcu_tool notify_power_on）
 *                                  (3) 每 ms 喂狗 [0x33,0xAB]（等价 mcu_run_thread）
 *                                  notify 若首发失败会在循环里每 5 次重试，直到成功。
 *   mcu_smdt --dump                打印关心的几个寄存器（按类正确读）
 *   可选： -b <bus>（默认 5） -a <addr>（默认 0x62）
 *
 * 退出码：0 成功；1 参数错；2 I/O 失败。
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

static int g_fd = -1;
static int g_addr = 0x62;

/* ===== 底层 I2C ===== */
static int i2c_wr(uint8_t reg, uint8_t val)
{
    uint8_t out[2];
    struct i2c_msg m;
    struct i2c_rdwr_ioctl_data d;
    out[0] = reg;
    out[1] = val;
    m.addr = (unsigned short)g_addr;
    m.flags = 0;
    m.len = 2;
    m.buf = out;
    d.msgs = &m;
    d.nmsgs = 1;
    return ioctl(g_fd, I2C_RDWR, &d);
}

static int i2c_rd(uint8_t reg, uint8_t *buf, int len)
{
    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data d;
    msgs[0].addr = (unsigned short)g_addr; msgs[0].flags = 0;  msgs[0].len = 1; msgs[0].buf = &reg;
    msgs[1].addr = (unsigned short)g_addr; msgs[1].flags = I2C_M_RD;
    msgs[1].len = (unsigned short)len;     msgs[1].buf = buf;
    d.msgs = msgs;
    d.nmsgs = 2;
    return ioctl(g_fd, I2C_RDWR, &d);
}

/* ===== 寄存器分类：0x01–0x05 是配置型，走指针模型；其余走运行时协议 ===== */
static int is_config_reg(uint8_t reg)
{
    return reg >= 0x01 && reg <= 0x05;
}

/* 裸写：只发一条 transfer，不做回读校验。用于 0x33（喂狗）——其镜像 0xB3 不可读。 */
static int mcu_write_raw(uint8_t reg, uint8_t val)
{
    int r = i2c_wr(reg, val);
    /* ioctl(I2C_RDWR) 成功时返回"完成的 msg 条数"（这里是 1），不是 0。 */
    return r >= 0 ? 0 : r;
}

/* 模型 A：写 -> 回读 (reg+0x80) 校验，最多 5 次（0x33 不要调它，无镜像） */
static int mcu_write_cmd(uint8_t reg, uint8_t val)
{
    int i, got;
    for (i = 0; i < 5; i++) {
        if (i2c_wr(reg, val) < 0) {
            fprintf(stderr, "  [i2c_wr 0x%02x<-%02x] %s\n", reg, val, strerror(errno));
            usleep(100 * 1000);
            continue;
        }
        usleep(20 * 1000);
        got = -1;
        {
            uint8_t b = 0;
            if (i2c_rd((uint8_t)(reg + 0x80), &b, 1) >= 0)
                got = b;
        }
        if (got == (int)val)
            return 0;
        fprintf(stderr, "  校验不符 reg=0x%02x 期望=%02x 回读(0x%02x)=%d\n",
                reg, val, (uint8_t)(reg + 0x80), got);
        usleep(100 * 1000);
    }
    return -2;
}

/* 模型 A：读镜像区 reg+0x80 */
static int mcu_read_mirror(uint8_t reg, uint8_t *out)
{
    int r = i2c_rd((uint8_t)(reg + 0x80), out, 1);
    return r >= 0 ? 0 : -2;
}

/* ===== 模型 B：指针（仅 0x01–0x05）===== */
/* 读：select 0x05=reg -> 读 0x85 回显须==reg -> 读 0x86 数据 */
static int mcu_pointer_read(uint8_t reg, uint8_t *out)
{
    if (!is_config_reg(reg)) return -3;   /* 越界 select 会拉死总线，直接拒绝 */
    if (i2c_wr(0x05, reg) < 0) return -1;
    usleep(20 * 1000);
    uint8_t echo = 0xff;
    if (i2c_rd(0x85, &echo, 1) < 0) return -1;
    if (echo != reg) return -2;           /* MCU 不接受该选择子 */
    uint8_t data = 0;
    if (i2c_rd(0x86, &data, 1) < 0) return -1;
    *out = data;
    return 0;
}

/* 写：select 0x05=reg -> 写 0x06/0x07/0x08 -> 回读验证 */
static int mcu_pointer_write(uint8_t reg, uint8_t val)
{
    if (!is_config_reg(reg)) return -3;
    if (i2c_wr(0x05, reg) < 0) return -1;
    uint8_t b = val;  if (i2c_wr(0x06, b) < 0) return -1;
    b = 0;             if (i2c_wr(0x07, b) < 0) return -1;
    if (i2c_wr(0x08, b) < 0) return -1;
    usleep(20 * 1000);
    uint8_t rb = 0;
    if (mcu_pointer_read(reg, &rb) == 0 && rb == val) return 0;
    return -2;
}

/* 统一读写路由：按寄存器类选协议 */
static int mcu_read_value(uint8_t reg, uint8_t *out)
{
    if (is_config_reg(reg)) return mcu_pointer_read(reg, out);
    return mcu_read_mirror(reg, out);
}
static int mcu_write_value(uint8_t reg, uint8_t val)
{
    if (is_config_reg(reg)) return mcu_pointer_write(reg, val);
    if (reg == 0x33) return mcu_write_raw(reg, val);   /* 无镜像，不校验 */
    return mcu_write_cmd(reg, val);
}

/* ===== 语义命令 ===== */
static int mcu_notify_power_on(void)
{
    return mcu_write_cmd(0x51, 0x33);   /* ioctl 0xAA */
}

static int mcu_feed(void)
{
    return mcu_write_raw(0x33, 0xAB);   /* ioctl 0xA3 */
}

/* ioctl 0xA2 -> write_cmd(0x32, val)。该位在 MCU 侧掉电保持：
 * 实验写成 0 后冷上电读回仍是 0（docs/45 §25），故生产模式必须显式写回 1。 */
static int mcu_wdt_enable(int on)
{
    return mcu_write_cmd(0x32, on ? 1 : 0);
}

/* 0x02 开机模式语义封装（指针路径，真值） */
static const char *boot_mode_name(uint8_t b)
{
    if (b == 0x00) return "auto (power-on boot)";
    if (b == 0xAA) return "button (factory default)";
    return "unknown";
}
static int mcu_boot_mode_get(uint8_t *out)
{
    return mcu_pointer_read(0x02, out);
}
static int mcu_boot_mode_set(uint8_t val)
{
    return mcu_pointer_write(0x02, val);
}

static void usage(const char *p)
{
    fprintf(stderr,
        "用法: %s <mode> [-b bus] [-a addr]\n"
        "  --notify            写 [0x51,0x33] = 通知 MCU 已开机（等价 ioctl 0xAA）\n"
        "  --feed              写 [0x33,0xAB] = 喂狗（等价 ioctl 0xA3）\n"
        "  --wdt on|off        写 [0x32,1|0]  = 开关看门狗（等价 ioctl 0xA2）\n"
        "  --get <reg>         读寄存器（按类自动路由协议）\n"
        "  --set <reg> <val>   写寄存器（按类自动路由协议）\n"
        "  --boot-mode get                 读 0x02 开机模式（指针路径真值）\n"
        "  --boot-mode set auto            设 0x02=0x00（上电自启）\n"
        "  --boot-mode set button          设 0x02=0xAA（按键开机=工厂态）\n"
        "  --boot-mode set factory         等价 set button\n"
        "  --dump              打印关键寄存器（按类正确读）\n"
        "  --watch [--ms N]    常驻：notify + 每 N ms 喂狗（默认 1000）\n"
        "  --daemon [--ms N]   生产模式：开看门狗 + notify + 每 N ms 喂狗（默认 1000）\n", p);
}

int main(int argc, char **argv)
{
    int bus = 5, i, mode = 0, wdt_on = 1, ms = 1000, notify_first = 0;
    int reg = -1, val = -1;
    uint8_t boot_val = 0xAA;  /* set 时：0x00=auto，0xAA=button/factory */
    char path[64];

    if (argc < 2) { usage(argv[0]); return 1; }

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--notify")) mode = 1;
        else if (!strcmp(argv[i], "--feed")) mode = 2;
        else if (!strcmp(argv[i], "--wdt") && i + 1 < argc) {
            mode = 3; wdt_on = !strcmp(argv[++i], "on");
        } else if (!strcmp(argv[i], "--get") && i + 1 < argc) {
            mode = 4; reg = (int)strtol(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--set") && i + 2 < argc) {
            mode = 5;
            reg = (int)strtol(argv[++i], NULL, 0);
            val = (int)strtol(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--dump")) mode = 6;
        else if (!strcmp(argv[i], "--watch")) mode = 7;
        else if (!strcmp(argv[i], "--daemon")) mode = 8;
        else if (!strcmp(argv[i], "--boot-mode") && i + 1 < argc) {
            char *act = argv[++i];
            if (!strcmp(act, "get")) { mode = 9; }
            else if (!strcmp(act, "set") && i + 1 < argc) {
                char *sub = argv[++i];
                if (!strcmp(sub, "auto"))         { boot_val = 0x00; }
                else if (!strcmp(sub, "button"))  { boot_val = 0xAA; }
                else if (!strcmp(sub, "factory")) { boot_val = 0xAA; }
                else { fprintf(stderr, "未知 --boot-mode set 值: %s\n", sub); usage(argv[0]); return 1; }
                mode = 10;
            } else { fprintf(stderr, "未知 --boot-mode 动作: %s\n", act); usage(argv[0]); return 1; }
        } else if (!strcmp(argv[i], "--notify-first")) notify_first = 1;
        else if (!strcmp(argv[i], "--ms") && i + 1 < argc) ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-b") && i + 1 < argc) bus = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-a") && i + 1 < argc) g_addr = (int)strtol(argv[++i], NULL, 0);
        else { fprintf(stderr, "未知参数: %s\n", argv[i]); usage(argv[0]); return 1; }
    }

    /* 防呆：--ms 传 0 会变成忙等，把 CPU 和 I2C 打满 */
    if (ms < 100) ms = 100;

    snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
    g_fd = open(path, O_RDWR);
    if (g_fd < 0) {
        fprintf(stderr, "打不开 %s: %s\n", path, strerror(errno));
        return 2;
    }
    if (ioctl(g_fd, I2C_SLAVE, g_addr) < 0) {
        /* 部分内核要求先 SLAVE_FORCE；失败仅告警，I2C_RDWR 自带 addr */
        fprintf(stderr, "I2C_SLAVE 0x%02x: %s（继续）\n", g_addr, strerror(errno));
    }

    switch (mode) {
    case 1:
        if (mcu_notify_power_on() == 0) { printf("notify_power_on OK  [0x51]=0x33\n"); return 0; }
        fprintf(stderr, "notify_power_on 失败\n"); return 2;

    case 2:
        if (mcu_feed() == 0) { printf("feed OK  [0x33]=0xAB\n"); return 0; }
        fprintf(stderr, "feed 失败\n"); return 2;

    case 3:
        if (mcu_write_cmd(0x32, wdt_on ? 1 : 0) == 0) {
            printf("wdt %s OK  [0x32]=0x%02x\n", wdt_on ? "on" : "off", wdt_on ? 1 : 0);
            return 0;
        }
        fprintf(stderr, "wdt 失败\n"); return 2;

    case 4: {
        uint8_t b = 0;
        if (mcu_read_value((uint8_t)reg, &b) == 0) {
            if (is_config_reg((uint8_t)reg))
                printf("reg 0x%02x (pointer) = 0x%02x\n", reg, b);
            else
                printf("reg 0x%02x (mirror 0x%02x) = 0x%02x\n", reg, (uint8_t)(reg + 0x80), b);
            return 0;
        }
        fprintf(stderr, "读 0x%02x 失败\n", reg); return 2;
    }

    case 5:
        if (mcu_write_value((uint8_t)reg, (uint8_t)val) == 0) {
            if (is_config_reg((uint8_t)reg))
                printf("set 0x%02x = 0x%02x OK (pointer)\n", reg, val);
            else
                printf("set 0x%02x = 0x%02x OK\n", reg, val);
            return 0;
        }
        fprintf(stderr, "写 0x%02x 失败\n", reg); return 2;

    case 6: {
        /* 按寄存器类正确读：配置型走指针，运行时走镜像。 */
        static const struct { uint8_t reg; const char *name; int cfg; } rlist[] = {
            {0x02, "boot-mode",   1},
            {0x32, "wdt-enable",  0},
            {0x33, "wdt-feed",    0},
            {0x51, "power-status",0},
            {0x58, "shutdown-1",  0},
            {0x59, "shutdown-2",  0},
            {0x5A, "shutdown-3",  0},
        };
        for (i = 0; i < (int)(sizeof(rlist) / sizeof(rlist[0])); i++) {
            uint8_t b = 0;
            if (rlist[i].cfg) {
                int ok = (mcu_pointer_read(rlist[i].reg, &b) == 0);
                printf("reg 0x%02x %-13s (pointer) = %s0x%02x%s\n",
                       rlist[i].reg, rlist[i].name, ok ? "" : "(读失败) ",
                       b, ok ? "" : "");
            } else if (rlist[i].reg == 0x33) {
                printf("reg 0x%02x %-13s (mirror 0xb3) = write-only（无镜像可读）\n",
                       rlist[i].reg, rlist[i].name);
            } else {
                int ok = (mcu_read_mirror(rlist[i].reg, &b) == 0);
                printf("reg 0x%02x %-13s (mirror 0x%02x) = %s0x%02x\n",
                       rlist[i].reg, rlist[i].name, (uint8_t)(rlist[i].reg + 0x80),
                       ok ? "" : "(读失败) ", b);
            }
        }
        return 0;
    }

    case 9: {
        uint8_t b = 0;
        if (mcu_boot_mode_get(&b) == 0) {
            printf("boot-mode reg 0x02 = 0x%02x (%s)\n", b, boot_mode_name(b));
            return 0;
        }
        fprintf(stderr, "读 boot-mode 0x02 失败（检查是否 0x01–0x05 内、总线状态）\n");
        return 2;
    }

    case 10:
        if (mcu_boot_mode_set(boot_val) == 0) {
            printf("boot-mode set 0x02 = 0x%02x (%s)%s\n", boot_val,
                   boot_val == 0x00 ? "auto" : "button/factory",
                   boot_val == 0xAA ? "（回工厂态：按键开机）" : "");
            return 0;
        }
        fprintf(stderr, "设 boot-mode 0x02 失败\n"); return 2;

    case 7: {
        /* 单进程常驻喂狗。fd 只 open 一次，绝不在循环里 open/close /dev/i2c-5。 */
        unsigned long n = 0, fails = 0;
        if (notify_first) {
            if (mcu_notify_power_on() != 0)
                fprintf(stderr, "警告：首发 notify 失败，仍继续喂狗\n");
            else
                printf("notify_power_on OK [0x51]=0x33\n");
        }
        printf("feed loop start: %d ms, pid=%d\n", ms, (int)getpid());
        fflush(stdout);
        for (;;) {
            if (mcu_feed() != 0) {
                fails++;
                fprintf(stderr, "feed #%lu 失败: %s (累计失败 %lu)\n",
                        n, strerror(errno), fails);
                fflush(stderr);
            } else if ((n % 30) == 0) {
                printf("feed #%lu OK (失败累计 %lu)\n", n, fails);
                fflush(stdout);
            }
            n++;
            usleep((useconds_t)ms * 1000);
        }
    }

    case 8: {
        /* 生产模式（init 服务调用）。顺序严格对齐工厂内核：
         *   probe          -> write_cmd(0x32, 1)      复位看门狗使能
         *   boot_completed -> write_cmd(0x51, 0x33)   上报已开机
         *   mcu_run_thread -> write_cmd(0x33, 0xAB)   每 1000 ms 喂狗
         * fd 只 open 一次：反复 open/close 会触发 rk3x_i2c_release 复位控制器，
         * 把 SDA 拉死（docs/45 §17）。 */
        unsigned long n = 0, fails = 0;
        int notify_ok = 0;

        if (mcu_wdt_enable(1) == 0) {
            printf("wdt on OK  [0x32]=0x01\n");
        } else {
            fprintf(stderr, "警告：写 [0x32]=1 失败，看门狗保持原状\n");
        }

        if (mcu_notify_power_on() == 0) {
            notify_ok = 1;
            printf("notify_power_on OK  [0x51]=0x33\n");
        } else {
            fprintf(stderr, "警告：notify 失败，将在喂狗循环里重试\n");
        }

        printf("daemon start: %d ms, pid=%d\n", ms, (int)getpid());
        fflush(stdout);

        for (;;) {
            if (mcu_feed() != 0) {
                fails++;
                fprintf(stderr, "feed #%lu 失败: %s (累计失败 %lu)\n",
                        n, strerror(errno), fails);
                fflush(stderr);
            } else if ((n % 60) == 0) {
                printf("feed #%lu OK (失败累计 %lu)\n", n, fails);
                fflush(stdout);
            }
            n++;
            if (!notify_ok && (n % 5) == 0 && mcu_notify_power_on() == 0) {
                notify_ok = 1;
                printf("notify_power_on 补发成功（第 %lu 拍）\n", n);
                fflush(stdout);
            }
            usleep((useconds_t)ms * 1000);
        }
    }

    default:
        usage(argv[0]);
        return 1;
    }
}

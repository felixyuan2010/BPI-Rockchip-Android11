/*
 * mcu_smdt.c —— 按 SMDT 工厂驱动语义操作 PMU MCU（STM8S00K3 @ i2c-5 / 0x62）
 *
 * 协议来源：反汇编工厂内核 drivers/smdt/mcuinf.c 的 write_cmd/read_cmd/MCU_ioctl。
 *   write_cmd(reg, val)  => 一条 i2c_transfer，单 msg {addr=0x62, flags=0, buf=[reg, val]}
 *                           随后回读镜像 (reg+0x80) 校验，失败重试 5 次
 *   read_cmd(reg, &out)  => smbus_read_byte_data：先写 1 字节 reg，再读 1 字节
 *                            镜像语义：reg 的值可从 (reg+0x80) 读到
 *
 * ioctl 语义映射（来自 MCU_ioctl 的跳转表）：
 *   0xAA notify_power_on    -> write_cmd(0x51, 0x33)
 *   0xA2 setWatchDogEnable  -> write_cmd(0x32, arg)   arg=1 开、0 关
 *   0xA3 setWatchDogFeed    -> write_cmd(0x33, 0xAB)
 *   0xA6 getMcuByCMD        -> read_cmd(arg)
 *   0xA8 setMcuByCMD        -> write_cmd(cmd, val)
 *
 * 工厂内核还起了 kthread mcu_run_thread：每 1000 ms 喂一次 (0x33,0xAB)，条件是
 * mcu_data+0x64 != 0（该标志由 ioctl 0xA2 设置）。probe 时会先 write_cmd(0x32,1)。
 *
 * 用法：
 *   mcu_smdt --notify              一次性上报"已开机"：写 [0x51,0x33]，回读 0xD1 校验
 *   mcu_smdt --feed                一次性喂狗：写 [0x33,0xAB]
 *   mcu_smdt --wdt on|off          开关 MCU 侧看门狗：写 [0x32,1|0]
 *   mcu_smdt --get 0x51            读寄存器（直接寻址）
 *   mcu_smdt --set 0x51 0x33       写寄存器
 *   mcu_smdt --watch [--ms 1000]   常驻：先 notify，再每 ms 喂狗（等价工厂 kthread）
 *   mcu_smdt --daemon [--ms 1000]  生产模式（init 服务用的就是它）：
 *                                  (1) 写 [0x32,1] 复位看门狗使能（等价 factory probe）
 *                                  (2) 写 [0x51,0x33] 上报已开机（等价 mcu_tool notify_power_on）
 *                                  (3) 每 ms 喂狗 [0x33,0xAB]（等价 mcu_run_thread）
 *                                  notify 若首发失败会在循环里每 5 次重试，直到成功。
 *   mcu_smdt --dump                打印关心的几个寄存器
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

/* 裸写：只发一条 transfer，不做回读校验。
 * 用于 0x33（喂狗）——驱动 write_cmd 对 cmd==0x33 特判跳过校验，
 * 因为 0x33 的镜像 0xB3 不可读（实测恒 0xff）。 */
static int mcu_write_raw(uint8_t reg, uint8_t val)
{
    int r = i2c_wr(reg, val);
    /* 注意：ioctl(I2C_RDWR) 成功时返回"完成的 msg 条数"（这里是 1），不是 0。
     * 早期版本用 `!= 0` 判断，导致每次成功都被误报成失败（errno=Success）。 */
    return r >= 0 ? 0 : r;
}

/* 复刻 write_cmd：写 -> 回读 (reg+0x80) 校验，最多 5 次 */
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

static int mcu_read_cmd(uint8_t reg, uint8_t *out)
{
    int r = i2c_rd(reg, out, 1);
    return r >= 0 ? 0 : -2;
}

static int mcu_notify_power_on(void)
{
    return mcu_write_cmd(0x51, 0x33);   /* ioctl 0xAA */
}

static int mcu_feed(void)
{
    /* ioctl 0xA3；0x33 无镜像可读，故与驱动一致：不校验 */
    return mcu_write_raw(0x33, 0xAB);
}

/* ioctl 0xA2 -> write_cmd(0x32, val)。该位在 MCU 侧是**掉电保持**的：
 * 曾经实验把它写成 0 之后，冷上电读回来仍是 0（docs/45 §25）。
 * 因此生产模式必须显式写回 1，否则看门狗永远不再工作。 */
static int mcu_wdt_enable(int on)
{
    return mcu_write_cmd(0x32, on ? 1 : 0);
}

static void usage(const char *p)
{
    fprintf(stderr,
        "用法: %s <mode> [-b bus] [-a addr]\n"
        "  --notify            写 [0x51,0x33] = 通知 MCU 已开机（等价 ioctl 0xAA）\n"
        "  --feed              写 [0x33,0xAB] = 喂狗（等价 ioctl 0xA3）\n"
        "  --wdt on|off        写 [0x32,1|0]  = 开关看门狗（等价 ioctl 0xA2）\n"
        "  --get <reg>         读寄存器\n"
        "  --set <reg> <val>   写寄存器\n"
        "  --dump              打印关键寄存器及其镜像\n"
        "  --watch [--ms N]    常驻：notify + 每 N ms 喂狗（默认 1000）\n"
        "  --daemon [--ms N]   生产模式：开看门狗 + notify + 每 N ms 喂狗（默认 1000）\n", p);
}

int main(int argc, char **argv)
{
    int bus = 5, i, mode = 0, wdt_on = 1, ms = 1000, notify_first = 0;
    int reg = -1, val = -1;
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
        else if (!strcmp(argv[i], "--notify-first")) notify_first = 1;
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
        if (mcu_read_cmd((uint8_t)reg, &b) == 0) { printf("reg 0x%02x = 0x%02x\n", reg, b); return 0; }
        fprintf(stderr, "读 0x%02x 失败\n", reg); return 2;
    }

    case 5:
        if (mcu_write_cmd((uint8_t)reg, (uint8_t)val) == 0) {
            printf("set 0x%02x = 0x%02x OK\n", reg, val); return 0;
        }
        fprintf(stderr, "写 0x%02x 失败\n", reg); return 2;

    case 6: {
        /* 注意：写入侧 0x00-0x7F 多数不可直接读（返回 0xff），
         * 可读的是镜像区 reg+0x80。所以 --dump 一律读镜像。 */
        static const int rlist[] = { 0x02, 0x32, 0x33, 0x51, 0x58, 0x59, 0x5A };
        for (i = 0; i < (int)(sizeof(rlist) / sizeof(rlist[0])); i++) {
            uint8_t b = 0;
            int mreg = rlist[i] + 0x80;
            int ok = (mcu_read_cmd((uint8_t)mreg, &b) == 0);
            printf("reg 0x%02x (mirror 0x%02x) = %s0x%02x\n",
                   rlist[i], mreg, ok ? "" : "(读失败) ", b);
        }
        return 0;
    }

    case 7: {
        /* 单进程常驻喂狗。与工厂 mcu_run_thread 等价：
         *   msleep(1000) -> write_cmd(0x33, 0xAB)
         * 关键：fd 只 open 一次，绝不在循环里 open/close /dev/i2c-5。 */
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

/*
 * SPDX-License-Identifier:     GPL-2.0+
 *
 * (C) Copyright 2020 Rockchip Electronics Co., Ltd
 */

#include <common.h>
#include <dwc3-uboot.h>
#include <key.h>
#include <usb.h>

DECLARE_GLOBAL_DATA_PTR;

#ifdef CONFIG_USB_DWC3
static struct dwc3_device dwc3_device_data = {
	.maximum_speed = USB_SPEED_HIGH,
	.base = 0xfcc00000,
	.dr_mode = USB_DR_MODE_PERIPHERAL,
	.index = 0,
	.dis_u2_susphy_quirk = 1,
	.usb2_phyif_utmi_width = 16,
};

int usb_gadget_handle_interrupts(void)
{
	dwc3_uboot_handle_interrupt(0);
	return 0;
}

int board_usb_init(int index, enum usb_init_type init)
{
	return dwc3_uboot_init(&dwc3_device_data);
}
#endif

/*
 * 本板（SMDT AIoT-3568A V2.0）的 UBOOT/升级键在 dtb 里是 adc-keys/uboot-key，
 * linux,code = KEY_F7 (65)；公版参考板上才是 KEY_VOLUMEUP (115)。
 *
 * 上游 arch/arm/mach-rockchip/boot_rkimg.c 的 __weak 版只查 KEY_VOLUMEUP，
 * 本板因此永远认不到按键。2026-10-30 实机串口实测（按住键上电）：
 *   [K3] ev_up=0 ev_f7=1 raw=127 ret=0
 * 即：同一个 ADC 通道读到 127，KEY_VOLUMEUP 判定为未按，KEY_F7 判定为按下。
 * 补充：board_late_init() 执行时 gd->fdt_blob 已是内核 dtb（0x0a100000），
 * 所以改 U-Boot 自己 dts 里的阈值是无效的（9 / 223000 / 224000 都试过，均无效）。
 *
 * 按 boot_rkimg.c 里 "It's better to make this a weak function that can be
 * overridden by some special boards" 的设计意图，在此覆盖之：两个 code 都认，
 * 既兼容公版，也支持本板。
 */
int rockchip_dnl_key_pressed(void)
{
	int ev = key_read(KEY_VOLUMEUP);

	if (key_is_pressed(ev))
		return 1;

	ev = key_read(KEY_F7);

	return key_is_pressed(ev);
}

/*
 * display.c - display subsystem driver
 *
 * Copyright (c) 2013 William Smith <terminalnt@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/clk.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/ioctl.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <mach/power-gate.h>
#include <mach/imap-iomap.h>
#include <mach/items.h>
#include <linux/gpio.h>

#include <mach/pad.h>

#include <dss_common.h>
#include <abstraction.h>
#include <implementation.h>
#include <ids_access.h>
#include <controller.h>
#include <imapx_dev_mmu.h>
#include <interface.h>
#include <scaler.h>
#include "api.h"

#define DSS_LAYER	"[display]"
#define DSS_SUB1	""
#define DSS_SUB2	""

#define IM_BUFFER_FLAG_DEVADDR	(1 << 2)
#define DMMU_DEV_OSD1			(13)
#define DMMU_DEV_OSD0          (12)
#define DMMU_DEV_IDS1_W1           (6)

#define HDMI_IOCTL_VSYNC		0x10004
#define HDMI_IOCTL_SWAP			0x10002
#define HDMI_IOCTL_DEVINFO		0x18001
#define HDMI_IOCTL_GETVIC		0x18002
#define HDMI_IOCTL_SETVIC 		0x18003
#define HDMI_IOCTL_ENABLE		0x18004
#define HDMI_IOCTL_QUERY		0x10005
#define HDMI_IOCTL_EVENT		0x10003
#define CVBS_IOCTL_EVENT		0x10006
#define HDMI_IOCTL_CVBS_SWAP_BUFFER       0x18006
#define HDMI_IOCTL_GET_CVBS_TYPE       0x18008
#define HDMI_IOCTL_GET_PRODUCT_TYPE       0x18005
#define HDMI_IOCTL_DISABLE_CVBS       0x18007

extern int lcd_vic;
static int hdmi_vic = 16;

struct hdmi_vic_user {
	int count;
	int vic[40];
};

struct dmmu_buffer {
    int		uid;
    void *	vir_addr;
    int		phy_addr;
    int		dev_addr;
    int		size;
    int		flag;
};

struct hdmi_device {
	int hplg;
	int enabled;
	int cvbs_enabled;
	int saved_state;
	int vic;
	int cvbs_vic;
	int enable_request;
	struct work_struct work;
#ifdef CONFIG_IMAPX15_G2D
	struct delayed_work sync_work;
#endif
	struct cdev *cdev;
	dev_t dev;
	wait_queue_head_t wait;
	wait_queue_head_t cvbs_wait;
} * imapx_hdmi;

int dss_log_level = DSS_INFO;

extern int backlight_en_control(int enable);
extern int hdmi_update_state(int);
extern int hdmi_audio_update_state(int);
extern int hdmi_get_vic(int a[], int len);
extern int hdmi_get_monitor(char *, int);
extern int terminal_configure(char * terminal_name, int vic, int enable);
extern int ids_access_sysmgr_init(void);
extern int swap_hdmi_buffer(u32 phy_addr);
extern int swap_cvbs_buffer(u32 phy_addr);
static struct mutex dmmu_mutex;
extern int pt;        
extern int cvbs_style;
extern int pre_vic;
extern dma_addr_t      map_dma_cvbs;   /* physical */
extern u_char *            map_cpu_cvbs;   /* virtual */
extern int map_cvbs_size;
extern int lcd_default_enable(int enable);
extern int blanking;
extern int hdmi_state;
static int spk_pad = 0;

int display_atoi(char *s)
{
	int num=0,flag=0;
	int i;
	for(i=0;i<=strlen(s);i++)
	{
		if(s[i] >= '0' && s[i] <= '9')
			num = num * 10 + s[i] -'0';
		else if(s[0] == '-' && i==0)
			flag =1;
		else
			break;
	}

	if(flag == 1)
		num = num * -1;

	return(num);
}

int hdmi_hpd_event(int hpd)
{
	dss_trace("%d\n", hpd);

	imapx_hdmi->hplg = !!hpd;

	/* invoke hdmi_work */
	schedule_work(&imapx_hdmi->work);
	return 0;
}

void dump_ids1(void)
{
	uint32_t regs[] = {
		0x0, 0x4, 0x8, 0xc, 0x10, 0x18, 0x30, 0x54, 0x58, 0x5c,
		0x1000, 0x1004, 0x1008, 0x100c, 
		0x1080, 0x1084, 0x1088, 0x108c, 0x1090, 0x1094, 0x1098, 0x109c, 0x10a0,
		0x5000, 0x5004, 0x5008, 0x500c, 0x5010, 0x5014, 0x5018,
	}, i;

	void __iomem * r;

	r = ioremap(0x24000000, 0x8000);

	if(!r) {
		printk(KERN_ERR "failed to remap ids1.\n");
		return ;
	}

	printk(KERN_ERR "ids1 regs:\n");
	for(i = 0; i < ARRAY_SIZE(regs); i++)
		printk(KERN_ERR "0x%08x: 0x%08x\n", 0x24000000 + regs[i],
				readl(r + regs[i]));

	iounmap(r);
}

int hdmi_hp = 0;
int hdmi_en = 0;
extern int hdmi_app_display;
int hdmi_enable(int en)
{
	dss_dbg("%d\n", en);
	char name[64];
	int ret = 0;

	if(imapx_hdmi->enabled  == en || hdmi_en == 0 || hdmi_state == en)
//	if(imapx_hdmi->enabled  == en)
		return 0;

	hdmi_state = en;
	if(en) {
		/* hdmi will be acturly enabled when the first
		 * swap buffer is available.
		 *
		 * or else, ids1 will be enabled with an invalid
		 * memory, which result in a black or crash screen.
		 */
		if (spk_pad)
			gpio_set_value(spk_pad,  0);
		backlight_en_control(0);
		hdmi_hp = 1;
		msleep(50);
		terminal_configure("lcd_panel", lcd_vic, 0);
		set_controller(DSS_INTERFACE_LCDC); //1:lcdc 2:tvif 3:i80
		set_interface(2); // 0:lcd 1:cvbs 2:hdmi
		module_power_down(SYSMGR_IDS1_BASE);     
		msleep(10);                              
		module_power_on(SYSMGR_IDS1_BASE);       
		ids_writeword(1, 0x5c, 0);               
		msleep(10);                              
		ret = api_Initialize(0, 1, 2500, 1);     
		if(ret == 0) {                           
			dss_err("api_Initialize() failed\n");
			return -1;                           
		}                                        

		hdmi_get_monitor(name, 64);
		writel(0x1, IO_ADDRESS(SYSMGR_IDS1_BASE + 0x1C));
		terminal_configure("hdmi_sink", hdmi_vic/*imapx_hdmi->vic*/, 1);
		hdmi_hp = 0;
		msleep(20);
		if(hdmi_app_display)//for hdmi inserted in and out in app
		{
			ids_write(1, OVCW1CKCR, 24, 1, 1);
			ids_write(1, OVCW1CKCR, 25, 1, 1);
			ids_writeword(1, OVCW1CKR, 0x1f);
		}
		imapx_hdmi->enabled = 1;
		imapx_hdmi->enable_request = 1;
	} else {
		hdmi_hp = 1;
		terminal_configure("hdmi_sink", hdmi_vic/*imapx_hdmi->vic*/, 0);
		set_controller(lcd_interface);//1:lcdc 2:tvif 3:i80
		set_interface(0);// 0:lcd 1:cvbs 2:hdmi
		module_power_down(SYSMGR_IDS1_BASE);     
		msleep(10);                              
		module_power_on(SYSMGR_IDS1_BASE);       
		ids_writeword(1, 0x5c, 0);               
		ret = api_Initialize(0, 1, 2500, 1);     
		if(ret == 0) {                           
			dss_err("api_Initialize() failed\n");
			return -1;                           
		} 
		writel(0x0, IO_ADDRESS(SYSMGR_IDS1_BASE + 0x1C));
		terminal_configure("lcd_panel", lcd_vic, 1);
		msleep(100);
		hdmi_hp = 0;
		backlight_en_control(1); 
		if (spk_pad)
			gpio_set_value(spk_pad,  1);
		imapx_hdmi->enable_request = 0;
		imapx_hdmi->enabled = 0;
	}

	return 0;
}
EXPORT_SYMBOL(hdmi_enable);

extern dma_addr_t gmap_dma_f1;
extern u_char * gmap_cpu_f1;
static void hdmi_output(void)
{
	char name[64];
	swap_hdmi_buffer(gmap_dma_f1);

	dss_info("enable HDMI on the first swap.\n");
	hdmi_get_monitor(name, 64);
	terminal_configure("hdmi_sink", hdmi_vic/*imapx_hdmi->vic*/, 1);
	ids_writeword(1, OVCW0CMR, 0x0000000);//dma work
	msleep(20);

	imapx_hdmi->enabled = 1;
}

static void hdmi_work_func(struct work_struct *work)
{
	char name[64];

	if(!imapx_hdmi->hplg) {
		hdmi_enable(0);
		hdmi_update_state(0);
		hdmi_audio_update_state(0);
	} else {
		hdmi_enable(1);
		hdmi_update_state(1);
		hdmi_audio_update_state(1);
//		hdmi_output();
	}
}

static int hdmi_open(struct inode *inode, struct file *filp)
{
	dss_dbg("\n");
	return 0;
}

static int hdmi_release(struct inode *inode, struct file *filp)
{
	dss_dbg("\n");
	return 0;
}

static const struct file_operations hdmi_fops = {
	.owner = THIS_MODULE,
	.open = hdmi_open,
	.release = hdmi_release,
};

static int display_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	struct hdmi_device *hdmi;
	struct class *c;

	dss_trace("line: %d\n", __LINE__);

	hdmi = kzalloc(sizeof(struct hdmi_device), GFP_KERNEL);
	if(!hdmi) {
		dss_err("failed to alloc device buffer.\n");
		return -1;
	}
	imapx_hdmi = hdmi;

	ret = alloc_chrdev_region(&hdmi->dev, 0, 1, "hdmi");
	if (ret) {
		dss_err("alloc chrdev region failed\n");
		return -1;
	}

	hdmi->cdev = cdev_alloc();
	cdev_init(hdmi->cdev, &hdmi_fops);
	hdmi->cdev->owner = hdmi_fops.owner;

	ret = cdev_add(hdmi->cdev, hdmi->dev, 1);
	if (ret)
		dss_err("cdev add failed %d\n", ret);

	c = class_create (THIS_MODULE, "hdmi");
	if(IS_ERR(c)) {
		dss_err("failed to create class.\n");
		return -1;
	}
	device_create(c, NULL, hdmi->dev, NULL, "hdmi");
	init_waitqueue_head(&hdmi->wait);

	INIT_WORK(&hdmi->work, hdmi_work_func);
	if(item_exist("sound.speaker")){
		spk_pad = item_integer("sound.speaker", 1);
		ret = gpio_request(spk_pad, "spk io");
		if (ret) {
			dss_err("request gpio for spk failed\n");
		} else {
			gpio_direction_output(spk_pad, 1);
			gpio_set_value(spk_pad,  1);
		}
	}
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	ids_irq_set(1, res->start);	
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0); 
	ids_access_Initialize(1, res);
	mutex_init(&dmmu_mutex);

	/* initialization */
	/* 
	 *(1)Author:summer
	 *(2)Date:2014-6-26
	 *(3)Reason:
	 *  The logo from uboot to kernel can't be continous,the modification is 
	 *  required by alex.
	 * */
	implementation_init();
	abstraction_init();
	return 0;
}

extern struct clk *hdmi_clk, *clk_ids1, *clk_ids1_osd, *clk_ids1_eitf;
static int display_suspend(struct device *dev)
{
	dss_trace("%d\n", __LINE__);


	/* disable ids1 and HDMI and dmmu to
	 * let it work properly after resume.
	 *
	 * a flag called @saved_state is set.
	 * if this flag is detected during an HPD_IN
	 * event, ids1 and HDMI will be automatically
	 * enabled in regardless with upper-layer's
	 * instruction.
	 */
	if((imapx_hdmi->enabled ) && (pt == PRODUCT_MID)) {
		imapx_hdmi->saved_state = 1;
		hdmi_enable(0);
	}

	if (blanking == 0) {
		backlight_en_control(0);                    

		terminal_configure("lcd_panel", lcd_vic, 0);
		ids_writeword(1, 0x1090, 0x1000000);        
		blanking = 1;
	}
	
	disable_irq(GIC_IDS1_ID);
	return 0;
}

extern void imapx_bl_power_on(void);
static int display_resume(struct device *dev)
{
	int vic[40];
	dss_trace("%d\n", __LINE__);

	ids_access_sysmgr_init();
	implementation_init();
	if (pt == PRODUCT_BOX) {
		clk_prepare_enable(clk_ids1);     
		clk_prepare_enable(clk_ids1_osd); 
		clk_prepare_enable(clk_ids1_eitf);
		clk_prepare_enable(hdmi_clk);     

		module_power_on(SYSMGR_IDS1_BASE);
		ids_writeword(1, 0x5c, 0);        
		msleep(10);
		api_Initialize(0, 1, 2500, 1);
		hdmi_get_vic(vic, 64);
		terminal_configure("hdmi_sink", hdmi_vic, 1);
	} else if (pt == PRODUCT_MID) {
		//if (blanking == 1) {
			ids_writeword(1, 0x1090, 0x0);              

			terminal_configure("lcd_panel", lcd_vic, 1);
//			backlight_en_control(1);
			imapx_bl_power_on();               
			blanking = 0;
		//}
	}

	enable_irq(GIC_IDS1_ID);
	return 0;
}

static const struct dev_pm_ops display_pm_ops = {
	.suspend		= display_suspend,
	.resume		= display_resume,
};

static struct platform_driver display_driver = {
	.probe		= display_probe,
	.driver		= {
		.name	= "display",
		.owner	= THIS_MODULE,
		.pm = &display_pm_ops,
	},
};

int __init display_init(void)
{
	return platform_driver_register(&display_driver);
}
static void __exit display_exit(void)
{
	platform_driver_unregister(&display_driver);
}

module_init(display_init);
module_exit(display_exit);

MODULE_DESCRIPTION("InfoTM iMAP display subsystem driver");
MODULE_AUTHOR("warits");
MODULE_LICENSE("GPL v2");

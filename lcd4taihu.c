/*

   Copyright 2008-2012 Peter Huewe <peterhuewe (at) gmx.de>

   This is a simple driver for the LCD Character Display on AMCC's taihu
   development platform.
   This driver was created as a project work during my studies and is just
   updated to the latest kernel versions, as I don't have the hardware anymore.

   Please consider writing me an email if you use this driver successfully.
   AMCC and Taihu are (probabl) registered Trademarks of.
   Applied Micro Circuits Corporation - I'm in not affiliated with them.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.


*/
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/fs.h>		/* everything... */
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>		/* kmalloc() */
#include <linux/io.h>		/* iowrite8/ioread8 */

/*<1>*/
#define LCD_BCKL_ADDR  0x50100001
#define LCD_CMD_ADDR  0x50100002
#define LCD_DATA_ADDR 0x50100003
#define CMD_CLEAR_DISPLAY 0x01
#define CMD_SET_HOME 0x80

/*<2>*/
static volatile __iomem void *data_mmap;
static volatile __iomem void *cmd_mmap;
static volatile __iomem void *bckl_mmap;
static unsigned char g_addr;

static int taihu_lcd_open(struct inode *dev_file, struct file *f_instance);
static ssize_t taihu_lcd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
static int __init taihu_lcd_init(void);
static void __exit taihu_lcd_cleanup(void);

static int taihu_lcd_open(struct inode *dev_file, struct file *f_instance)
{
	if (!(f_instance->f_flags & O_APPEND)) {	/* If we do not append to device we flush it */
		iowrite8(CMD_CLEAR_DISPLAY, (void __iomem *)cmd_mmap);	/* flush and return home */
		udelay(2000);
		g_addr = CMD_SET_HOME;	/* set cursor to first line, first character */
	}
	return 0;
}

/*<3>*/
static ssize_t taihu_lcd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	size_t i = 0;
	unsigned char *ks_buf = kmalloc(count + 1, GFP_KERNEL);	/* +1 for nullbyte */
	(*f_pos) += count;	/* TODO check how many bytes writen. */
	if (ks_buf == NULL) {
		pr_crit("Kmalloc failed!\n");
	} else {
		if (!copy_from_user(ks_buf, buf, count)) {
			for (i = 0; i < count; i++) {
				iowrite8(ks_buf[i], (void __iomem *)data_mmap);
				udelay(2000);
				g_addr++;
				if (g_addr & 0x10)	/* end of lineclass_ reached - change to other line <4> */
				{
					g_addr ^= 0x40;	/* Toggle Second line */
					g_addr &= 0xC0;	/* Reset cursor to first char of line */
					iowrite8(g_addr, (void __iomem *)cmd_mmap);
					udelay(2000);
				}
			}
		} else {
			ks_buf[count] = '\0';	/* Nullbyte for printing */
			pr_err("Copy from user failed! Value %s anzahl %zd\n", ks_buf, count);
		}
		kfree(ks_buf);
	}
	return count;
}

/*<5>*/
static struct file_operations taihu_lcd_ops = {
	.owner = THIS_MODULE,
	.open = taihu_lcd_open,
	.write = taihu_lcd_write
};

/*<6>*/
static ssize_t store_hex_cmd(struct device *dev, struct device_attribute *attr, const char *buffer, size_t size)
{
	char new = simple_strtol(buffer, NULL, 16);
	iowrite8(new, (void __iomem *)cmd_mmap);
	return 4;
}

static ssize_t store_hex_data(struct device *dev, struct device_attribute *attr, const char *buffer, size_t size)
{
	char new = simple_strtol(buffer, NULL, 16);
	iowrite8(new, (void __iomem *)data_mmap);
	return 4;
}

static ssize_t store_cmd(struct device *dev, struct device_attribute *attr, const char *buffer, size_t size)
{
	iowrite8(buffer[0], (void __iomem *)cmd_mmap);
	return 1;
}

static ssize_t store_data(struct device *dev, struct device_attribute *attr, const char *buffer, size_t size)
{
	iowrite8(buffer[0], (void __iomem *)data_mmap);
	return 1;
}

static ssize_t get_backlight(struct device *dev, struct device_attribute *attr, char *buffer)
{
	ssize_t len = 0;
	char backlight = ioread8((void __iomem *)bckl_mmap);
	backlight >>= 1;
	backlight &= 0x01;
	len = snprintf(buffer, 3, "%d\n", backlight);
	return len;
}

static ssize_t set_backlight(struct device *dev, struct device_attribute *attr, const char *buffer, size_t size)
{
	char on;
	char backlight = ioread8((void __iomem *)bckl_mmap);
	on = simple_strtol(buffer, NULL, 2);
	if (on == 1) {
		backlight |= 0x02;
	} else if (on == 0) {
		backlight &= ~(0x02);
	} else {		/* Error */
		return -EINVAL;
	}
	iowrite8(backlight, (void __iomem *)bckl_mmap);
	return size;
}

/*<7>*/
static DEVICE_ATTR(hex_cmd, S_IWUSR, NULL, store_hex_cmd);
static DEVICE_ATTR(hex_data, S_IWUSR, NULL, store_hex_data);
static DEVICE_ATTR(cmd, S_IWUSR, NULL, store_cmd);
static DEVICE_ATTR(data, S_IWUSR, NULL, store_data);
static DEVICE_ATTR(backlight, S_IRUGO | S_IWUSR, get_backlight, set_backlight);

/*<8>*/
static struct miscdevice taihu_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,			/* dynamic minor please :) */
	.name = "lcds",
	.fops = &taihu_lcd_ops
};

static int __init taihu_lcd_init(void)
{
	struct resource *res_cmd_data;
	struct resource *res_lcd_data;
	struct resource *res_lcd_bckl;
	int status = -ENODEV;

/*<9>*/
	if ((res_cmd_data = request_mem_region(LCD_CMD_ADDR, 1L, "lcdcmd")) == NULL) {
		pr_err("An error occured while requesting mem_region for lcd_cmd_addr\n");
		goto err_0;	/*<10> */
	}
	if ((res_lcd_data = request_mem_region(LCD_DATA_ADDR, 1L, "lcddata")) == NULL) {
		pr_err("An error occured while requesting mem_region for lcd_data_addr\n");
		goto err_1;
	}
	if ((res_lcd_bckl = request_mem_region(LCD_BCKL_ADDR, 1L, "lcdbckl")) == NULL) {
		pr_err("An error occured while requesting mem_region for lcd_bckl_addr\n");
		goto err_2;
	}

/*<11>*/
	cmd_mmap = ioremap((resource_size_t)LCD_CMD_ADDR, 1);
	data_mmap = ioremap((resource_size_t)LCD_DATA_ADDR, 1);
	bckl_mmap = ioremap((resource_size_t)LCD_BCKL_ADDR, 1);
	iowrite8(CMD_CLEAR_DISPLAY, (void __iomem *)cmd_mmap);
	g_addr = CMD_SET_HOME;
	udelay(2000);

/*<12>*/
	misc_register(&taihu_miscdev);
	status = device_create_file(taihu_miscdev.this_device, &dev_attr_hex_cmd);
	status = device_create_file(taihu_miscdev.this_device, &dev_attr_hex_data);
	status = device_create_file(taihu_miscdev.this_device, &dev_attr_data);
	status = device_create_file(taihu_miscdev.this_device, &dev_attr_cmd);
	status = device_create_file(taihu_miscdev.this_device, &dev_attr_backlight);
	return 0;

/*<13>*/
err_2:
	pr_debug("cleanup - releasing mem_region for lcd_cmd_addr\n");
	release_mem_region(LCD_CMD_ADDR, 1L);
err_1:
	pr_debug("cleanup - releasing mem_region for lcd_data_addr\n");
	release_mem_region(LCD_DATA_ADDR, 1L);
err_0:
	pr_debug("Nothing left to clean up - Bailing out\n");
	return status;
}

static void __exit taihu_lcd_cleanup(void)
{
/*<14>*/
	device_remove_file(taihu_miscdev.this_device, &dev_attr_backlight);
	device_remove_file(taihu_miscdev.this_device, &dev_attr_cmd);
	device_remove_file(taihu_miscdev.this_device, &dev_attr_data);
	device_remove_file(taihu_miscdev.this_device, &dev_attr_hex_data);
	device_remove_file(taihu_miscdev.this_device, &dev_attr_hex_cmd);
	misc_deregister(&taihu_miscdev);
	release_mem_region(LCD_BCKL_ADDR, 1L);
	release_mem_region(LCD_DATA_ADDR, 1L);
	release_mem_region(LCD_CMD_ADDR, 1L);
}

MODULE_AUTHOR("Peter Huewe");
MODULE_LICENSE("GPL");
module_init(taihu_lcd_init);
module_exit(taihu_lcd_cleanup);

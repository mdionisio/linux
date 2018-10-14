#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <asm/io.h>

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/uaccess.h>

/*
 insmod lib/modules/4.18.11/kernel/drivers/mytest.ko
 cat /proc/iomem
 cat /dev/mytest ; echo
 echo "10" >/dev/mytest
 cat /dev/mytest ; echo
*/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michele Dionisio");
MODULE_DESCRIPTION("example of mytest qemu device");
MODULE_VERSION("0.01");

#define FSL_IMX6_MMDC_ADDR 0x10000000
#define MYTEST_ADDR (FSL_IMX6_MMDC_ADDR + 0x40000000)
#define MYTEST_ADDR_P (void *)(MYTEST_ADDR)


static int major = -1;
static struct cdev mycdev;
static struct class *myclass = NULL;
static struct resource *memresource = NULL;
static void *sim_device = NULL;

int mytest_open(struct inode *inode, struct file *filp);
int mytest_release(struct inode *inode, struct file *filp);
ssize_t mytest_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t mytest_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);

static const struct file_operations fops = {
    .open = mytest_open,
    .release = mytest_release,

    .read = mytest_read,
    .write = mytest_write,
};

int mytest_open(struct inode *inode, struct file *filp)
{
    /* Success */
    return 0;
}

int mytest_release(struct inode *inode, struct file *filp)
{
    /* Success */
    return 0;
}

ssize_t mytest_read(struct file *filp, char __user *buf,
                    size_t count, loff_t *f_pos)
{
    char temp[10];
    int len;
    if (*f_pos != 0) {
        return 0;
    } else {
        unsigned int data_read = ioread32(sim_device);
        snprintf(temp, sizeof(temp), "%u", data_read);
        len = strnlen(temp, sizeof(temp));
        if (count >= len) {
            if (copy_to_user(buf, temp, len) == 0) {
                *f_pos += len;
                return len;
            } else {
                return -EPERM;
            }
        } else {
            return -EPERM;
        }
    }
}

ssize_t mytest_write(struct file *filp, const char __user *buf,
                     size_t count, loff_t *f_pos)
{
    char temp[10];
    long result = 0;
    if (count < (sizeof(temp)-1)) {
        if (copy_from_user(temp, buf, count) == 0) {
            temp[count] = '\0';
            if (kstrtol(temp, 10, &result) == 0) {
                unsigned int data_write = (unsigned int)result;
                iowrite32(data_write, sim_device);
                return count;
            } else {
                return -EPERM;
            }
        } else {
            return -EPERM;
        }
    } else {
        return 0;
    }
}

static int __init mytest_init(void)
{
    printk(KERN_INFO "my test init!");

    memresource = request_mem_region(MYTEST_ADDR, sizeof(uint32_t), "mytest");
    if (memresource == NULL) {
        return -1;
    }

    sim_device = ioremap_nocache(MYTEST_ADDR, sizeof(uint32_t));
    if (sim_device == NULL) {
        release_mem_region(MYTEST_ADDR, sizeof(uint32_t));
        memresource = NULL;
        return -1;
    }

    if (alloc_chrdev_region(&major, 0, 1, "mytest_dev") < 0) //$cat /proc/devices
    {
        iounmap(sim_device);
        sim_device = NULL;
        release_mem_region(MYTEST_ADDR, sizeof(uint32_t));
        memresource = NULL;
        return -1;
    }
    if ((myclass = class_create(THIS_MODULE, "mytestdrv")) == NULL) //$ls /sys/class
    {
        iounmap(sim_device);
        sim_device = NULL;
        release_mem_region(MYTEST_ADDR, sizeof(uint32_t));
        memresource = NULL;
        unregister_chrdev_region(major, 1);
        major = -1;
        return -1;
    }
    if (device_create(myclass, NULL, major, NULL, "mytest") == NULL) //$ls /dev/
    {
        iounmap(sim_device);
        sim_device = NULL;
        release_mem_region(MYTEST_ADDR, sizeof(uint32_t));
        memresource = NULL;
        class_destroy(myclass);
        myclass = NULL;
        unregister_chrdev_region(major, 1);
        major = -1;
        return -1;
    }
    cdev_init(&mycdev, &fops);
    if (cdev_add(&mycdev, major, 1) == -1)
    {
        iounmap(sim_device);
        sim_device = NULL;
        release_mem_region(MYTEST_ADDR, sizeof(uint32_t));
        memresource = NULL;
        device_destroy(myclass, major);
        class_destroy(myclass);
        myclass = NULL;
        unregister_chrdev_region(major, 1);
        major = -1;
        return -1;
    }
    return 0;
}

static void __exit mytest_exit(void)
{
    printk(KERN_INFO "my test exit!\n");
    if (sim_device != NULL) {
        iounmap(sim_device);
        sim_device = NULL;
    }
    if (memresource != NULL) {
        release_mem_region(MYTEST_ADDR, sizeof(uint32_t));
        memresource = NULL;
    }
    device_destroy(myclass, major);
    cdev_del(&mycdev);
    if (myclass)
    {
        class_destroy(myclass);
        myclass = NULL;
    }
    if (major != -1)
    {
        unregister_chrdev_region(major, 1);
        major = -1;
    }
}

module_init(mytest_init);
module_exit(mytest_exit);
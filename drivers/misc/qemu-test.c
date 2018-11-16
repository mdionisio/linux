#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>

/* We'll try to allocate a device major number and store it here */
static int major;

static const struct file_operations qemudev_fops = {
	.owner		= THIS_MODULE,
};

static struct class *qemudev_class;

static int __init qemudev_init(void)
{
	int status;

	/* Let the kernel assign us a major number, and use it
	 * to register our character device */
	major = register_chrdev(0, "qemu-test", &qemudev_fops);
	if (major < 0)
		return major;

	/* Register a class that will allow udev/mdev to automatically
	 * manage our driver's nodes under /dev */
	qemudev_class = class_create(THIS_MODULE, "qemu-test");
	if (IS_ERR(qemudev_class)) {
		status = PTR_ERR(qemudev_class);
		goto err_class_create;
	}

	printk(KERN_INFO "qemu-test: driver loaded\n");
	return 0;

err_class_create:
	unregister_chrdev(major, "qemu-test");
	return status;
}
module_init(qemudev_init);

static void __exit qemudev_exit(void)
{
	/* Unregister the major and device class */
	class_destroy(qemudev_class);
	unregister_chrdev(major, "qemu-test");
}
module_exit(qemudev_exit);

MODULE_AUTHOR("Michele Dionisio, Pietro Lorefice");
MODULE_DESCRIPTION("Device driver for QEMU test device");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");


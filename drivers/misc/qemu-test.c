#include <linux/init.h>
#include <linux/module.h>

static int __init qemudev_init(void)
{
	/* TODO: implement me! */
	return 0;
}
module_init(qemudev_init);

static void __exit qemudev_exit(void)
{
	/* TODO: implement me! */
}
module_exit(qemudev_exit);

MODULE_AUTHOR("Michele Dionisio, Pietro Lorefice");
MODULE_DESCRIPTION("Device driver for QEMU test device");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");


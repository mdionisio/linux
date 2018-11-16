#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>

/* We'll try to allocate a device major number and store it here */
static int major;

static const struct file_operations qemudev_fops = {
	.owner		= THIS_MODULE,
};

static struct class *qemudev_class;

/*
 * The *_probe function is called every time the kernel finds an instance of
 * this device on the system. This can happen in several ways: device tree
 * instantiation, machine file, auto-discovery, hot-plugging and so on.
 */
static int qemudev_probe(struct platform_device *pdev)
{
	/* TODO: implement me! */
	dev_info(&pdev->dev, "successfully probed!\n");
	return 0;
}

/*
 * The *_probe dual function, invoked every time a device is removed from
 * the system.
 */
static int qemudev_remove(struct platform_device *pdev)
{
	/* TODO: implement me! */
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id qemudev_dt_ids[] = {
	{ .compatible = "linuxlab,qemu-test" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, qemudev_dt_ids);
#endif

static struct platform_driver qemudev_driver = {
	.driver		= {
		.name		= "qemu-test",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(qemudev_dt_ids),
	},
	.probe		= qemudev_probe,
	.remove		= qemudev_remove,
};

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

	/* Finally, register a platform driver that will manage these devices */
	status = platform_driver_register(&qemudev_driver);
	if (status < 0)
		goto err_pdev_reg;

	printk(KERN_INFO "qemu-test: driver loaded\n");
	return 0;

err_pdev_reg:
	class_destroy(qemudev_class);
err_class_create:
	unregister_chrdev(major, "qemu-test");
	return status;
}
module_init(qemudev_init);

static void __exit qemudev_exit(void)
{
	/* Unregister platform driver first */
	platform_driver_unregister(&qemudev_driver);

	/* We can then unregister the major and device class */
	class_destroy(qemudev_class);
	unregister_chrdev(major, "qemu-test");
}
module_exit(qemudev_exit);

MODULE_AUTHOR("Michele Dionisio, Pietro Lorefice");
MODULE_DESCRIPTION("Device driver for QEMU test device");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");


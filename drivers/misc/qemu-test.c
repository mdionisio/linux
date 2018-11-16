#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <asm/io.h>

/* We'll try to allocate a device major number and store it here */
static int major;

/* One minor for each instance of the device */
#define N_QEMUDEV_MINORS	32 /* up to 256 */
static DECLARE_BITMAP(minors, N_QEMUDEV_MINORS);

/* A specific instance of our physical device */
struct qemu_device {
	struct list_head	list_entry; /* List handle for this element     */
	struct platform_device	*pdev;      /* The platform device we belong to */
	void __iomem		*regs;      /* Memory-mapped device registers   */
	dev_t			devt;       /* Our device instance identifier   */
	int			irq;        /* Interrupt number                 */
};

static LIST_HEAD(qemudev_list);
static DEFINE_SPINLOCK(qemudev_list_lock);

/* Utility functions to access the peripheral's registers */
#define qemu_reg_write(base, value)	__raw_writel((value), base)
#define qemu_reg_read(base)		__raw_readl(base)

/*
 * Serve interrupts coming from the device.
 * When an interrupt happens, reset the counter value to 0.
 */
static irqreturn_t qemudev_irq_handler(int irq, void *dev_id)
{
	struct qemu_device *qemudev = dev_id;
	struct device *dev = &qemudev->pdev->dev;

	qemu_reg_write(qemudev->regs, 0);

	dev_info(dev, "IRQ handled!\n");
	return IRQ_HANDLED;
}

/*
 * Reading from the device file triggers a read from the device's data register.
 * This value is then formatted into a human-readable string and copied to
 * userspace.
 */
static ssize_t qemudev_read(struct file *filp, char __user *buf,
			    size_t count, loff_t *f_pos)
{
	struct qemu_device *qemudev = filp->private_data;
	uint32_t val = qemu_reg_read(qemudev->regs);
	char str[12];
	ssize_t n;

	/* For simplicity's sake */
	if (*f_pos)
		return 0;

	n = snprintf(str, sizeof(str), "%u\n", val);
	n = n - copy_to_user(buf, str, n);
	*f_pos += n;

	return n;
}

/*
 * Writing to the device file triggers a write to the device's data register.
 * We must first convert the human-readable string coming from userspace into
 * its binary representation.
 */
static ssize_t qemudev_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *f_pos)
{
	struct qemu_device *qemudev = filp->private_data;
	uint32_t val;
	int ret;

	ret = kstrtouint_from_user(buf, count, 0, &val);
	if (ret)
		return ret;

	qemu_reg_write(qemudev->regs, val);
	return count;
}

/*
 * When opening a device file, we need to find the qemu_device associated with
 * that file. This is usually done by looking at the minor in this case.
 * A reference to the found qemu_device is then stored in the private_data
 * field of the file structure.
 */
static int qemudev_open(struct inode *inode, struct file *filp)
{
	struct qemu_device *qemudev;
	unsigned minor = iminor(inode);

	/* Traverse the instanced devices to find the one matching the
	 * minor of the requested inode */
	spin_lock(&qemudev_list_lock);
	list_for_each_entry(qemudev, &qemudev_list, list_entry) {
		if (MINOR(qemudev->devt) == minor)
			goto found;
	}
	qemudev = NULL;

found:
	spin_unlock(&qemudev_list_lock);
	if (!qemudev)
		return -ENODEV;

	filp->private_data = qemudev;
	return 0;
}

/*
 * Nothing to do, just unbind the private_data field.
 */
static int qemudev_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static const struct file_operations qemudev_fops = {
	.owner		= THIS_MODULE,
	.open		= qemudev_open,
	.release	= qemudev_release,
	.read		= qemudev_read,
	.write		= qemudev_write,
	.llseek		= no_llseek,
};

static struct class *qemudev_class;

/*
 * The *_probe function is called every time the kernel finds an instance of
 * this device on the system. This can happen in several ways: device tree
 * instantiation, machine file, auto-discovery, hot-plugging and so on.
 */
static int qemudev_probe(struct platform_device *pdev)
{
	struct qemu_device *qemudev;
	struct resource *regs;
	struct device *dev;
	unsigned long minor;
	int ret;

	/* Allocate our device's data structure using managed allocation */
	qemudev = devm_kzalloc(&pdev->dev, sizeof(struct qemu_device), GFP_KERNEL);
	if (!qemudev) {
		dev_err(&pdev->dev, "out of memory\n");
		return -ENOMEM;
	}
	qemudev->pdev = pdev;

	/* Retrieve and remap the MMIO region associated to this device.
	 * In modern kernels for ARM platforms, this is usually retrieved
	 * from a device tree entry.
	 */
	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	qemudev->regs = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(qemudev->regs))
		return PTR_ERR(qemudev->regs);

	/* Retrieve interrupt number and register interrupt handler */
	qemudev->irq = platform_get_irq(pdev, 0);
	if (!qemudev->irq) {
		dev_err(&pdev->dev, "could not get irq\n");
		return -ENXIO;
	}

	ret = devm_request_irq(&pdev->dev, qemudev->irq, qemudev_irq_handler,
			       IRQF_SHARED, pdev->name, qemudev);
	if (ret) {
		dev_err(&pdev->dev, "could not register IRQ handler\n");
		return ret;
	}

	/* Create the associated device entry in /dev */
	spin_lock(&qemudev_list_lock);
	{
		/* Find a free minor to use for this device.
		 * Reusing minors is fine as long as udev is working.
		 */
		minor = find_first_zero_bit(minors, N_QEMUDEV_MINORS);

		if (minor < N_QEMUDEV_MINORS) {
			qemudev->devt = MKDEV(major, minor);
			dev = device_create(qemudev_class, NULL, qemudev->devt,
					    qemudev, "qemu-test-%ld", minor);
			ret = PTR_ERR_OR_ZERO(dev);
		} else {
			dev_warn(&pdev->dev, "no minor available!\n");
			ret = -ENODEV;
		}

		/* If everything went fine, mark the minor as used
		 * and push our new device into the list. */
		if (!ret) {
			set_bit(minor, minors);
			list_add(&qemudev->list_entry, &qemudev_list);

			platform_set_drvdata(pdev, qemudev);

			dev_info(&pdev->dev, "successfully probed!\n");
		}
	}
	spin_unlock(&qemudev_list_lock);

	return ret;
}

/*
 * The *_probe dual function, invoked every time a device is removed from
 * the system.
 */
static int qemudev_remove(struct platform_device *pdev)
{
	struct qemu_device *qemudev = platform_get_drvdata(pdev);

	/* Remove this specific device from the list, and destroy
	 * the /dev entry associated with its minor */
	spin_lock(&qemudev_list_lock);
	{
		list_del(&qemudev->list_entry);
		clear_bit(MINOR(qemudev->devt), minors);
		device_destroy(qemudev_class, qemudev->devt);
	}
	spin_unlock(&qemudev_list_lock);

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


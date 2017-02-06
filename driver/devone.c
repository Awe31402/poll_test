#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/current.h>
#include <linux/proc_fs.h>

MODULE_LICENSE("Dual BSD/GPL");

static unsigned int timeout_val = 20;

static int devone_devs = 1;
static int devone_major = 0;
static int devone_minor = 0;
static struct cdev devone_cdev;
static struct class* devone_class = NULL;
static dev_t devone_dev;
static struct task_struct* waiting_proc = NULL;

struct devone_data {
    struct timer_list timeout;
    spinlock_t lock;
    wait_queue_head_t read_wait;
    int timeout_done;
    struct semaphore sem;
};

static void devone_timeout(unsigned long arg)
{
    struct devone_data* dev = (struct devone_data*) arg;
    unsigned long flags;

    printk("%s called\n", __func__);

    spin_lock_irqsave(&dev->lock, flags);
    dev->timeout_done = 1;
    wake_up_interruptible(&dev->read_wait);
    spin_unlock_irqrestore(&dev->lock, flags);
}

static unsigned int devone_poll(struct file* file, poll_table* wait)
{
    struct devone_data* dev = file->private_data;
    unsigned int mask = POLLOUT | POLLWRNORM;

    printk(KERN_INFO "%s is called\n", __func__);

    if (!dev)
        return -EBADFD;

    down(&dev->sem);
    waiting_proc = current;
    poll_wait(file, &dev->read_wait, wait);
    if (dev->timeout_done == 1) {
        mask |= POLLIN | POLLRDNORM;
        waiting_proc = NULL;
    }
    up(&dev->sem);

    return mask;
}

static ssize_t devone_write(struct file* file, const char __user *buf,
        size_t count, loff_t *f_pos)
{
    return -EFAULT;
}

static ssize_t devone_read(struct file* file, char __user *buf,
        size_t count, loff_t *f_pos)
{
    struct devone_data *dev = file->private_data;
    int i;
    unsigned char val;
    int retval;

    printk(KERN_INFO "%s is called\n", __func__);

    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    if (dev->timeout_done == 0) { /*device is not readable*/
        up(&dev->sem);
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        do {
            retval = wait_event_interruptible_timeout(
                    dev->read_wait,
                    dev->timeout_done == 1,
                    1 * HZ);
            if (retval == -ERESTARTSYS)
                return -ERESTARTSYS;
        } while (retval == 0);

        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
    }
    val = 0xff;

    for (i = 0; i < count; i++)
        if (copy_to_user(&buf[i], &val, sizeof(val))) {
            retval = -EFAULT;
            goto out;
        }
    retval = count;
out:
    dev->timeout_done = 0;
    mod_timer(&dev->timeout, jiffies + timeout_val * HZ);
    up(&dev->sem);
    return retval;
}

static int devone_open(struct inode* inode, struct file *file)
{
    struct devone_data *dev;
    dev = kmalloc(sizeof(struct devone_data), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;
    spin_lock_init(&dev->lock);
    init_waitqueue_head(&dev->read_wait);
    sema_init(&dev->sem, 1);

    init_timer(&dev->timeout);
    dev->timeout.function = devone_timeout;
    dev->timeout.data = (unsigned long) dev;

    file->private_data = dev;
    dev->timeout_done = 0;

    mod_timer(&dev->timeout, jiffies + timeout_val * HZ);
    return 0;
}

static int devone_close(struct inode* inode, struct file* file)
{
    struct devone_data* dev;
    dev = file->private_data;

    if (dev) {
        del_timer_sync(&dev->timeout);
        kfree(dev);
    }

    return 0;
}

static struct file_operations devone_fops = {
    .owner = THIS_MODULE,
    .open = devone_open,
    .release = devone_close,
    .read = devone_read,
    .write = devone_write,
    .poll = devone_poll,
};

static int waiting_proc_read(struct seq_file* m, void *v)
{
    seq_printf(m, "%s\n",
            (waiting_proc)?
            (READ_ONCE(waiting_proc->state))? "sleeping" : "running"
            : "no waiting task");
    return 0;
}

static int waiting_proc_open(struct inode *inode, struct file* file)
{
    return single_open(file, waiting_proc_read, NULL);
}

static const struct file_operations waiting_proc_fops = {
    .owner = THIS_MODULE,
    .open = waiting_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static int __init devone_init(void)
{
    dev_t dev = MKDEV(devone_major, 0);
    int alloc_ret = 0;
    int major;
    int cdev_err = 0;
    struct device* class_dev = NULL;
    struct proc_dir_entry* entry;

    alloc_ret = alloc_chrdev_region(&dev, 0, devone_devs, "devone");
    if (alloc_ret)
        goto error;

    devone_major = major = MAJOR(dev);

    cdev_init(&devone_cdev, &devone_fops);
    devone_cdev.owner = THIS_MODULE;
    devone_cdev.ops = &devone_fops;
    cdev_err = cdev_add(&devone_cdev, MKDEV(devone_major, devone_minor), 1);

    if (cdev_err)
        goto error;

    devone_class = class_create(THIS_MODULE, "devone");
    if (IS_ERR(devone_class))
        goto error;
    devone_dev = MKDEV(devone_major, devone_minor);
    class_dev = device_create(devone_class,
            NULL,
            devone_dev,
            NULL,
            "devone%d",
            devone_minor);

    entry = proc_create("waiting_proc", S_IRUGO, NULL,
            &waiting_proc_fops);
    if (!entry) {
        printk(KERN_INFO "unable to create proc entry");
        goto error;
    }

    printk(KERN_INFO "devone driver (major %d) is installed\n", devone_major);

    return 0;
error:
    if (cdev_err == 0)
        cdev_del(&devone_cdev);
    if (alloc_ret == 0)
        unregister_chrdev_region(dev, devone_devs);
    return -1;
}

static __exit void devone_exit(void)
{
    dev_t dev = MKDEV(devone_major, 0);

    device_destroy(devone_class, devone_dev);
    class_destroy(devone_class);

    cdev_del(&devone_cdev);
    unregister_chrdev_region(dev, devone_devs);
    proc_remove_entry("waiting_proc", NULL);
    printk(KERN_INFO "devone removed\n");
}

module_init(devone_init);
module_exit(devone_exit);

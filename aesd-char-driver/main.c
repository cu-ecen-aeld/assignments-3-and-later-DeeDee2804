/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/slab.h> // for kmalloc, kfree, krealloc
#include "aesdchar.h"
#include "aesd-circular-buffer.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("DeeDee2804"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;
/* FUNCTION PROTOTYPES */
void aesd_cleanup_module(void);
int aesd_init_module(void);
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos);

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;   /* for other methods */
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_buffer_entry *entry;
    ssize_t retval = 0;
    ssize_t entry_offset = 0;
    struct aesd_dev *dev = filp->private_data;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(dev->buffer, *f_pos, &entry_offset);
    if (entry == NULL) {
        PDEBUG("No entry found for offset %lld", *f_pos);
        retval = 0; // No data to read
        goto out;
    } else if (entry_offset + count > entry->size) {
        PDEBUG("Requested read exceeds entry size, adjusting count");
        count = entry->size - entry_offset; // Adjust count to fit within the entry
    } else {
        PDEBUG("Read %zu bytes from entry at offset %zu", count, entry_offset);
    }


    // Copy the data from the entry to user space
    if (copy_to_user(buf, entry->buffptr + entry_offset, count)) {
        PDEBUG("copy_to_user failed");
        retval = -EFAULT;
        goto out;
    }
    // Update the file position
    PDEBUG("copy_to_user success");
    *f_pos += count; 
    retval = count; // Return the number of bytes read

  out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = filp->private_data;
    char *kbuf;
    char *new_buff;
    const char *replaced_entry; 
    
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    if (mutex_lock_interruptible(&dev->lock)) {
        PDEBUG("mutex lock interrupted");
        return -ERESTARTSYS;
    }
    // Copy the user buffer to kernel space
    kbuf = kmalloc(count+1, GFP_KERNEL);
    if (copy_from_user(kbuf, buf, count)) {
        PDEBUG("copy_from_user failed");
        mutex_unlock(&dev->lock);
        kfree(kbuf);
        return -EFAULT;
    }
    // Null-terminate the string
    kbuf[count] = '\0'; 

    // Append the kbuffer to the current entry
    if (dev->current_entry->buffptr) {
        // If the current entry already has a buffer, we need to reallocate it
        new_buff = krealloc((void *)dev->current_entry->buffptr,
                                  dev->current_entry->size + count + 1, GFP_KERNEL);
        if (!new_buff) {
            PDEBUG("krealloc failed");
            kfree(kbuf);
            mutex_unlock(&dev->lock);
            return -ENOMEM;
        }
        dev->current_entry->buffptr = new_buff;
        // Copy the new data into the reallocated buffer, included the null terminator
        memcpy(dev->current_entry->buffptr + dev->current_entry->size, kbuf, count+1);
        dev->current_entry->size += count;
    } else {
        // Create new buffer and copy data
        dev->current_entry->buffptr = kmalloc(count+1, GFP_KERNEL);
        if (!dev->current_entry->buffptr) {
            PDEBUG("kmalloc failed for current_entry");
            kfree(kbuf);
            mutex_unlock(&dev->lock);
            return -ENOMEM;
        }
        memcpy(dev->current_entry->buffptr, kbuf, count+1);
        dev->current_entry->size = count;
    }

    // Check if the user buffer terminated by \n
    if (kbuf[count - 1] == '\n') {
        PDEBUG("The write command is ended with newline");
        PDEBUG("Writing data: %s\n", kbuf);
        // If it does, we can add the entry to the circular buffer
        replaced_entry = aesd_circular_buffer_add_entry(dev->buffer, dev->current_entry);
        if (replaced_entry) {
            PDEBUG("Freeing data: %s\n", replaced_entry);
            // If the buffer was full, we need to free the old entry
            kfree(replaced_entry);
        }
        // Reset the current entry
        dev->current_entry->buffptr = NULL;
        dev->current_entry->size = 0;
        // Return the number of bytes written
        retval = count; 
    } else {
        // No write operation is performed until a newline is received
        PDEBUG("No newline received, waiting for next write operation");
        retval = count;
    }

    
    mutex_unlock(&dev->lock);
    kfree(kbuf);
    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    // Allocate the memory for the buffer
    aesd_device.buffer = kmalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
    if (!aesd_device.buffer) {
        result = -ENOMEM;
        goto fail;
    }
    aesd_circular_buffer_init(aesd_device.buffer);

    // Allocate the memory for current entry
    aesd_device.current_entry = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
    if (!aesd_device.current_entry) {
        result = -ENOMEM;
        goto fail;
    }
    memset(aesd_device.current_entry, 0, sizeof(struct aesd_buffer_entry));

    // Initialize the mutex
    mutex_init(&aesd_device.lock);

    // Setup the character device
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

  fail:
    aesd_cleanup_module();
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    if (aesd_device.current_entry && aesd_device.current_entry->buffptr) {
        kfree(aesd_device.current_entry->buffptr);
    }
    kfree(aesd_device.buffer);
    kfree(aesd_device.current_entry);
    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

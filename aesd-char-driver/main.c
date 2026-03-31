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

#include "aesdchar.h"
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/types.h>
int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Your Name Here"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp) {
  struct aesd_dev *dev;
  PDEBUG("open");
  /**
   * TODO: handle open
   */
  dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
  filp->private_data = dev;

  return 0;
}

int aesd_release(struct inode *inode, struct file *filp) {
  PDEBUG("release");
  /**
   * TODO: handle release
   */
  return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos) {
  ssize_t retval = 0;
  struct aesd_dev *dev = filp->private_data;
  struct aesd_buffer_entry *entry;
  size_t entry_offset;
  size_t bytes_to_copy;

  PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

  // Lock mutex before touching shared data
  if (mutex_lock_interruptible(&dev->lock)) {
    return -ERESTARTSYS;
  }

  // Step 1: Find which circular buffer entry f_pos falls in
  // entry_offset tells us the byte offset WITHIN that entry
  entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos,
                                                          &entry_offset);

  // Step 2: If NULL, we've read everything - return EOF
  if (entry == NULL) {
    retval = 0;
    goto cleanup;
  }

  // Step 3: Calculate how many bytes we can copy
  // Can't copy more than what's left in this entry
  // Can't copy more than what userspace asked for
  bytes_to_copy = min(count, entry->size - entry_offset);

  // Step 4: Copy from kernel buffer entry into userspace buf
  // entry->buffptr + entry_offset = start of data to copy
  if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_copy)) {
    retval = -EFAULT;
    goto cleanup;
  }

  // Step 5: Advance f_pos by how many bytes we copied
  // This is how next read knows where to start
  *f_pos += bytes_to_copy;
  retval = bytes_to_copy;

cleanup:
  mutex_unlock(&dev->lock);
  return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos) {
  ssize_t retval = -ENOMEM;
  struct aesd_dev *dev = filp->private_data;
  const char *newline_pos;
  const char *evicted_entry;
  char *new_buf;
  PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
  /**
   * TODO: handle write
   */
  // Lock mutex before touching shared data
  if (mutex_lock_interruptible(&dev->lock)) {
    return -ERESTARTSYS;
  }

  // make entry_in_progress buffer hold new data
  new_buf = krealloc(dev->entry_in_progress.buffptr,
                     dev->entry_in_progress.size + count, GFP_KERNEL);

  // if allocation fails
  if (new_buf == NULL) {
    mutex_unlock(&dev->lock);
    return -ENOMEM;
  }
  // Copy data from userspace to kernel
  if (copy_from_user(new_buf + dev->entry_in_progress.size, buf, count)) {
    mutex_unlock(&dev->lock);
    return -EFAULT;
  }

  // Update entry_in_progress with new buffer and size
  dev->entry_in_progress.buffptr = new_buf;
  dev->entry_in_progress.size += count;

  // Check
  newline_pos =
      memchr(dev->entry_in_progress.buffptr, '\n', dev->entry_in_progress.size);

  if (newline_pos != NULL) {
    // We have a complete command - push to circular buffer
    evicted_entry =
        aesd_circular_buffer_add_entry(&dev->buffer, &dev->entry_in_progress);

    // If an old entry was evicted, free its memory
    if (evicted_entry != NULL) {
      kfree(evicted_entry);
    }

    // Reset entry_in_progress - command is now in circular buffer
    dev->entry_in_progress.buffptr = NULL;
    dev->entry_in_progress.size = 0;
  }

  // Tell userspace we consumed all the bytes
  retval = count;

  mutex_unlock(&dev->lock);

  return retval;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev) {
  int err, devno = MKDEV(aesd_major, aesd_minor);

  cdev_init(&dev->cdev, &aesd_fops);
  dev->cdev.owner = THIS_MODULE;
  dev->cdev.ops = &aesd_fops;
  err = cdev_add(&dev->cdev, devno, 1);
  if (err) {
    printk(KERN_ERR "Error %d adding aesd cdev", err);
  }
  return err;
}

int aesd_init_module(void) {
  dev_t dev = 0;
  int result;
  result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
  aesd_major = MAJOR(dev);
  if (result < 0) {
    printk(KERN_WARNING "Can't get major %d\n", aesd_major);
    return result;
  }
  memset(&aesd_device, 0, sizeof(struct aesd_dev));

  /**
   * TODO: initialize the AESD specific portion of the device
   */
  mutex_init(&aesd_device.lock);

  aesd_circular_buffer_init(&aesd_device.buffer);
  result = aesd_setup_cdev(&aesd_device);

  if (result) {
    unregister_chrdev_region(dev, 1);
  }
  return result;
}

void aesd_cleanup_module(void) {
  uint8_t index;

  struct aesd_buffer_entry *entry;
  dev_t devno = MKDEV(aesd_major, aesd_minor);

  cdev_del(&aesd_device.cdev);
  // Free any incomplete write that was in progress
  if (aesd_device.entry_in_progress.buffptr != NULL) {
    kfree(aesd_device.entry_in_progress.buffptr);
  }

  /**
   * TODO: cleanup AESD specific poritions here as necessary
   */
  AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
    if (entry->buffptr != NULL) {
      kfree(entry->buffptr);
    }
  }

  unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

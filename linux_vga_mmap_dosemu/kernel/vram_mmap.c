// vram_mmap.c
// Simple kernel module exposing physical VGA text-mode memory (default 0xB8000) via /dev/vram
// Build with the provided Makefile.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/io.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Assistant");
MODULE_DESCRIPTION("Expose VGA text-mode VRAM via /dev/vram (mmap to physical memory).");

static unsigned long phys_addr = 0xb8000;
module_param(phys_addr, ulong, 0444);
MODULE_PARM_DESC(phys_addr, "Physical address of VRAM (default 0xB8000)");

static unsigned long vsize = 0x4000; // 16KiB default (text mode)
module_param(vsize, ulong, 0444);
MODULE_PARM_DESC(vsize, "Size of VRAM region (default 0x4000)");

static dev_t devt;
static struct cdev vram_cdev;
static struct class *vram_class;

static int vram_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int vram_release(struct inode *inode, struct file *file)
{
    return 0;
}

static int vram_mmap(struct file *file, struct vm_area_struct *vma)
{
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    unsigned long phys_start = phys_addr + offset;
    unsigned long len = vma->vm_end - vma->vm_start;
    unsigned long pfn_start;

    if (offset + len > vsize) {
        pr_warn("vram_mmap: requested mapping exceeds region (off %lu len %lu vsize %lu)\n",
                offset, len, vsize);
        return -EINVAL;
    }

    /* Non-cached mapping for device memory is usually desirable */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#else
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#endif

    pfn_start = phys_start >> PAGE_SHIFT;

    if (remap_pfn_range(vma, vma->vm_start, pfn_start, len, vma->vm_page_prot)) {
        pr_err("vram_mmap: remap_pfn_range failed\n");
        return -EAGAIN;
    }

    return 0;
}

static const struct file_operations vram_fops = {
    .owner = THIS_MODULE,
    .open = vram_open,
    .release = vram_release,
    .mmap = vram_mmap,
};

static int __init vram_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&devt, 0, 1, "vram");
    if (ret) {
        pr_err("vram: alloc_chrdev_region failed: %d\n", ret);
        return ret;
    }

    cdev_init(&vram_cdev, &vram_fops);
    vram_cdev.owner = THIS_MODULE;

    ret = cdev_add(&vram_cdev, devt, 1);
    if (ret) {
        pr_err("vram: cdev_add failed: %d\n", ret);
        unregister_chrdev_region(devt, 1);
        return ret;
    }

    vram_class = class_create(THIS_MODULE, "vramclass");
    if (IS_ERR(vram_class)) {
        pr_err("vram: class_create failed\n");
        cdev_del(&vram_cdev);
        unregister_chrdev_region(devt, 1);
        return PTR_ERR(vram_class);
    }

    if (IS_ERR(device_create(vram_class, NULL, devt, NULL, "vram"))) {
        pr_err("vram: device_create failed\n");
        class_destroy(vram_class);
        cdev_del(&vram_cdev);
        unregister_chrdev_region(devt, 1);
        return -ENOMEM;
    }

    pr_info("vram: module loaded. /dev/vram created. phys=0x%lx size=0x%lx\n", phys_addr, vsize);
    return 0;
}

static void __exit vram_exit(void)
{
    device_destroy(vram_class, devt);
    class_destroy(vram_class);
    cdev_del(&vram_cdev);
    unregister_chrdev_region(devt, 1);
    pr_info("vram: module unloaded\n");
}

module_init(vram_init);
module_exit(vram_exit);


#include <linux/init.h>
#include <linux/module.h> /* Module macros */
#include <linux/fs.h>     /* allocate and register region, file structure definition */
#include <linux/kernel.h> /* printk(), container_of etc */
#include <linux/slab.h>   /* kmalloc/kfree */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* u8, i16 and other types */
#include <linux/cdev.h>   /* char reg: latest kernel */
#include <linux/ioport.h> /* request_mem_region */
#include <linux/io.h>  	  /* clrbits32, setbits32 */
#include <linux/mm.h>
#include <linux/kdev_t.h>

#include <asm/uaccess.h>  /* copy_from_user */
#include <asm/io.h>	  /* ioremap, iounmap */ 
#include <asm/page.h>
#include <asm/sections.h>



/*
* Include definitions for rdev hardware registers, types and more	
 */
#include "roach_common.h"        /* local definitions */
#include "ppc4xx_gpio.c"

static int roach_major = 0;
static int roach_minor = 0;
unsigned int gateware_bytes;
unsigned int fpga;

#define ROACH_NAME   "roach2"
#define ROACH_NR_DEVS 2

/**
* Data type for mapping of ROACH2 Board memory space
* The structure holds pointers for the GPIO, SMAP and FPGA mappings
 */

typedef struct roach_map{
  unsigned long long smap_phy;
  void *smap_virt;
  unsigned long long fpga_phy;
  void *fpga_virt;
  void *tx_buf;
  void *rx_buf;
}rmap_t;

/**
*  Device structure for this device, on top of a character device
 */
typedef struct roach{
  rmap_t rmap;
  struct cdev roach_devs[ROACH_NR_DEVS];
  struct ppc4xx_gpio_chip *gc;
}roach_t;

static roach_t *rdev = NULL;

#define MAX_ROACH_DEV 2

/*
* Common roach VMA ops.
 */

void roach_vma_open(struct vm_area_struct *vma)
{
  printk(KERN_NOTICE "roach VMA open, virt %lx, phys %lx\n",
      vma->vm_start, vma->vm_pgoff << PAGE_SHIFT);
}

void roach_vma_close(struct vm_area_struct *vma)
{
  printk(KERN_NOTICE "roach VMA close.\n");
}
#if 0
static int roach_vma_sync(struct vm_area_struct *vma, unsigned long offset, size_t size, unsigned int flags)
{
  printk(KERN_NOTICE "%s: offset: %lu size: %d flags:%d\n", __func__, offset, size, flags);
  

  return 0;
}
#endif

static struct vm_operations_struct roach_remap_vm_ops = {
  .open = roach_vma_open,
  .close = roach_vma_close,
//  .sync = roach_vma_sync,
};

static int roach_mem_mmap(struct file *file,
    struct vm_area_struct *vma)
{
  unsigned long start, vsize, offset, addr;
  unsigned long page, pos, dummy_pos;

  /* VMA properties */
  start = vma->vm_start;
  vsize = vma->vm_end - vma->vm_start;
  offset = vma->vm_pgoff << PAGE_SHIFT;

  printk(KERN_NOTICE "%s: vm_start %lx, vm_end %lx, vsize %lx, offset %lx\n",
      __func__, vma->vm_start, vma->vm_end, (vma->vm_end - vma->vm_start), vma->vm_pgoff << PAGE_SHIFT);

  if (offset + vsize > ROACH_FPGA_LENGTH) {
    return -EINVAL;
  }

  pos = (unsigned long)(rdev->rmap.fpga_virt) + offset;
  printk(KERN_NOTICE "%s: pos to be converted : %lx\n", __func__, pos);


  vma->vm_flags |= VM_RESERVED;   /* avoid to swap out this VMA */


  /*NOTE: This is the sole change i made to make sure the mmaped write work
	  and renamed size variable to vsize*/

  vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

  while (vsize > 0) {
	  page = vmalloc_to_pfn((void *)pos);
	  if (remap_pfn_range(vma, start, page, PAGE_SIZE, vma->vm_page_prot)) {
		  return -EAGAIN;
	  }
	  start += PAGE_SIZE;
	  pos += PAGE_SIZE;
	  if (vsize > PAGE_SIZE)
		  vsize -= PAGE_SIZE;
	  else
		  vsize = 0;
  }

  vma->vm_ops = &roach_remap_vm_ops;
  roach_vma_open(vma);

  return 0;

}

int roach_mem_open(struct inode *inode, struct file *filp)
{
  printk(KERN_NOTICE "rdev mem open called\n");
  return 0;
}

ssize_t roach_mem_read(struct file *filp, char __user *buf, size_t cnt, loff_t *f_pos)
{
  unsigned long i;
  unsigned long start;

  printk(KERN_NOTICE "rdev mem read called with cnt val=%u\n", cnt);
  if(*f_pos < ROACH_FPGA_LENGTH){
    /* Writn to FPGA*/
    start = *f_pos;
    if(start >= ROACH_FPGA_LENGTH){
      cnt = 0;
    }
    else if(start + cnt > ROACH_FPGA_LENGTH){
      cnt = ROACH_FPGA_LENGTH - start;
    }

    for(i = 0; i < cnt; i+=4){
      __put_user(in_be32(rdev->rmap.fpga_virt + i + start), (uint32_t *)(buf + i));
    }
  }

  *f_pos += cnt;
  printk(KERN_INFO "Reading %lld data from fpga \n", *f_pos);
  return cnt;
}

ssize_t roach_mem_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *f_pos)
{
  unsigned long i;
  unsigned long start;
  unsigned int tmpS;
  int retval = -EIO;
  uint32_t word_count;
  volatile uint32_t *src;
  size_t have_written, will_write;


  have_written = 0; /* the number of bytes which we were given which we have written */

  printk(KERN_DEBUG "%s:Writing FPGA (size = %d bytes) to location 0x%x\n", __func__, cnt, (unsigned int) (rdev->rmap.fpga_virt + *f_pos));

  if(*f_pos >= ROACH_FPGA_LENGTH){
    printk(KERN_WARNING "request for way too much data, fpga size %u\n", ROACH_FPGA_LENGTH);
    have_written = -EINVAL;
    goto out_free_mutex;
  }

  if(*f_pos < ROACH_FPGA_LENGTH){
    printk(KERN_INFO "Entering FPGA write with cnt value=%u", cnt);
    /* Writn to FPGA*/
    start = *f_pos;
    if(start >= ROACH_FPGA_LENGTH){
      cnt = 0;
    }
    else if(start + cnt > ROACH_FPGA_LENGTH){
      cnt = ROACH_FPGA_LENGTH - start;
    }

    for(i = 0; i < cnt; i += 4){
      /*TODO: Write as u32s not shorts*/
      __get_user(tmpS, (unsigned int *)(buf + i));
      out_be32(rdev->rmap.fpga_virt + i + *f_pos, tmpS);
    }
  }

  *f_pos += cnt;
  printk(KERN_INFO "Writing %lld data to fpga \n", *f_pos);
out_free_mutex:
out:
  return cnt;
}

int roach_mem_release(struct inode *inode, struct file *filp)
{
  return 0;
}

int roach_config_open(struct inode *inode, struct file *filp)
{
  int i;
  roach_t *rdev_config;
  rdev_config = container_of(inode->i_cdev, roach_t, roach_devs[0]);
  filp->private_data = rdev_config;
  printk(KERN_NOTICE "roach open config called\n");

  /* Configure initn as output*/
  ppc4xx_gpio_dir_out(rdev->gc, GPIO_SMAP_INITN, GPIO_OUT_1);

  wmb();

  /* Set Write mode, and enable init_n and prog_b pins  */
  ppc4xx_gpio_set(rdev->gc, GPIO_SMAP_RDWRN, 0);
  ppc4xx_gpio_set(rdev->gc, GPIO_SMAP_INITN, 1);
  ppc4xx_gpio_set(rdev->gc, GPIO_SMAP_PROGN, 1);
  wmb();

  for (i = 0; i < 32; i++) { /* Delay for at least 350ns  */
    /* Set Write mode, and disable init_n and prog_n pins */
    ppc4xx_gpio_set(rdev->gc, GPIO_SMAP_INITN, 0);
    ppc4xx_gpio_set(rdev->gc, GPIO_SMAP_PROGN, 0);
  }
  wmb();

  /* Clear prog_n */
  ppc4xx_gpio_set(rdev->gc, GPIO_SMAP_PROGN, 1);
  wmb();

  /* configure initn as input */

  ppc4xx_gpio_dir_in(rdev->gc, GPIO_SMAP_INITN);


  /* Here we need to wait for initn to be driven to one by the v6 */
  for (i = 0; i < SMAP_INITN_WAIT + 1; i++) {
    if (ppc4xx_gpio_get(rdev->gc, GPIO_SMAP_INITN)){
      break;
    }
    if (i == SMAP_INITN_WAIT) {
      printk(KERN_INFO "SelectMap - Init_n pin has not been asserted\n");
      return -EIO;
    }
  }
  printk(KERN_NOTICE "rdev gpio preconfig done\n");

  gateware_bytes = 0;
  return 0;
}


ssize_t roach_config_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *f_pos)
{
  int i;
  int retval = -EIO;
  uint32_t word_count;
  volatile uint32_t *src;
  size_t have_written, will_write;


  have_written = 0; /* the number of bytes which we were given which we have written */

  if(gateware_bytes >= SMAP_IMAGE_SIZE){
    printk(KERN_WARNING "request for way too much data, gateware size %u\n", SMAP_IMAGE_SIZE);
    have_written = -EINVAL;
    goto out_free_mutex;
  }

  while(have_written < cnt){
    will_write = cnt - have_written;
    if(will_write > PAGE_SIZE){
      will_write = PAGE_SIZE;
    }

    retval = copy_from_user((uint32_t *)(rdev->rmap.tx_buf), (uint32_t *)(buf + have_written), will_write);
    if(retval != 0){
      printk(KERN_WARNING "copy from user failed with code %d, at %u/%u in write, %u bytes programmed so far\n", retval, have_written, cnt, gateware_bytes + have_written);
      goto out_free_mutex;
    }

    if((gateware_bytes + have_written + will_write) > SMAP_IMAGE_SIZE){
      if((gateware_bytes + have_written) < SMAP_IMAGE_SIZE){
        printk(KERN_WARNING "truncating buffer to %u as total exceeds gatware size %u\n", will_write, SMAP_IMAGE_SIZE);
        will_write = SMAP_IMAGE_SIZE - (gateware_bytes + have_written);
      } else {
        printk(KERN_WARNING "writing too much data, want %u, gateware size %u\n", gateware_bytes + have_written + will_write, SMAP_IMAGE_SIZE);
        will_write = 0;
        break; /* WARNING: end loop, but check if we need to do GPIO */
      }
    }

    word_count = will_write / 4;
    src = rdev->rmap.tx_buf;

    for(i = 0; i < word_count; i++){
      out_be32((uint32_t *)(rdev->rmap.smap_virt), *src);
      src++;
    }

    if((i * 4) < will_write){
      printk(KERN_WARNING "request to write %u was not a multiple of 4, only writing %u", i * 4, will_write);
      have_written += (i * 4);
      break; /* WARNING: assumes that this only occurs for last few bytes of cnt */
    }

    have_written += will_write;
  }

  gateware_bytes += have_written;

  if(gateware_bytes == SMAP_IMAGE_SIZE){
    /* Poll until done pin is enabled */
    for (i = 0; (i < SMAP_DONE_WAIT) && (ppc4xx_gpio_get(rdev->gc, GPIO_SMAP_DONE) == 0); i++);

    if(i == SMAP_DONE_WAIT){
      printk(KERN_ERR "error: SelectMAP programming failed, done stuck low\n");
      have_written = -EIO;
    } else {
      printk(KERN_INFO "ROACH2 Virtex-6 configuration completed successfully\n");
    }

    goto out_free_mutex;

  }
out_free_mutex:
out:
  return have_written;	
}


int roach_config_release(struct inode *inode, struct file *filp)
{
  return 0;
}

/* Set up cdev structure for a device */
static void roach_setup_cdev(struct cdev *dev, int minor, struct file_operations *fops)
{
  int err, devno = MKDEV(roach_major, minor);
  cdev_init(dev, fops);
  dev->owner = THIS_MODULE;
  dev->ops = fops;
  err = cdev_add(dev, devno, 1);
  if(err)
    printk(KERN_NOTICE "Error %d adding roach%d\n", err, minor);
}

/* ROACH subdevices */
static struct file_operations roach_config_fops = {
  .owner = THIS_MODULE,
  .open  = roach_config_open,
  .write = roach_config_write,
  .release = roach_config_release,
};

static struct file_operations roach_mem_fops = {
  .owner = THIS_MODULE,
  .open  = roach_mem_open,
  .read = roach_mem_read,
  .write = roach_mem_write,
  .release = roach_mem_release,
  .mmap = roach_mem_mmap,
};

static struct file_operations *roach_fops[MAX_ROACH_DEV] = {
  &roach_config_fops,
  &roach_mem_fops,
};

static int roach_init(void)
{
  dev_t dev = 0;
  int retval = 0;

  if(roach_major)
    retval = register_chrdev_region(dev, 1, "ROACH_NAME");
  else{
    retval = alloc_chrdev_region(&dev, roach_minor, ROACH_NR_DEVS , "ROACH_NAME");
    roach_major = MAJOR(dev);
  }
  if(retval < 0){
    printk(KERN_WARNING "roach2: Unable to get major %d\n", roach_major);
    goto out;
  }
  if(roach_major == 0)
    roach_major = retval;

  /*Allocate the devices*/
  rdev = kmalloc(sizeof(roach_t), GFP_KERNEL);
  if(!rdev){
    printk(KERN_WARNING "%s:Cannot allocate %d memory for rdev\n", __func__, sizeof(rdev));
    retval = -ENOMEM;
    goto out;
  }
  memset(rdev, 0, sizeof(roach_t));

  /*Setting up the two char devs*/
  roach_setup_cdev(rdev->roach_devs, 0, &roach_config_fops);
  roach_setup_cdev(rdev->roach_devs + 1, 1, &roach_mem_fops);

  /*Request regions for FPGA, SMAP*/
  if(!request_mem_region((resource_size_t)ROACH_SMAP_BASE, ROACH_SMAP_LENGTH, "roach_smap")){
    printk(KERN_ERR "%s: memory range 0x%08llx to 0x%08llx is in use\n", "roach_smap", ROACH_SMAP_BASE, ROACH_SMAP_BASE + ROACH_SMAP_LENGTH);
    retval = -ENOMEM;
  }

  rdev->rmap.smap_phy  = ROACH_SMAP_BASE;
  rdev->rmap.smap_virt = ioremap(ROACH_SMAP_BASE, ROACH_SMAP_LENGTH);
  printk(KERN_ALERT "new SMAP I/O memory range 0x%08llx to 0x%08llx allocated (virt:0x%p)\n",rdev->rmap.smap_phy, rdev->rmap.smap_phy + ROACH_SMAP_LENGTH, rdev->rmap.smap_virt);

  if(!request_mem_region((resource_size_t)ROACH_FPGA_BASE, ROACH_FPGA_LENGTH, "roach_fpga")){
    printk(KERN_ERR "%s: memory range 0x%08llx to 0x%08llx is in use\n", "roach_fpga", ROACH_FPGA_BASE, ROACH_FPGA_BASE + ROACH_FPGA_LENGTH);
    retval = -ENOMEM;
    goto out_free_smap;
  }

  rdev->rmap.fpga_phy  = ROACH_FPGA_BASE;
  rdev->rmap.fpga_virt = ioremap_nocache(ROACH_FPGA_BASE, ROACH_FPGA_LENGTH);
  printk(KERN_ALERT "new FPGA I/O memory range 0x%08llx to 0x%08llx allocated (virt:0x%p)\n",rdev->rmap.fpga_phy, rdev->rmap.fpga_phy + ROACH_FPGA_LENGTH, rdev->rmap.fpga_virt);


  rdev->gc = kmalloc(sizeof(struct ppc4xx_gpio_chip), GFP_KERNEL);
  if(rdev->gc == NULL){
    printk(KERN_ERR "roach: unable to allocate gpio structure\n");
    goto out_free_fpga;
  }

  rdev->gc->gpio_base = NULL;

  /* Memory mapping gpio registers */
  retval = gpio_request_resources(rdev->gc);
  if(retval){
    goto out_free_gc;
  }

  rdev->rmap.tx_buf = (void *) __get_free_page(GFP_KERNEL);
  if(!rdev->rmap.tx_buf){
    printk(KERN_ERR "rdev: failed getting memory for roach TX buf\n");
    retval = -ENOMEM;
    goto out_free_gpio;
  }

  rdev->rmap.rx_buf = (void *) __get_free_page(GFP_KERNEL);
  if(!rdev->rmap.rx_buf){
    printk(KERN_ERR "rdev: failed getting memory for roach RX buf\n");
    retval = -ENOMEM;
    goto out_free_tx;
  }

  goto out;

out_free_tx:
  free_page((int)rdev->rmap.tx_buf);
  rdev->rmap.tx_buf = NULL;
out_free_gpio:
  gpio_release_resources(rdev->gc);
out_free_gc:
  kfree(rdev->gc);
out_free_fpga:
  release_mem_region(ROACH_FPGA_BASE, ROACH_FPGA_LENGTH);
out_free_smap:
  release_mem_region(ROACH_SMAP_BASE, ROACH_SMAP_LENGTH);
out:
  return retval;
}

static void roach_exit(void)
{
  gpio_release_resources(rdev->gc);
  kfree(rdev->gc);
  rdev->gc = NULL;
  printk(KERN_INFO "gpio released\n");

  if(rdev){
#define release_roach_region(p, q) \
    if(rdev->rmap.p##_virt){ \
      iounmap(rdev->rmap.p##_virt); \
      rdev->rmap.p##_virt = NULL; \
    } \
    if(rdev->rmap.p##_phy){ \
      release_mem_region(rdev->rmap.p##_phy, ROACH_##q##_LENGTH); \
      rdev->rmap.p##_phy = 0; \
    }

    release_roach_region(smap, SMAP)
      release_roach_region(fpga, FPGA)
#undef release_roach_region
      printk(KERN_INFO "smap and fpga regions released\n");
    if(rdev->rmap.tx_buf){
      free_page((int)rdev->rmap.tx_buf);
      rdev->rmap.tx_buf = NULL;
    }
    printk(KERN_INFO "tx_buf released\n");
    if(rdev->rmap.rx_buf){
      free_page((int)rdev->rmap.rx_buf);
      rdev->rmap.rx_buf = NULL;
    }
    printk(KERN_INFO "rx_buf released\n");
    cdev_del(rdev->roach_devs);
    printk(KERN_INFO "Char device 1 released\n");
    cdev_del((rdev->roach_devs) + 1);
    printk(KERN_INFO "Char device 2 released\n");
    kfree(rdev);
    printk(KERN_INFO "Freed the rdev structure released\n");
    rdev = NULL;
  }
  unregister_chrdev_region(MKDEV(roach_major, 0), ROACH_NR_DEVS);
  printk(KERN_INFO "%s: roach2 kernel module unloaded\n", __func__);
}

/*
* Module declarations and macros
 */
MODULE_AUTHOR("Shanly Rajan <shanly.rajan@ska.ac.za>");
MODULE_DESCRIPTION("Hardware access driver for ROACH2 board");
MODULE_VERSION("0:0.1");
MODULE_LICENSE("GPL");

module_init(roach_init);
module_exit(roach_exit);


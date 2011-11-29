#ifndef ROACH_COMMON_H_
#define ROACH_COMMON_H_

#define ROACH_DEV_MAGIC 0xdeadbeef

#ifdef PDEBUG
#  define PDEBUG(lvl, fmt, args...) printk(KERN_DEBUG roach ": " fmt, ## args)
#endif


/* Some device specific definitions */
#define ROACH_SMAP_BASE         0x1C0100000ull
#define ROACH_FPGA_BASE         0x1D0000000ull
#define ROACH_GPIO_BASE   	0x1EF600B00ull


//#define ROACH_SMAP_LENGTH       0x000100000
#define ROACH_SMAP_LENGTH       0x000200000
#define ROACH_FPGA_LENGTH       0x004000000
#define ROACH_GPIO_LENGTH       0x000000200 /* 0x144 min needed*/


/* TODO: Temporary*/
#define GPIO_SMAP_INITN 12
#define GPIO_SMAP_DONE  13
#define GPIO_SMAP_PROGN 14
#define GPIO_SMAP_RDWRN 15
#define GPIO_SMAP_GPIO0 28
#define GPIO_SMAP_GPIO1 27
#define GPIO_SMAP_GPIO2 30
#define GPIO_SMAP_GPIO3 31
#define GPIO_SMAP_LED   29


/* SMAP Control register definitions */
/* Delay fors smap checks */
#define SMAP_INITN_WAIT 100000
#define SMAP_DONE_WAIT  100000


/* Default SX475T image size in bytes */
#define SMAP_IMAGE_SIZE 19586188

/*
 * Function declarations for the roach_dev module
 */
ssize_t roach_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t roach_write(struct file *file, const char __user *buffer, size_t length, loff_t *offset);
int roach_open(struct inode *inode, struct file *file);
int roach_release(struct inode *inode, struct file *file);

#if 0
ssize_t roach_config_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t roach_config_write(struct file *file,const char __user *buffer, size_t length, loff_t *offset);
int roach_config_open(struct inode *inode, struct file *file);
int roach_config_release(struct inode *inode, struct file *file);
int roach_mem_open(struct inode *inode, struct file *file);
int roach_mem_release(struct inode *inode, struct file *file);

ssize_t roach_mem_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t roach_mem_write(struct file *file,const char __user *buffer, size_t length, loff_t *offset);
int roach_mem_open(struct inode *inode, struct file *file);
int roach_mem_release(struct inode *inode, struct file *file);
#endif

#endif /* ROACH_COMMON_H_ */







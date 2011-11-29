/*
 * PPC4xx gpio driver for ROACH2
 *
 * Copyright (c) 2008 Harris Corporation
 * Copyright (c) 2008 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 * Copyright (c) MontaVista Software, Inc. 2008.
 *
 * Author: Steve Falco <sfalco@harris.com>
 * Modified: Shanly Rajan <shanly.rajan@ska.ac.za>, SA SKA, 10th May 2011
 * Comments: Ported ppc gpio code to roach2 platform 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h> 		/* clrbits32, setbits32 */

#include <asm/io.h>		/* ioremap, iounmap */ 

#define GPIO0           0
#define GPIO1           1

#define GPIO_MAX        32
#define GPIO_ALT1_SEL   0x40000000
#define GPIO_ALT2_SEL   0x80000000
#define GPIO_ALT3_SEL   0xc0000000
#define GPIO_IN_SEL     0x40000000


#define GPIO_MASK(gpio)         (0x80000000 >> (gpio))
#define GPIO_MASK2(gpio)        (0xc0000000 >> ((gpio) * 2))


typedef enum gpio_select { GPIO_SEL, GPIO_ALT1, GPIO_ALT2, GPIO_ALT3 } gpio_select_t;
typedef enum gpio_driver { GPIO_DIS, GPIO_IN, GPIO_OUT, GPIO_BI } gpio_driver_t;
typedef enum gpio_out    { GPIO_OUT_0, GPIO_OUT_1, GPIO_OUT_NO_CHG } gpio_out_t;

/* Physical GPIO register layout */
struct ppc4xx_gpio {
        __be32 or;
        __be32 tcr;
        __be32 osrl;
        __be32 osrh;
        __be32 tsrl;
        __be32 tsrh;
        __be32 odr;
        __be32 ir;
        __be32 rr1;
        __be32 rr2;
        __be32 rr3;
        __be32 reserved1;
        __be32 isr1l;
        __be32 isr1h;
        __be32 isr2l;
        __be32 isr2h;
        __be32 isr3l;
        __be32 isr3h;
};

struct ppc4xx_gpio_chip {
	void *gpio_base;
        spinlock_t lock;
};

static int dump_gpio(struct ppc4xx_gpio_chip *gc)
{
        struct ppc4xx_gpio __iomem *regs = gc->gpio_base;
	printk(KERN_INFO "GPIO0_OR           :   0x%08x\n", in_be32(&regs->or     ));
	printk(KERN_INFO "GPIO0_TCR          :   0x%08x\n", in_be32(&regs->tcr));
	printk(KERN_INFO "GPIO0_OSRL         :   0x%08x\n", in_be32(&regs->osrl));
	printk(KERN_INFO "GPIO0_OSRH         :   0x%08x\n", in_be32(&regs->osrh));
	printk(KERN_INFO "GPIO0_TSRL         :   0x%08x\n", in_be32(&regs->tsrl));
	printk(KERN_INFO "GPIO0_TSRH         :   0x%08x\n", in_be32(&regs->tsrh));
	printk(KERN_INFO "GPIO0_ISR1L        :   0x%08x\n", in_be32(&regs->isr1l));
	printk(KERN_INFO "GPIO0_ISR1H        :   0x%08x\n", in_be32(&regs->isr1h));
	printk(KERN_INFO "GPIO0_ISR2L        :   0x%08x\n", in_be32(&regs->isr2l));
	printk(KERN_INFO "GPIO0_ISR2H        :   0x%08x\n", in_be32(&regs->isr2h));
	printk(KERN_INFO "GPIO0_ISR3L        :   0x%08x\n", in_be32(&regs->isr3l));
	printk(KERN_INFO "GPIO0_ISR3H        :   0x%08x\n", in_be32(&regs->isr3h));

	return 0;
}

static int ppc4xx_gpio_get(struct ppc4xx_gpio_chip *gc, unsigned int gpio)
{
        struct ppc4xx_gpio __iomem *regs = gc->gpio_base;
 	//printk(KERN_INFO "%s: gc = %p ,ir = %p, gpio = %d, gpio_base = %p\n",__func__, gc, &regs->ir, gpio, gc->gpio_base);	
        return (in_be32(&regs->ir) & GPIO_MASK(gpio) ? 1 : 0);
}

static inline void
__ppc4xx_gpio_set(struct ppc4xx_gpio_chip *gc, unsigned int gpio, int val)
{
        struct ppc4xx_gpio __iomem *regs = gc->gpio_base;

	if (val)
		setbits32(&regs->or, GPIO_MASK(gpio));
	else
		clrbits32(&regs->or, GPIO_MASK(gpio));
}

void ppc4xx_gpio_set_isr1l(struct ppc4xx_gpio_chip *gc, int val)
{
        struct ppc4xx_gpio __iomem *regs = gc->gpio_base;
	if(val)
                setbits32(&regs->isr1l, val);
}

static void
ppc4xx_gpio_set(struct ppc4xx_gpio_chip *gc, unsigned int gpio, int val)
{
        unsigned long flags;

        spin_lock_irqsave(&gc->lock, flags);

        __ppc4xx_gpio_set(gc, gpio, val);

        spin_unlock_irqrestore(&gc->lock, flags);
#ifdef DEBUG
        PDEBUG(9,"%s: gpio: %d val: %d\n", __func__, gpio, val);
#endif
}

static int ppc4xx_gpio_dir_in(struct ppc4xx_gpio_chip *gc, unsigned int gpio)
{
        unsigned long flags;

        struct ppc4xx_gpio __iomem *regs = gc->gpio_base;

        spin_lock_irqsave(&gc->lock, flags);

        /* Disable open-drain function */
        clrbits32(&regs->odr, GPIO_MASK(gpio));

        /* Float the pin */
        clrbits32(&regs->tcr, GPIO_MASK(gpio));

        /* Bits 0-15 use TSRL/OSRL, bits 16-31 use TSRH/OSRH */
        if (gpio < 16) {
                clrbits32(&regs->osrl, GPIO_MASK2(gpio));
                clrbits32(&regs->tsrl, GPIO_MASK2(gpio));
        } else {
                clrbits32(&regs->osrh, GPIO_MASK2(gpio));
                clrbits32(&regs->tsrh, GPIO_MASK2(gpio));
        }

        spin_unlock_irqrestore(&gc->lock, flags);
        return 0;
}

static int
ppc4xx_gpio_dir_out(struct ppc4xx_gpio_chip *gc, unsigned int gpio, int val)
{
        unsigned long flags;

        struct ppc4xx_gpio __iomem *regs = gc->gpio_base;

        spin_lock_irqsave(&gc->lock, flags);

        /* First set initial value */
        __ppc4xx_gpio_set(gc, gpio, val);

        /* Disable open-drain function */
        clrbits32(&regs->odr, GPIO_MASK(gpio));

        /* Drive the pin */
        setbits32(&regs->tcr, GPIO_MASK(gpio));

        /* Bits 0-15 use TSRL, bits 16-31 use TSRH */
        if (gpio < 16) {
                clrbits32(&regs->osrl, GPIO_MASK2(gpio));
                clrbits32(&regs->tsrl, GPIO_MASK2(gpio));
        } else {
                clrbits32(&regs->osrh, GPIO_MASK2(gpio));
                clrbits32(&regs->tsrh, GPIO_MASK2(gpio));
        }

        spin_unlock_irqrestore(&gc->lock, flags);
#ifdef DEBUG
        PDEBUG(9,"%s: gpio: %d val: %d\n", __func__, gpio, val);
#endif

        return 0;
}

/* Allocate all resources for the controller */

static int __devinit gpio_request_resources(struct ppc4xx_gpio_chip *gc)
{
  if (!request_mem_region(ROACH_GPIO_BASE, ROACH_GPIO_LENGTH, "roach-gpio")) {
    printk(KERN_ERR "%s: memory range 0x%llx to 0x%llx is in use\n",
                          "roach-gpio",
                          (resource_size_t) ROACH_GPIO_BASE,
                          (resource_size_t)(ROACH_GPIO_BASE + ROACH_GPIO_LENGTH));
    return ENOMEM;
  }

  gc->gpio_base = ioremap(ROACH_GPIO_BASE, ROACH_GPIO_LENGTH);
 // printk(KERN_DEBUG "gpio-sanity-test: ioremap - req = %x, ret = %x\n", ROACH_GPIO_BASE, (unsigned int) gc->gpio_base);
        printk(KERN_ALERT "new gpio I/O memory range 0x%08llx to 0x%08llx allocated (virt:0x%p)\n",ROACH_GPIO_BASE, ROACH_GPIO_BASE + ROACH_SMAP_LENGTH, gc->gpio_base);


  if (!(gc->gpio_base)) {
    release_mem_region(ROACH_GPIO_BASE, ROACH_GPIO_LENGTH);
    return ENOMEM;
  }

  return 0;
}

/*
 * Release all resources for the controller.
 */

static void gpio_release_resources(struct ppc4xx_gpio_chip *gc)
{

  if (gc->gpio_base){
    iounmap(gc->gpio_base);
  }
  release_mem_region(ROACH_GPIO_BASE, ROACH_GPIO_LENGTH);

}


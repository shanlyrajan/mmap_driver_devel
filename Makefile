obj-m = roach_dev.o

PWD = $(shell pwd)

all:
	$(MAKE) -C /home/adam/work/roach2/linux_devel SUBDIRS=$(PWD) modules

clean:
	rm -f *.o *.ko *.order *.symvers *.mod.c


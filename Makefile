obj-m = roach_mmap.o

PWD = $(shell pwd)

all:
	$(MAKE) -C /home/shanly/Work/2roach/linux_devel SUBDIRS=$(PWD) modules

clean:
	rm -f *.o *.ko *.order *.symvers *.mod.c


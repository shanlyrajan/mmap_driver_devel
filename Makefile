obj-m = roach_dev.o

PWD = $(shell pwd)

all:
	$(MAKE) -C /home/shanly/Work/linux_casper SUBDIRS=$(PWD) modules

clean:
	rm -f *.o *.ko *.order *.symvers *.mod.c


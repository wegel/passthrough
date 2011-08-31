obj-m := passthrough.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
EXTRA_CFLAGS := -g
default:
	$(MAKE) -I/usr/include -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	$(MAKE) -I/usr/include -C $(KDIR) SUBDIRS=$(PWD) clean

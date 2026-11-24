obj-m += audiousb.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all: module test_audiousb

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

test_audiousb: test_audiousb.c audiousb_ioctl.h
	$(CC) -Wall -Wextra -o $@ test_audiousb.c

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f test_audiousb
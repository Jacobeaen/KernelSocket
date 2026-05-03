obj-m += ks.o

ks-objs := ks_test.o ks_main.o ks_sock.o ks_echo.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
        make -C $(KDIR) M=$(PWD) clean
obj-m += ks.o

SRC=src
TEST=tests

ccflags-y := -I$(srctree)/$(src)/../include

ks-objs := $(SRC)/ks_main.o \
           $(SRC)/ks_sock.o \
           $(TEST)/ks_user_kernel_test.o \
#            $(TEST)/ks_kernel_user_test.o \
#            $(TEST)/ks_test.o \

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)



all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	find . -name "*.o" -type f -delete
obj-m += ks_linux_module.o
ks_linux_module-objs := ks_linux.o ../../test/module_main.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
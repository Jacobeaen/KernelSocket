#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "ks.h"

extern void ks_test(void);

static int __init ks_init(void)
{
    printk(KERN_INFO "KS module loaded\n");

    ks_test();
    ks_echo_start(); 
    
    return 0;
}

static void __exit ks_exit(void)
{
    printk(KERN_INFO "KS module unloaded\n");
}

module_init(ks_init);
module_exit(ks_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("KernelSocketsCommand");
MODULE_DESCRIPTION("Kernel Socket wrapper");
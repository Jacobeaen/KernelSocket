#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "ks.h"

extern void ks_test(void);

static int __init ks_init(void)
{
    printk(KERN_INFO "KS module loaded\n");

    // ks_test_client_tcp(); // kernel->userspace (client)
    // ks_test_client_udp(); // kernel->userspace (client) 
    // ks_test_us_krnl(); // two clients (tcp + udp) starting in one function
    // ks_echo_start_tcp();  // userspace->kernel (echo server)
    // ks_echo_start_udp();  // userspace->kernel (echo server)
    
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
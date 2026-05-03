#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/slab.h>

#include <net/net_namespace.h>

#define PORT 5555

static struct socket *listen_sock;
static struct task_struct *thread;

static int echo_thread(void *arg)
{
    struct socket *client;
    struct kvec iov;
    struct msghdr msg;
    char buf[256];
    int ret;

    printk(KERN_INFO "[echo] thread started\n");

    while (!kthread_should_stop()) {

        ret = kernel_accept(listen_sock, &client, 0);
        if (ret < 0) {
            schedule_timeout_interruptible(msecs_to_jiffies(500));
            continue;
        }

        printk(KERN_INFO "[echo] client connected\n");

        while (!kthread_should_stop()) {

            memset(&msg, 0, sizeof(msg));

            iov.iov_base = buf;
            iov.iov_len = sizeof(buf) - 1;

            ret = kernel_recvmsg(client, &msg, &iov, 1, sizeof(buf)-1, 0);

            if (ret <= 0)
                break;

            buf[ret] = '\0';

            printk(KERN_INFO "[echo] recv: %s\n", buf);

            kernel_sendmsg(client, &msg, &iov, 1, ret);
        }

        sock_release(client);
        printk(KERN_INFO "[echo] client disconnected\n");
    }

    return 0;
}

static int __init echo_init(void)
{
    struct sockaddr_in addr;
    int ret;

    printk(KERN_INFO "[echo] init\n");

    ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &listen_sock);
    if (ret < 0)
        return ret;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    ret = kernel_bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
        return ret;

    ret = kernel_listen(listen_sock, 5);
    if (ret < 0)
        return ret;

    thread = kthread_run(echo_thread, NULL, "ks_echo_thread");

    return 0;
}

static void __exit echo_exit(void)
{
    printk(KERN_INFO "[echo] exit\n");

    if (thread)
        kthread_stop(thread);

    if (listen_sock)
        sock_release(listen_sock);
}

int ks_echo_start(void)
{
    struct sockaddr_in addr;
    int ret;

    printk(KERN_INFO "[echo] init\n");

    ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &listen_sock);
    if (ret < 0)
        return ret;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    ret = kernel_bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
        return ret;

    ret = kernel_listen(listen_sock, 5);
    if (ret < 0)
        return ret;

    thread = kthread_run(echo_thread, NULL, "ks_echo_thread");

    return 0;
}

MODULE_LICENSE("GPL");
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/slab.h>

#include <net/net_namespace.h>

#include "ks.h"

#define TCP_PORT 7777
#define UDP_PORT 8888

static struct task_struct *tcp_thread;
static struct task_struct *udp_thread;

static ks_socket_t *ks_listen_tcp = NULL;
static ks_socket_t *ks_listen_udp = NULL;

static int echo_thread_tcp(void *arg)
{
    ks_socket_t *client_ks;
    char buf[256];
    int ret;

    printk(KERN_INFO "[ks_user_kernel_test_tcp] echo thread started\n");
    while (!kthread_should_stop())
    {
        client_ks = ks_accept(ks_listen_tcp);
        if (!client_ks) {
            schedule_timeout_interruptible(msecs_to_jiffies(500));
            continue;
        }
        
        printk(KERN_INFO "[ks_user_kernel_test_tcp] client connected\n");

        while (!kthread_should_stop())
        {
            ret = ks_recv(client_ks, buf, sizeof(buf) - 1);
            if (ret <= 0)
                break;

            buf[ret] = '\0';
            printk(KERN_INFO "[ks_user_kernel_test_tcp] recv: %s\n", buf);

            ks_send(client_ks, buf, ret);
        }
        ks_close(client_ks);
        printk(KERN_INFO "[ks_user_kernel_test_tcp] client disconnected\n");
    }

    return 0;
}

int ks_echo_start_tcp(void)
{
    int ret;

    printk(KERN_INFO "[ks_echo_start_tcp] init\n");

    ks_listen_tcp = ks_socket(KS_TCP);
    if (!ks_listen_tcp)
        return KS_ERR_NOMEM;

    ret = ks_bind(ks_listen_tcp, NULL, TCP_PORT);
    if (ret < 0) {
        ks_close(ks_listen_tcp);
        return KS_ERR;
    }

    // ret = kernel_listen(ks_listen_tcp->sock, 5);
    ret = ks_listen(ks_listen_tcp, 1);
    if (ret < 0) {
        ks_close(ks_listen_tcp);
        return KS_ERR;
    }

    tcp_thread = kthread_run(echo_thread_tcp, NULL, "ks_echo_thread_tcp");
    if (IS_ERR(tcp_thread)) {
        ks_close(ks_listen_tcp);
        return KS_ERR;
    }

    return KS_OK;
}


static int echo_thread_udp(void *arg) 
{
    char buf[256];
    char src_ip[16];
    unsigned short src_port;
    int ret;

    printk(KERN_INFO "[ks_user_kernel_test_udp] echo thread started\n");

    while (!kthread_should_stop()) 
    {
        ret = ks_recvfrom(ks_listen_udp, buf, sizeof(buf) - 1, src_ip, &src_port);
        if (ret <= 0) {
            schedule_timeout_interruptible(msecs_to_jiffies(500));
            continue;
        }

        buf[ret] = '\0';
        printk(KERN_INFO "[ks_user_kernel_test_udp] recv from %s:%d: %s\n", src_ip, src_port, buf);

        // Отправляем ответ клиенту по обратному адресу
        ks_sendto(ks_listen_udp, buf, ret, src_ip, src_port);
    }

    return 0;
}

int ks_echo_start_udp(void) 
{
    struct sockaddr_in addr;
    int ret;

    printk(KERN_INFO "[ks_echo_start_udp] init\n");
    
    ks_listen_udp = ks_socket(KS_UDP);
    if (!ks_listen_udp)
        return KS_ERR_NOMEM;

    ret = ks_bind(ks_listen_udp, NULL, UDP_PORT);
    if (ret < 0) {
        ks_close(ks_listen_udp);
        return KS_ERR;
    }

    udp_thread = kthread_run(echo_thread_udp, NULL, "ks_echo_thread_udp");
    if (IS_ERR(udp_thread)) {
        ks_close(ks_listen_udp);
        return KS_ERR;
    }

    return KS_OK;
}

MODULE_LICENSE("GPL");

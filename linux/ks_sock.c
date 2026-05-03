#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <net/sock.h>
#include <linux/inet.h>

#include "ks.h"


typedef struct ks_socket {
    struct socket *sock;
    int protocol;
} ks_socket_t;


ks_socket_t* ks_socket(int protocol)
{
    ks_socket_t *ks;
    int type;
    int proto;
    int ret;

    if (protocol == KS_TCP) {
        type = SOCK_STREAM;
        proto = IPPROTO_TCP;
    } else if (protocol == KS_UDP) {
        type = SOCK_DGRAM;
        proto = IPPROTO_UDP;
    } else {
        return NULL;
    }

    ks = kmalloc(sizeof(*ks), GFP_KERNEL);
    if (!ks)
        return NULL;

    ret = sock_create(AF_INET, type, proto, &ks->sock);
    if (ret < 0) {
        kfree(ks);
        return NULL;
    }

    ks->protocol = protocol;
    return ks;
}

int ks_connect(ks_socket_t *ks, const char *ip, unsigned short port)
{
    struct sockaddr_in addr = {0};
    int ret;

    if (!ks || ks->protocol != KS_TCP)
        return KS_ERR_INVAL;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    ret = in4_pton(ip, -1, (u8 *)&addr.sin_addr.s_addr, -1, NULL);
    if (!ret)
        return KS_ERR_INVAL;

    ret = kernel_connect(ks->sock,
                         (struct sockaddr *)&addr,
                         sizeof(addr),
                         0);

    if (ret < 0)
        return KS_ERR_CONN;

    return KS_OK;
}

int ks_send(ks_socket_t *ks, const void *buf, unsigned int len)
{
    struct kvec iov;
    struct msghdr msg = {};
    int ret;

    if (!ks || !buf || len == 0)
        return KS_ERR_INVAL;

    iov.iov_base = (void *)buf;
    iov.iov_len = len;

    ret = kernel_sendmsg(ks->sock, &msg, &iov, 1, len);

    if (ret < 0)
        return KS_ERR;

    return ret;
}

int ks_recv(ks_socket_t *ks, void *buf, unsigned int len)
{
    struct kvec iov;
    struct msghdr msg = {};
    int ret;

    if (!ks || !buf || len == 0)
        return KS_ERR_INVAL;

    iov.iov_base = buf;
    iov.iov_len = len;

    ret = kernel_recvmsg(ks->sock, &msg, &iov, 1, len, 0);

    if (ret == 0)
        return 0; // EOF

    if (ret < 0)
        return KS_ERR;

    return ret;
}

int ks_sendto(ks_socket_t *ks,
              const void *buf,
              unsigned int len,
              const char *ip,
              unsigned short port)
{
    struct sockaddr_in addr = {0};
    struct kvec iov;
    struct msghdr msg = {};
    int ret;

    if (!ks || ks->protocol != KS_UDP)
        return KS_ERR_INVAL;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (!in4_pton(ip, -1, (u8 *)&addr.sin_addr.s_addr, -1, NULL))
        return KS_ERR_INVAL;

    msg.msg_name = &addr;
    msg.msg_namelen = sizeof(addr);

    iov.iov_base = (void *)buf;
    iov.iov_len = len;

    ret = kernel_sendmsg(ks->sock, &msg, &iov, 1, len);

    if (ret < 0)
        return KS_ERR;

    return ret;
}

int ks_recvfrom(ks_socket_t *ks,
                void *buf,
                unsigned int len,
                char *src_ip,
                unsigned short *src_port)
{
    struct sockaddr_in addr;
    struct kvec iov;
    struct msghdr msg = {};
    int ret;

    if (!ks || ks->protocol != KS_UDP)
        return KS_ERR_INVAL;

    msg.msg_name = &addr;
    msg.msg_namelen = sizeof(addr);

    iov.iov_base = buf;
    iov.iov_len = len;

    ret = kernel_recvmsg(ks->sock, &msg, &iov, 1, len, 0);

    if (ret < 0)
        return KS_ERR;

    if (src_ip)
        snprintf(src_ip, 16, "%pI4", &addr.sin_addr);

    if (src_port)
        *src_port = ntohs(addr.sin_port);

    return ret;
}

void ks_close(ks_socket_t *ks)
{
    if (!ks)
        return;

    if (ks->sock)
        sock_release(ks->sock);

    kfree(ks);
}
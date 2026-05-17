/**
 * @file ks_linux.c
 * @brief KernelSocket — Linux LKM реализация
 */

#include "ks_linux_internal.h"
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>

/* =========================================================================
 * Вспомогательные функции
 * ========================================================================= */

/**
 * @brief Проверка контекста выполнения.
 * Сетевые функции ядра Linux (особенно tcp) могут засыпать (sleep).
 * Вызов их из обработчика прерывания или спин-блока приведет к Kernel Panic.
 */
static bool is_safe_context(void) {
    return !(in_atomic() || in_interrupt());
}

/* =========================================================================
 * РЕАЛИЗАЦИЯ ПУБЛИЧНОГО API
 * ========================================================================= */

KS_SOCKET* ksSocket(int Protocol) {
    KS_SOCKET* Sock;
    int err;
    int type = (Protocol == KS_TCP) ? SOCK_STREAM : SOCK_DGRAM;
    int proto = (Protocol == KS_TCP) ? IPPROTO_TCP : IPPROTO_UDP;

    if (!is_safe_context()) {
        printk(KERN_ERR "[KernelSocket] ksSocket: вызвано в атомарном контексте!\n");
        return NULL;
    }

    Sock = kmalloc(sizeof(KS_SOCKET), GFP_KERNEL);
    if (!Sock) return NULL;

    Sock->Protocol = Protocol;
    Sock->is_listening = false;

    /* init_net - это дефолтное сетевое пространство имен (namespace) */
    err = sock_create_kern(&init_net, PF_INET, type, proto, &Sock->sock);
    if (err < 0) {
        printk(KERN_ERR "[KernelSocket] sock_create_kern ошибка: %d\n", err);
        kfree(Sock);
        return NULL;
    }

    return Sock;
}

void ksClose(KS_SOCKET* Sock) {
    if (!Sock) return;
    
    if (Sock->sock) {
        sock_release(Sock->sock);
        Sock->sock = NULL;
    }
    kfree(Sock);
}

int ksBind(KS_SOCKET* Sock, const char* Ip, unsigned short Port) {
    struct sockaddr_in addr;
    int err;

    if (!Sock || !Ip || !is_safe_context()) return KS_ERR_INVAL;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(Port);
    addr.sin_addr.s_addr = in_aton(Ip);

    err = kernel_bind(Sock->sock, (struct sockaddr *)&addr, sizeof(addr));
    if (err < 0) {
        printk(KERN_ERR "[KernelSocket] kernel_bind ошибка: %d\n", err);
        return KS_ERR;
    }

    return KS_OK;
}

int ksListen(KS_SOCKET* Sock, int Backlog) {
    int err;

    if (!Sock || Sock->Protocol != KS_TCP || !is_safe_context()) return KS_ERR_INVAL;

    err = kernel_listen(Sock->sock, Backlog);
    if (err < 0) {
        printk(KERN_ERR "[KernelSocket] kernel_listen ошибка: %d\n", err);
        return KS_ERR;
    }

    Sock->is_listening = true;
    return KS_OK;
}

int ksAccept(KS_SOCKET* Sock, KS_SOCKET** NewSock, char* ClientIp, unsigned short* ClientPort) {
    int err;
    struct socket *new_sock = NULL;
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);

    if (!Sock || !NewSock || !Sock->is_listening || !is_safe_context()) return KS_ERR_INVAL;

    /* Создаем пустой сокет для будущего клиента */
    err = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &new_sock);
    if (err < 0) return KS_ERR_NOMEM;

    /* Блокируемся в ожидании клиента (0 = флаги) */
    err = kernel_accept(Sock->sock, &new_sock, 0);
    if (err < 0) {
        sock_release(new_sock);
        return KS_ERR;
    }

    /* Упаковываем новый сокет в нашу структуру */
    *NewSock = kmalloc(sizeof(KS_SOCKET), GFP_KERNEL);
    if (!*NewSock) {
        sock_release(new_sock);
        return KS_ERR_NOMEM;
    }
    (*NewSock)->Protocol = KS_TCP;
    (*NewSock)->sock = new_sock;
    (*NewSock)->is_listening = false;

    /* Узнаем IP и порт клиента */
    if (ClientIp != NULL || ClientPort != NULL) {
        err = kernel_getpeername(new_sock, (struct sockaddr *)&addr);
        if (err >= 0) {
            if (ClientIp) {
                /* В Linux ядре формат %pI4 сам красиво преобразует IP-адрес в строку */
                snprintf(ClientIp, 16, "%pI4", &addr.sin_addr.s_addr);
            }
            if (ClientPort) {
                *ClientPort = ntohs(addr.sin_port);
            }
        }
    }

    return KS_OK;
}

int ksConnect(KS_SOCKET* Sock, const char* Ip, unsigned short Port) {
    struct sockaddr_in addr;
    int err;

    if (!Sock || !Ip || Port == 0 || Sock->Protocol != KS_TCP || !is_safe_context()) return KS_ERR_INVAL;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(Port);
    addr.sin_addr.s_addr = in_aton(Ip);

    err = kernel_connect(Sock->sock, (struct sockaddr *)&addr, sizeof(addr), 0);
    if (err < 0) {
        if (err == -ECONNREFUSED) return KS_ERR_CONN;
        return KS_ERR;
    }

    return KS_OK;
}

int ksSend(KS_SOCKET* Sock, const void* Buf, unsigned int Len) {
    struct kvec vec;
    struct msghdr msg;
    int err;

    if (!Sock || !Buf || Len == 0 || !is_safe_context()) return KS_ERR_INVAL;

    memset(&msg, 0, sizeof(msg));
    vec.iov_base = (void *)Buf;
    vec.iov_len = Len;

    err = kernel_sendmsg(Sock->sock, &msg, &vec, 1, Len);
    return (err < 0) ? KS_ERR : err;
}

int ksRecv(KS_SOCKET* Sock, void* Buf, unsigned int Len) {
    struct kvec vec;
    struct msghdr msg;
    int err;

    if (!Sock || !Buf || Len == 0 || !is_safe_context()) return KS_ERR_INVAL;

    memset(&msg, 0, sizeof(msg));
    vec.iov_base = Buf;
    vec.iov_len = Len;

    err = kernel_recvmsg(Sock->sock, &msg, &vec, 1, Len, 0);
    
    /* Соединение закрыто удаленной стороной (EOF) */
    if (err == 0) return 0;
    
    return (err < 0) ? KS_ERR : err;
}

int ksSendTo(KS_SOCKET* Sock, const void* Buf, unsigned int Len, const char* Ip, unsigned short Port) {
    struct sockaddr_in addr;
    struct kvec vec;
    struct msghdr msg;
    int err;

    if (!Sock || !Buf || Len == 0 || !Ip || !is_safe_context()) return KS_ERR_INVAL;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(Port);
    addr.sin_addr.s_addr = in_aton(Ip);

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &addr;
    msg.msg_namelen = sizeof(addr);

    vec.iov_base = (void *)Buf;
    vec.iov_len = Len;

    err = kernel_sendmsg(Sock->sock, &msg, &vec, 1, Len);
    return (err < 0) ? KS_ERR : err;
}

int ksRecvFrom(KS_SOCKET* Sock, void* Buf, unsigned int Len, char* SrcIp, unsigned short* SrcPort) {
    struct sockaddr_in addr;
    struct kvec vec;
    struct msghdr msg;
    int err;

    if (!Sock || !Buf || Len == 0 || !is_safe_context()) return KS_ERR_INVAL;

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &addr;
    msg.msg_namelen = sizeof(addr);

    vec.iov_base = Buf;
    vec.iov_len = Len;

    err = kernel_recvmsg(Sock->sock, &msg, &vec, 1, Len, 0);
    if (err < 0) return KS_ERR;

    if (SrcIp != NULL) {
        snprintf(SrcIp, 16, "%pI4", &addr.sin_addr.s_addr);
    }
    if (SrcPort != NULL) {
        *SrcPort = ntohs(addr.sin_port);
    }

    return err; /* Возвращаем количество принятых байт */
}
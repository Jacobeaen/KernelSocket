/**
 * @file ks_linux_internal.h
 * @brief Внутренние структуры для Linux-реализации KernelSocket.
 */

#ifndef KS_LINUX_INTERNAL_H
#define KS_LINUX_INTERNAL_H

#include <linux/net.h>
#include <linux/in.h>
#include <linux/inet.h>
#include "../../include/ks_api.h"

/* =========================================================================
 * Внутренняя структура сокета (Реализация непрозрачного типа KS_SOCKET)
 * ========================================================================= */
struct KS_SOCKET {
    int Protocol;             /**< KS_TCP или KS_UDP */
    struct socket *sock;      /**< Встроенная структура сокета ядра Linux */
    bool is_listening;        /**< Флаг режима сервера (для TCP) */
};

#endif /* KS_LINUX_INTERNAL_H */
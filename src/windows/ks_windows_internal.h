#ifndef KS_WINDOWS_INTERNAL_H
#define KS_WINDOWS_INTERNAL_H

#include <ntddk.h>
#include <wsk.h>
#include "../../include/ks_api.h"

/* =========================================================================
 * Внутренняя структура сокета (Реализация непрозрачного типа KS_SOCKET)
 * ========================================================================= */
struct KS_SOCKET {
    int         Protocol;      /**< KS_TCP или KS_UDP */
    PWSK_SOCKET WskSocket;     /**< Указатель на объект WSK (создается лениво) */
    
    BOOLEAN     IsBound;       /**< Был ли вызван ksBind */
    SOCKADDR_IN LocalAddress;  /**< Локальный адрес (сохраняем при ksBind) */
    BOOLEAN     IsListening;   /**< Переведен ли сокет в режим Listen (TCP) */
};

/* =========================================================================
 * Инициализация WSK (вызывается из DriverEntry / DriverUnload)
 * ========================================================================= */
NTSTATUS KsWskInit(void);
void     KsWskCleanup(void);

#endif /* KS_WINDOWS_INTERNAL_H */
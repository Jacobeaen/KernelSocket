/**
 * @file ks_api.h
 * @brief KernelSocket — Единый API для работы с TCP/UDP в режиме ядра (Windows/Linux)
 *
 * @details 
 * Этот заголовочный файл предоставляет кроссплатформенный интерфейс для 
 * организации сетевого взаимодействия из драйверов. Поддерживает как клиентские, 
 * так и серверные сценарии (K<->U, U<->K, K<->K).
 *
 * @warning ОБЩИЕ ПРАВИЛА БЕЗОПАСНОСТИ (ИЗБЕЖАНИЕ BUGCHECK / KERNEL PANIC):
 * 1. IRQL (Windows): Все функции должны вызываться СТРОГО на уровне IRQL == PASSIVE_LEVEL. 
 *    Вызов на DISPATCH_LEVEL или выше приведет к BSOD (IRQL_NOT_LESS_OR_EQUAL).
 * 2. Память: Все передаваемые буферы (Buf) должны располагаться в невыгружаемой 
 *    памяти (NonPagedPool для Windows, kmalloc с GFP_KERNEL для Linux). Передача 
 *    указателей на user-mode память без правильной блокировки (MDL) приведет к 
 *    BSOD (PAGE_FAULT_IN_NONPAGED_AREA).
 */

#ifndef KS_API_H
#define KS_API_H

/* =========================================================================
 * 1. Определение платформы и системные зависимости
 * ========================================================================= */
#if defined(_WIN32) || defined(_WIN64)
    #define KS_PLATFORM_WINDOWS
    #include <ntddk.h>
#elif defined(__linux__) || defined(__KERNEL__)
    #define KS_PLATFORM_LINUX
    #include <linux/kernel.h>
    #include <linux/module.h>
    #include <linux/types.h>
#else
    #error "[KernelSocket] Unsupported platform! Must be compiled for Windows or Linux kernel."
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 2. Константы (SCREAMING_SNAKE_CASE)
 * ========================================================================= */

/** @brief Идентификаторы протоколов */
#define KS_TCP  0
#define KS_UDP  1

/** @brief Коды возврата */
#define KS_OK           0    /**< Успешное выполнение */
#define KS_ERR         -1    /**< Общая ошибка платформы */
#define KS_ERR_INVAL   -2    /**< Переданы неверные аргументы */
#define KS_ERR_NOMEM   -3    /**< Ошибка выделения памяти пула */
#define KS_ERR_CONN    -4    /**< В соединении отказано / узел недоступен */
#define KS_ERR_TIMEOUT -5    /**< Истекло время ожидания операции */

/* =========================================================================
 * 3. Типы данных
 * ========================================================================= */

/**
 * @brief Непрозрачный дескриптор сокета.
 * Внутренняя структура скрыта в платформенных исходниках (в internal.h).
 * Для пользователя API это просто указатель.
 */
typedef struct KS_SOCKET KS_SOCKET;

/* =========================================================================
 * 4. Базовый жизненный цикл (Функции: camelCase, Аргументы: PascalCase)
 * ========================================================================= */

/**
 * @brief Создает новый сокет (выделяет ресурсы ядра).
 * @param Protocol Транспортный протокол (KS_TCP или KS_UDP).
 * @return Указатель на структуру KS_SOCKET или NULL в случае нехватки памяти (NOMEM).
 */
KS_SOCKET* ksSocket(int Protocol);

/**
 * @brief Закрывает сокет и освобождает ресурсы ядра.
 * @param Sock Указатель на сокет, полученный из ksSocket или ksAccept.
 */
void ksClose(KS_SOCKET* Sock);

/* =========================================================================
 * 5. Серверные функции (для сценариев U->K и K->K)
 * ========================================================================= */

/**
 * @brief Привязывает сокет к локальному IP-адресу и порту.
 * @param Sock Указатель на сокет.
 * @param Ip Локальный IPv4-адрес строкой (например, "0.0.0.0").
 * @param Port Локальный порт в host byte order.
 * @return KS_OK при успехе, иначе негативный код ошибки.
 */
int ksBind(KS_SOCKET* Sock, const char* Ip, unsigned short Port);

/**
 * @brief Переводит сокет TCP в режим прослушивания входящих соединений.
 * @param Sock Указатель на привязанный сокет (после ksBind).
 * @param Backlog Максимальный размер очереди ожидающих подключений.
 * @return KS_OK при успехе, иначе код ошибки.
 */
int ksListen(KS_SOCKET* Sock, int Backlog);

/**
 * @brief Принимает входящее TCP-подключение (блокирующий вызов).
 * @param Sock Слушающий сокет (после ksListen).
 * @param NewSock [out] Возвращает новый сокет для общения с клиентом.
 * @param ClientIp [out] Буфер (мин. 16 байт) для записи IP клиента (может быть NULL).
 * @param ClientPort [out] Переменная для записи порта клиента (может быть NULL).
 * @return KS_OK при успехе. Возвращает KS_ERR при падении или закрытии сокета.
 */
int ksAccept(KS_SOCKET* Sock, KS_SOCKET** NewSock, char* ClientIp, unsigned short* ClientPort);

/* =========================================================================
 * 6. Клиентские и транспортные функции (TCP)
 * ========================================================================= */

/**
 * @brief Устанавливает TCP-соединение с удаленным узлом.
 * @param Sock Указатель на сокет (KS_TCP).
 * @param Ip IPv4-адрес сервера строкой ("192.168.1.100").
 * @param Port Порт сервера.
 * @return KS_OK или код ошибки (например, KS_ERR_CONN).
 */
int ksConnect(KS_SOCKET* Sock, const char* Ip, unsigned short Port);

/**
 * @brief Отправляет данные по TCP.
 * @param Sock Подключенный TCP-сокет.
 * @param Buf Указатель на буфер с данными (должен быть в NonPagedPool).
 * @param Len Количество байт для отправки (> 0).
 * @return Количество отправленных байт или код ошибки.
 */
int ksSend(KS_SOCKET* Sock, const void* Buf, unsigned int Len);

/**
 * @brief Принимает данные по TCP (блокирует до получения минимум 1 байта).
 * @param Sock Подключенный TCP-сокет.
 * @param Buf [out] Буфер для записи данных (NonPagedPool).
 * @param Len Размер буфера.
 * @return Количество прочитанных байт, 0 при закрытии соединения другой стороной, или ошибка.
 */
int ksRecv(KS_SOCKET* Sock, void* Buf, unsigned int Len);

/* =========================================================================
 * 7. Транспортные функции (UDP)
 * ========================================================================= */

/**
 * @brief Отправляет UDP-датаграмму заданному узлом.
 * @param Sock UDP-сокет.
 * @param Buf Указатель на данные.
 * @param Len Длина данных (не более ~65507 байт).
 * @param Ip IP-адрес получателя.
 * @param Port Порт получателя.
 * @return Количество отправленных байт или код ошибки.
 */
int ksSendTo(KS_SOCKET* Sock, const void* Buf, unsigned int Len, const char* Ip, unsigned short Port);

/**
 * @brief Принимает UDP-датаграмму и записывает адрес отправителя.
 * @param Sock UDP-сокет (обычно после ksBind).
 * @param Buf [out] Буфер для записи.
 * @param Len Размер буфера.
 * @param SrcIp [out] Буфер (мин. 16 байт) для IP отправителя (может быть NULL).
 * @param SrcPort [out] Порт отправителя (может быть NULL).
 * @return Количество полученных байт или код ошибки.
 */
int ksRecvFrom(KS_SOCKET* Sock, void* Buf, unsigned int Len, char* SrcIp, unsigned short* SrcPort);

#ifdef __cplusplus
}
#endif

#endif /* KS_API_H */
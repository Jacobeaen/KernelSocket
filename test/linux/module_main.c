/**
 * @file module_main.c
 * @brief KernelSocket — Комплексный тест TCP и UDP (Kernel-to-Kernel) для Linux.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h> 
#include <linux/delay.h>   
#include <linux/net.h>         
#include "ks_linux_internal.h" 
#include "../../include/ks_api.h"

#define TEST_PORT_TCP 9006
#define TEST_PORT_UDP 9007

MODULE_LICENSE("GPL");
MODULE_AUTHOR("KernelSocket Team");
MODULE_DESCRIPTION("KernelSocket Test Module - Full K2K");

/* Глобальные указатели на сокеты для экстренного завершения */
static KS_SOCKET *G_TcpServerSock = NULL;
static KS_SOCKET *G_UdpServerSock = NULL;

/* Глобальные указатели на потоки */
static struct task_struct *t_tcp_srv, *t_udp_srv, *t_tcp_cli, *t_udp_cli;

/* =========================================================================
 * Поток 1: Ядерный TCP-Сервер
 * ========================================================================= */
static int tcp_server_thread(void *arg) {
    KS_SOCKET *ClientSock;
    char ClientIp[16] = {0};
    unsigned short ClientPort = 0;
    char Buffer[128] = {0};
    int Bytes;

    pr_info("[KS-TCP-Srv] Инициализация TCP сервера...\n");
    G_TcpServerSock = ksSocket(KS_TCP);
    
    if (G_TcpServerSock && ksBind(G_TcpServerSock, "0.0.0.0", TEST_PORT_TCP) == KS_OK && 
        ksListen(G_TcpServerSock, 1) == KS_OK) {
        
        pr_info("[KS-TCP-Srv] УСПЕХ: Сервер слушает TCP порт %d\n", TEST_PORT_TCP);

        while (!kthread_should_stop()) {
            if (ksAccept(G_TcpServerSock, &ClientSock, ClientIp, &ClientPort) == KS_OK) {
                pr_info("[KS-TCP-Srv] Входящее подключение от -> %s:%d\n", ClientIp, ClientPort);

                Bytes = ksRecv(ClientSock, Buffer, sizeof(Buffer) - 1);
                if (Bytes > 0) {
                    Buffer[Bytes] = '\0';
                    pr_info("[KS-TCP-Srv] Прочитано %d байт: '%s'\n", Bytes, Buffer);
                    
                    ksSend(ClientSock, "Otvet ot Linux TCP Servera!", 27);
                    pr_info("[KS-TCP-Srv] Отправлен ответ.\n");
                    msleep(200); 
                }
                ksClose(ClientSock);
            } else {
                msleep(100); /* Ошибка accept или выгрузка модуля */
            }
        }
    }
    
    if (G_TcpServerSock) { ksClose(G_TcpServerSock); G_TcpServerSock = NULL; }
    pr_info("[KS-TCP-Srv] Поток завершен.\n");
    while (!kthread_should_stop()) msleep(100);
    return 0;
}

/* =========================================================================
 * Поток 2: Ядерный TCP-Клиент
 * ========================================================================= */
static int tcp_client_thread(void *arg) {
    KS_SOCKET *ClientSock;
    const char Msg[] = "Privet ot Linux TCP Klienta!";
    char Reply[128] = {0};
    int Bytes;

    msleep(2000); /* Ждем 2 сек, пока поднимется сервер */
    pr_info("[KS-TCP-Cli] Запуск клиента, подключение к %d...\n", TEST_PORT_TCP);
    
    ClientSock = ksSocket(KS_TCP);
    if (ClientSock) {
        if (ksConnect(ClientSock, "26.217.126.117", TEST_PORT_TCP) == KS_OK) {
            ksSend(ClientSock, Msg, sizeof(Msg));
            pr_info("[KS-TCP-Cli] УСПЕХ: Отправлено: '%s'\n", Msg);

            Bytes = ksRecv(ClientSock, Reply, sizeof(Reply) - 1);
            if (Bytes > 0) {
                Reply[Bytes] = '\0';
                pr_info("[KS-TCP-Cli] Получен эхо-ответ: '%s'\n", Reply);
            }
        } else {
            pr_info("[KS-TCP-Cli] ОШИБКА: Не удалось подключиться!\n");
        }
        ksClose(ClientSock);
    }
    
    pr_info("[KS-TCP-Cli] Поток завершен.\n");
    while (!kthread_should_stop()) msleep(100);
    return 0;
}

/* =========================================================================
 * Поток 3: Ядерный UDP-Сервер
 * ========================================================================= */
static int udp_server_thread(void *arg) {
    char ClientIp[16] = {0};
    unsigned short ClientPort = 0;
    char Buffer[128] = {0};
    int Bytes;

    pr_info("[KS-UDP-Srv] Инициализация UDP сервера...\n");
    G_UdpServerSock = ksSocket(KS_UDP);
    
    if (G_UdpServerSock && ksBind(G_UdpServerSock, "0.0.0.0", TEST_PORT_UDP) == KS_OK) {
        pr_info("[KS-UDP-Srv] УСПЕХ: Сервер слушает UDP порт %d\n", TEST_PORT_UDP);

        while (!kthread_should_stop()) {
            Bytes = ksRecvFrom(G_UdpServerSock, Buffer, sizeof(Buffer) - 1, ClientIp, &ClientPort);
            if (Bytes > 0) {
                Buffer[Bytes] = '\0';
                pr_info("[KS-UDP-Srv] Получено от %s:%d: '%s'\n", ClientIp, ClientPort, Buffer);
                
                ksSendTo(G_UdpServerSock, "Otvet ot Linux UDP Servera!", 27, ClientIp, ClientPort);
                pr_info("[KS-UDP-Srv] Отправлен ответ.\n");
            } else {
                msleep(100);
            }
        }
    }
    
    if (G_UdpServerSock) { ksClose(G_UdpServerSock); G_UdpServerSock = NULL; }
    pr_info("[KS-UDP-Srv] Поток завершен.\n");
    while (!kthread_should_stop()) msleep(100);
    return 0;
}

/* =========================================================================
 * Поток 4: Ядерный UDP-Клиент
 * ========================================================================= */
static int udp_client_thread(void *arg) {
    KS_SOCKET *ClientSock;
    const char Msg[] = "Privet ot Linux UDP Klienta!";
    char Reply[128] = {0};
    int Bytes;

    msleep(2500); /* Ждем 2.5 сек, пока поднимется сервер */
    pr_info("[KS-UDP-Cli] Запуск клиента, отправка на порт %d...\n", TEST_PORT_UDP);

    ClientSock = ksSocket(KS_UDP);
    if (ClientSock) {
        if (ksSendTo(ClientSock, Msg, sizeof(Msg), "26.217.126.117", TEST_PORT_UDP) > 0) {
            pr_info("[KS-UDP-Cli] УСПЕХ: Отправлено: '%s'\n", Msg);

            Bytes = ksRecvFrom(ClientSock, Reply, sizeof(Reply) - 1, NULL, NULL);
            if (Bytes > 0) {
                Reply[Bytes] = '\0';
                pr_info("[KS-UDP-Cli] Получен эхо-ответ: '%s'\n", Reply);
            }
        } else {
            pr_info("[KS-UDP-Cli] ОШИБКА: Не удалось отправить датаграмму!\n");
        }
        ksClose(ClientSock);
    }

    pr_info("[KS-UDP-Cli] Поток завершен.\n");
    while (!kthread_should_stop()) msleep(100);
    return 0;
}

/* =========================================================================
 * Точки входа (init / exit)
 * ========================================================================= */
static int __init ks_module_init(void) {
    pr_info("[KernelSocket] Загрузка модуля. Запуск 4 тестовых потоков...\n");

    /* Запускаем серверы */
    t_tcp_srv = kthread_run(tcp_server_thread, NULL, "ks_tcp_srv");
    t_udp_srv = kthread_run(udp_server_thread, NULL, "ks_udp_srv");
    
    /* Запускаем клиенты */
    // t_tcp_cli = kthread_run(tcp_client_thread, NULL, "ks_tcp_cli");
    // t_udp_cli = kthread_run(udp_client_thread, NULL, "ks_udp_cli");

    return 0;
}

static void __exit ks_module_exit(void) {
    pr_info("[KernelSocket] Начат процесс выгрузки модуля...\n");

    /* 1. Экстренно обрываем сетевые вызовы (разблокируем accept и recvfrom) */
    if (G_TcpServerSock && G_TcpServerSock->sock) {
        kernel_sock_shutdown(G_TcpServerSock->sock, SHUT_RDWR);
    }
    if (G_UdpServerSock && G_UdpServerSock->sock) {
        kernel_sock_shutdown(G_UdpServerSock->sock, SHUT_RDWR);
    }

    /* 2. Официально завершаем и удаляем потоки из памяти ядра */
    if (t_tcp_cli) kthread_stop(t_tcp_cli);
    if (t_udp_cli) kthread_stop(t_udp_cli);
    if (t_tcp_srv) kthread_stop(t_tcp_srv);
    if (t_udp_srv) kthread_stop(t_udp_srv);

    pr_info("[KernelSocket] Модуль успешно выгружен. Все порты свободны!\n");
}

module_init(ks_module_init);
module_exit(ks_module_exit);

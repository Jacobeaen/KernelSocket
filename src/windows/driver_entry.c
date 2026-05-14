/**
 * @file driver_entry.c
 * @brief KernelSocket — Комплексный тест: TCP и UDP (Kernel-to-Kernel / Kernel-to-User)
 */

#include <ntddk.h>
#include "ks_windows_internal.h"
#include "../../include/ks_api.h"

/* Конфигурация тестового стенда */
#define TEST_PORT_TCP 9000
#define TEST_PORT_UDP 9001
#define TARGET_IP     "26.175.255.229" /* Замените на 127.0.0.1 для локального теста */

/* =========================================================================
 * Поток 1: Ядерный TCP-Сервер
 * ========================================================================= */
static void TcpServerThread(PVOID Context) {
    KS_SOCKET* ServerSock;
    KS_SOCKET* ClientSock;
    char ClientIp[16] = { 0 };
    unsigned short ClientPort = 0;
    char Buffer[128] = { 0 };
    int Bytes;

    UNREFERENCED_PARAMETER(Context);

    DbgPrint("[TCP-Сервер]    Инициализация потока...\n");
    ServerSock = ksSocket(KS_TCP);
    if (!ServerSock) PsTerminateSystemThread(STATUS_SUCCESS);

    if (ksBind(ServerSock, "0.0.0.0", TEST_PORT_TCP) != KS_OK) {
        DbgPrint("[TCP-Сервер]    ОШИБКА: Порт %d занят!\n", TEST_PORT_TCP);
        ksClose(ServerSock);
        PsTerminateSystemThread(STATUS_SUCCESS);
    }

    if (ksListen(ServerSock, 1) != KS_OK) {
        DbgPrint("[TCP-Сервер]    ОШИБКА: Сбой при вызове ksListen.\n");
        ksClose(ServerSock);
        PsTerminateSystemThread(STATUS_SUCCESS);
    }

    DbgPrint("[TCP-Сервер]    Слушаю порт %d. Ожидание подключений...\n", TEST_PORT_TCP);

    /* БЕСКОНЕЧНЫЙ ЦИКЛ ОЖИДАНИЯ КЛИЕНТОВ */
    while (TRUE) {
        if (ksAccept(ServerSock, &ClientSock, ClientIp, &ClientPort) == KS_OK) {
            DbgPrint("[TCP-Сервер]    ---> Подключился клиент: %s:%d\n", ClientIp, ClientPort);

            Bytes = ksRecv(ClientSock, Buffer, sizeof(Buffer) - 1);
            if (Bytes > 0) {
                Buffer[Bytes] = '\0'; /* Очищаем хвосты строки */
                DbgPrint("[TCP-Сервер]    Получено: '%s' (%d байт)\n", Buffer, Bytes);

                const char ReplyMsg[] = "Ответ от TCP Сервера (Ядро Windows)";
                ksSend(ClientSock, ReplyMsg, sizeof(ReplyMsg));

                DbgPrint("[TCP-Сервер]    Отправлено: '%s'\n", ReplyMsg);

                /* Задержка для фикса Race Condition (доставка пакета перед закрытием) */
                LARGE_INTEGER Delay;
                Delay.QuadPart = -5000000LL; /* 0.5 секунды */
                KeDelayExecutionThread(KernelMode, FALSE, &Delay);
            }
            ksClose(ClientSock);
            DbgPrint("[TCP-Сервер]    Соединение с клиентом закрыто.\n");
        }
        else {
            /* Выход из цикла при выгрузке драйвера */
            DbgPrint("[TCP-Сервер]    Работа завершена (Ожидание прервано).\n");
            break;
        }
    }

    ksClose(ServerSock);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* =========================================================================
 * Поток 2: Ядерный TCP-Клиент
 * ========================================================================= */
static void TcpClientThread(PVOID Context) {
    KS_SOCKET* ClientSock;
    LARGE_INTEGER Timeout;
    const char Msg[] = "Тестовое сообщение от TCP Клиента (Ядро Windows)";
    char Reply[128] = { 0 };
    int Bytes;

    UNREFERENCED_PARAMETER(Context);

    /* Ждем 2 секунды, чтобы сервер успел подняться */
    Timeout.QuadPart = -20000000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &Timeout);

    DbgPrint("[TCP-Клиент]    Поток запущен. Подключение к %s:%d...\n", TARGET_IP, TEST_PORT_TCP);
    ClientSock = ksSocket(KS_TCP);
    if (!ClientSock) PsTerminateSystemThread(STATUS_SUCCESS);

    if (ksConnect(ClientSock, TARGET_IP, TEST_PORT_TCP) == KS_OK) {
        DbgPrint("[TCP-Клиент]    ---> Успешное подключение!\n");
        
        ksSend(ClientSock, Msg, sizeof(Msg));
        DbgPrint("[TCP-Клиент]    Отправлено: '%s'\n", Msg);

        Bytes = ksRecv(ClientSock, Reply, sizeof(Reply) - 1);
        if (Bytes > 0) {
            Reply[Bytes] = '\0';
            DbgPrint("[TCP-Клиент]    Получено: '%s' (%d байт)\n", Reply, Bytes);
        }
    }
    else {
        DbgPrint("[TCP-Клиент]    ОШИБКА: Не удалось подключиться к серверу.\n");
    }

    ksClose(ClientSock);
    DbgPrint("[TCP-Клиент]    Работа завершена.\n");
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* =========================================================================
 * Поток 3: Ядерный UDP-Сервер
 * ========================================================================= */
static void UdpServerThread(PVOID Context) {
    KS_SOCKET* ServerSock;
    char ClientIp[16] = { 0 };
    unsigned short ClientPort = 0;
    char Buffer[128] = { 0 };
    int Bytes;

    UNREFERENCED_PARAMETER(Context);

    DbgPrint("[UDP-Сервер]    Инициализация потока...\n");
    ServerSock = ksSocket(KS_UDP);
    if (!ServerSock) PsTerminateSystemThread(STATUS_SUCCESS);

    if (ksBind(ServerSock, "0.0.0.0", TEST_PORT_UDP) != KS_OK) {
        DbgPrint("[UDP-Сервер]    ОШИБКА: Порт %d занят!\n", TEST_PORT_UDP);
        ksClose(ServerSock);
        PsTerminateSystemThread(STATUS_SUCCESS);
    }

    DbgPrint("[UDP-Сервер]    Слушаю порт %d. Ожидание датаграмм...\n", TEST_PORT_UDP);

    /* БЕСКОНЕЧНЫЙ ЦИКЛ ОЖИДАНИЯ ДАТАГРАММ */
    while (TRUE) {
        Bytes = ksRecvFrom(ServerSock, Buffer, sizeof(Buffer) - 1, ClientIp, &ClientPort);
        if (Bytes > 0) {
            Buffer[Bytes] = '\0';
            DbgPrint("[UDP-Сервер]    ---> Получено от %s:%d: '%s'\n", ClientIp, ClientPort, Buffer);

            const char ReplyMsg[] = "Ответ от UDP Сервера (Ядро Windows)";
            ksSendTo(ServerSock, ReplyMsg, sizeof(ReplyMsg), ClientIp, ClientPort);

            DbgPrint("[UDP-Сервер]    Отправлено: '%s'\n", ReplyMsg);
        }
        else {
            DbgPrint("[UDP-Сервер]    Работа завершена (Ожидание прервано).\n");
            break;
        }
    }

    ksClose(ServerSock);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* =========================================================================
 * Поток 4: Ядерный UDP-Клиент
 * ========================================================================= */
static void UdpClientThread(PVOID Context) {
    KS_SOCKET* ClientSock;
    LARGE_INTEGER Timeout;
    const char Msg[] = "Тестовая датаграмма от UDP Клиента (Ядро Windows)";
    char Reply[128] = { 0 };
    int Bytes;

    UNREFERENCED_PARAMETER(Context);

    /* Ждем 2.5 секунды (чтобы TCP успел отработать и логи не перемешались) */
    Timeout.QuadPart = -25000000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &Timeout);

    DbgPrint("[UDP-Клиент]    Поток запущен. Отправка на %s:%d...\n", TARGET_IP, TEST_PORT_UDP);
    ClientSock = ksSocket(KS_UDP);
    if (!ClientSock) PsTerminateSystemThread(STATUS_SUCCESS);

    if (ksSendTo(ClientSock, Msg, sizeof(Msg), TARGET_IP, TEST_PORT_UDP) > 0) {
        DbgPrint("[UDP-Клиент]    Отправлено: '%s'\n", Msg);

        Bytes = ksRecvFrom(ClientSock, Reply, sizeof(Reply) - 1, NULL, NULL);
        if (Bytes > 0) {
            Reply[Bytes] = '\0';
            DbgPrint("[UDP-Клиент]    Получено: '%s'\n", Reply);
        }
    }
    else {
        DbgPrint("[UDP-Клиент]    ОШИБКА: Сбой при отправке датаграммы.\n");
    }

    ksClose(ClientSock);
    DbgPrint("[UDP-Клиент]    Работа завершена.\n");
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* =========================================================================
 * Точки входа
 * ========================================================================= */
DRIVER_UNLOAD KsDriverUnload;

void KsDriverUnload(_In_ PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);
    KsWskCleanup();
    DbgPrint("[KernelSocket]  Драйвер успешно выгружен.\n");
}

DRIVER_INITIALIZE DriverEntry;

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
    NTSTATUS Status;
    HANDLE h1, h2, h3, h4;

    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrint("[KernelSocket]  =============================================\n");
    DbgPrint("[KernelSocket]  Загрузка драйвера. Запуск тестовых потоков...\n");
    DriverObject->DriverUnload = KsDriverUnload;

    Status = KsWskInit();
    if (!NT_SUCCESS(Status)) {
        DbgPrint("[KernelSocket]  ОШИБКА: KsWskInit вернул 0x%08X\n", Status);
        return Status;
    }

    /* Запускаем серверы */
    if (NT_SUCCESS(PsCreateSystemThread(&h1, THREAD_ALL_ACCESS, NULL, NULL, NULL, TcpServerThread, NULL))) ZwClose(h1);
    if (NT_SUCCESS(PsCreateSystemThread(&h3, THREAD_ALL_ACCESS, NULL, NULL, NULL, UdpServerThread, NULL))) ZwClose(h3);

    /* Запускаем клиенты (Закомментируйте эти 2 строки для работы только в режиме сервера) */
    if (NT_SUCCESS(PsCreateSystemThread(&h2, THREAD_ALL_ACCESS, NULL, NULL, NULL, TcpClientThread, NULL))) ZwClose(h2);
    if (NT_SUCCESS(PsCreateSystemThread(&h4, THREAD_ALL_ACCESS, NULL, NULL, NULL, UdpClientThread, NULL))) ZwClose(h4);

    DbgPrint("[KernelSocket]  Все потоки запущены и работают в фоне.\n");
    DbgPrint("[KernelSocket]  =============================================\n");
    return STATUS_SUCCESS;
}
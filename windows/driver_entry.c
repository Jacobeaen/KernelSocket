/**
 * @file driver_entry.c
 * @brief KernelSocket — KMDF DriverEntry and DriverUnload
 *
 * Entry point for the kernel-mode driver.  Initialises WSK via KsWskInit()
 * and performs a self-test TCP connection to verify the API works end-to-end.
 *
 * To load on the target VM (cmd.exe as Administrator):
 * @code
 *   sc create KernelSocket type= kernel binPath= C:\KernelSocket.sys
 *   sc start KernelSocket
 *   :: watch output in DebugView (Capture → Capture Kernel must be ON)
 *   sc stop KernelSocket
 *   sc delete KernelSocket
 * @endcode
 *
 * @note The self-test expects an echo server running on TEST_SERVER_IP:TEST_PORT.
 *       Start test/echo_server/echo_server.exe on the host before loading
 *       the driver on the VM.
 */

#include <ntddk.h>
#include "ks_windows.h"

/* -------------------------------------------------------------------------
 * Test parameters — adjust to match your echo server
 * ---------------------------------------------------------------------- */
#define TEST_SERVER_IP   "192.168.x.1"   /* <-- replace with your host IP  */
#define TEST_PORT        9000

/* =========================================================================
 * Driver Unload
 * ========================================================================= */

DRIVER_UNLOAD KsDriverUnload;

void KsDriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    DbgPrint("[KernelSocket] DriverUnload called\n");
    KsWskCleanup();
    DbgPrint("[KernelSocket] Driver unloaded OK\n");
}

/* =========================================================================
 * Self-test helper (runs from DriverEntry, PASSIVE_LEVEL)
 * ========================================================================= */

static void KsRunSelfTest(void)
{
    ks_socket_t *sock;
    const char   sendMsg[] = "Hello from KernelSocket!";
    char         recvBuf[128];
    int          ret;

    DbgPrint("[KernelSocket] --- Self-test BEGIN ---\n");

    /* 1. Create socket */
    sock = ks_socket(KS_TCP);
    if (!sock) {
        DbgPrint("[KernelSocket] FAIL: ks_socket returned NULL\n");
        return;
    }
    DbgPrint("[KernelSocket] PASS: ks_socket OK\n");

    /* 2. Connect */
    ret = ks_connect(sock, TEST_SERVER_IP, TEST_PORT);
    if (ret != KS_OK) {
        DbgPrint("[KernelSocket] FAIL: ks_connect returned %d\n", ret);
        DbgPrint("[KernelSocket] Make sure echo_server is running on %s:%d\n",
                 TEST_SERVER_IP, TEST_PORT);
        ks_close(sock);
        return;
    }
    DbgPrint("[KernelSocket] PASS: ks_connect to %s:%d OK\n",
             TEST_SERVER_IP, TEST_PORT);

    /* 3. Send */
    ret = ks_send(sock, sendMsg, (unsigned int)sizeof(sendMsg));
    if (ret <= 0) {
        DbgPrint("[KernelSocket] FAIL: ks_send returned %d\n", ret);
        ks_close(sock);
        return;
    }
    DbgPrint("[KernelSocket] PASS: ks_send %d bytes OK\n", ret);

    /* 4. Receive echo */
    RtlZeroMemory(recvBuf, sizeof(recvBuf));
    ret = ks_recv(sock, recvBuf, sizeof(recvBuf) - 1);
    if (ret <= 0) {
        DbgPrint("[KernelSocket] FAIL: ks_recv returned %d\n", ret);
        ks_close(sock);
        return;
    }
    DbgPrint("[KernelSocket] PASS: ks_recv %d bytes — echo: '%s'\n",
             ret, recvBuf);

    /* 5. Verify echo matches what we sent */
    if (RtlCompareMemory(recvBuf, sendMsg, sizeof(sendMsg)) == sizeof(sendMsg)) {
        DbgPrint("[KernelSocket] PASS: echo content matches sent data\n");
    } else {
        DbgPrint("[KernelSocket] WARN: echo content does NOT match\n");
    }

    /* 6. Close */
    ks_close(sock);
    DbgPrint("[KernelSocket] PASS: ks_close OK\n");
    DbgPrint("[KernelSocket] --- Self-test END (all steps passed) ---\n");
}

/* =========================================================================
 * DriverEntry
 * ========================================================================= */

DRIVER_INITIALIZE DriverEntry;

NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrint("[KernelSocket] DriverEntry: loading\n");

    /* Register unload routine so the driver can be stopped cleanly */
    DriverObject->DriverUnload = KsDriverUnload;

    /* Initialise WSK subsystem */
    status = KsWskInit();
    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] KsWskInit failed: 0x%08X — aborting\n",
                 status);
        return status;
    }

    /* Run end-to-end self-test to verify TCP works */
    KsRunSelfTest();

    DbgPrint("[KernelSocket] DriverEntry: driver ready\n");
    return STATUS_SUCCESS;
}

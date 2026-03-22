/**
 * @file driver_entry.c
 * @brief KernelSocket — KMDF DriverEntry and built-in self-tests
 *
 * Provides the mandatory DriverEntry and DriverUnload callbacks for the
 * KernelSocket kernel-mode driver. After initialising the WSK subsystem,
 * it immediately runs a TCP echo test and a UDP echo test to verify that
 * the ks_* API functions end-to-end.
 *
 * ## Loading the driver on the target VM
 *
 * The VM must have test-signing enabled:
 * @code
 *   bcdedit /set testsigning on
 *   bcdedit /set nointegritychecks on
 *   :: reboot the VM
 * @endcode
 *
 * Then, from an elevated command prompt:
 * @code
 *   sc create KernelSocket type= kernel binPath= C:\Drivers\KernelSocket.sys
 *   sc start  KernelSocket
 *   :: observe output in DebugView (Capture > Capture Kernel must be ON)
 *   sc stop   KernelSocket
 *   sc delete KernelSocket
 * @endcode
 *
 * ## Prerequisites for the self-tests
 *
 * Start test/echo_server/echo_server.exe on the host machine before loading
 * the driver. The server listens on TCP port @c TEST_PORT_TCP and UDP port
 * @c TEST_PORT_UDP simultaneously.
 *
 * @note Adjust TEST_SERVER_IP to match the host's VMware adapter address
 *       (check with @c ipconfig on the host — look for VMnet8 or VMnet1).
 */

#include <ntddk.h>
#include "ks_windows.h"

 /* =========================================================================
  * Self-test configuration
  * ========================================================================= */

  /** IPv4 address of the machine running echo_server.exe. */
#define TEST_SERVER_IP   "192.168.56.1"

/** TCP echo port — echo_server listens here. */
#define TEST_PORT_TCP    9000

/** UDP echo port — echo_server listens here (TEST_PORT_TCP + 1 by default). */
#define TEST_PORT_UDP    9001

/* =========================================================================
 * TCP self-test
 * ========================================================================= */

 /**
  * @brief Run a TCP echo round-trip using the ks_* API.
  *
  * Creates a TCP socket, connects to the echo server, sends a message,
  * reads the echo, compares the content, then closes the socket. All steps
  * are logged via DbgPrint (visible in DebugView).
  */
static void KsTestTcp(void)
{
    ks_socket_t* sock;
    const char   msg[] = "Hello from KernelSocket TCP!";
    char         reply[128] = { 0 };
    int          ret;

    DbgPrint("[KernelSocket] ========== TCP test ==========\n");

    /* 1 — create socket */
    sock = ks_socket(KS_TCP);
    if (!sock) {
        DbgPrint("[KernelSocket] FAIL ks_socket(KS_TCP)\n");
        return;
    }
    DbgPrint("[KernelSocket] PASS ks_socket\n");

    /* 2 — connect */
    ret = ks_connect(sock, TEST_SERVER_IP, TEST_PORT_TCP);
    if (ret != KS_OK) {
        DbgPrint("[KernelSocket] FAIL ks_connect -> %d  "
            "(is echo_server running on %s:%d?)\n",
            ret, TEST_SERVER_IP, TEST_PORT_TCP);
        ks_close(sock);
        return;
    }
    DbgPrint("[KernelSocket] PASS ks_connect to %s:%d\n",
        TEST_SERVER_IP, TEST_PORT_TCP);

    /* 3 — send */
    ret = ks_send(sock, msg, (unsigned int)sizeof(msg));
    if (ret <= 0) {
        DbgPrint("[KernelSocket] FAIL ks_send -> %d\n", ret);
        ks_close(sock);
        return;
    }
    DbgPrint("[KernelSocket] PASS ks_send %d bytes\n", ret);

    /* 4 — receive echo */
    ret = ks_recv(sock, reply, sizeof(reply) - 1);
    if (ret <= 0) {
        DbgPrint("[KernelSocket] FAIL ks_recv -> %d\n", ret);
        ks_close(sock);
        return;
    }
    DbgPrint("[KernelSocket] PASS ks_recv %d bytes: '%s'\n", ret, reply);

    /* 5 — verify content */
    if (RtlCompareMemory(reply, msg, sizeof(msg)) == sizeof(msg))
        DbgPrint("[KernelSocket] PASS echo content matches\n");
    else
        DbgPrint("[KernelSocket] WARN echo content mismatch\n");

    /* 6 — close */
    ks_close(sock);
    DbgPrint("[KernelSocket] PASS ks_close\n");
    DbgPrint("[KernelSocket] TCP test COMPLETE\n");
}

/* =========================================================================
 * UDP self-test
 * ========================================================================= */

 /**
  * @brief Run a UDP echo round-trip using the ks_* API.
  *
  * Creates a UDP socket, sends a datagram to the echo server via ks_sendto(),
  * receives the echoed datagram via ks_recvfrom(), logs the sender address,
  * then closes the socket.
  */
static void KsTestUdp(void)
{
    ks_socket_t* sock;
    const char    msg[] = "Hello from KernelSocket UDP!";
    char          reply[128] = { 0 };
    char          src_ip[16] = { 0 };
    unsigned short src_port = 0;
    int           ret;

    DbgPrint("[KernelSocket] ========== UDP test ==========\n");

    /* 1 — create socket */
    sock = ks_socket(KS_UDP);
    if (!sock) {
        DbgPrint("[KernelSocket] FAIL ks_socket(KS_UDP)\n");
        return;
    }
    DbgPrint("[KernelSocket] PASS ks_socket\n");

    /* 2 — send datagram (no connect needed for UDP) */
    ret = ks_sendto(sock, msg, (unsigned int)sizeof(msg),
        TEST_SERVER_IP, TEST_PORT_UDP);
    if (ret <= 0) {
        DbgPrint("[KernelSocket] FAIL ks_sendto -> %d\n", ret);
        ks_close(sock);
        return;
    }
    DbgPrint("[KernelSocket] PASS ks_sendto %d bytes to %s:%d\n",
        ret, TEST_SERVER_IP, TEST_PORT_UDP);

    /* 3 — receive echo */
    ret = ks_recvfrom(sock, reply, sizeof(reply) - 1, src_ip, &src_port);
    if (ret <= 0) {
        DbgPrint("[KernelSocket] FAIL ks_recvfrom -> %d\n", ret);
        ks_close(sock);
        return;
    }
    DbgPrint("[KernelSocket] PASS ks_recvfrom %d bytes from %s:%u: '%s'\n",
        ret, src_ip, src_port, reply);

    /* 4 — close */
    ks_close(sock);
    DbgPrint("[KernelSocket] PASS ks_close\n");
    DbgPrint("[KernelSocket] UDP test COMPLETE\n");
}

/* =========================================================================
 * DriverUnload
 * ========================================================================= */

DRIVER_UNLOAD KsDriverUnload;

/**
 * @brief Release all driver resources.
 *
 * Called by the I/O manager when the driver is stopped via sc.exe or
 * Device Manager. Releases the WSK provider and deregisters the client.
 *
 * @param[in] DriverObject  The driver's DRIVER_OBJECT (unused here).
 */
void KsDriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    DbgPrint("[KernelSocket] DriverUnload\n");
    KsWskCleanup();
    DbgPrint("[KernelSocket] Driver unloaded\n");
}

/* =========================================================================
 * DriverEntry
 * ========================================================================= */

DRIVER_INITIALIZE DriverEntry;

/**
 * @brief Driver entry point — initialise WSK and run self-tests.
 *
 * @param[in] DriverObject   Supplied by the kernel; used to set DriverUnload.
 * @param[in] RegistryPath   Path to the driver's registry key (unused).
 * @return                   STATUS_SUCCESS, or an NTSTATUS error if WSK init
 *                           fails (driver will not load in that case).
 */
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrint("[KernelSocket] DriverEntry: loading\n");

    DriverObject->DriverUnload = KsDriverUnload;

    /* Initialise WSK — must succeed before any ks_* call. */
    status = KsWskInit();
    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] KsWskInit failed 0x%08X — aborting\n",
            status);
        return status;
    }

    /* Run end-to-end self-tests. */
    KsTestTcp();
    KsTestUdp();

    DbgPrint("[KernelSocket] DriverEntry: driver ready\n");
    return STATUS_SUCCESS;
}
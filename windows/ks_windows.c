/**
 * @file ks_windows.c
 * @brief KernelSocket — Windows kernel-mode TCP/UDP implementation via WSK
 *
 * Implements the platform-agnostic ks_* API declared in ks_api.h using the
 * Winsock Kernel (WSK) provider interface available in Windows kernel mode.
 *
 * ## Architecture
 *
 * All WSK operations are inherently asynchronous and IRP-based. Every public
 * function in this file wraps its WSK calls synchronously: an IRP is allocated,
 * a KEVENT-based completion routine is attached, the operation is dispatched,
 * and the calling thread blocks on the event until the IRP completes. This
 * gives callers a simple blocking API identical in feel to POSIX sockets.
 *
 * ## WSK socket types
 *
 * | Protocol | WSK flag                    | Dispatch table                      |
 * |----------|-----------------------------|-------------------------------------|
 * | TCP      | WSK_FLAG_CONNECTION_SOCKET  | WSK_PROVIDER_CONNECTION_DISPATCH    |
 * | UDP      | WSK_FLAG_DATAGRAM_SOCKET    | WSK_PROVIDER_DATAGRAM_DISPATCH      |
 *
 * WskCloseSocket is available on all socket types through the common
 * WSK_PROVIDER_BASIC_DISPATCH (first member of every dispatch struct).
 * WskBind, however, is NOT in BASIC_DISPATCH — it must be called through
 * the protocol-specific dispatch table.
 *
 * ## Initialisation order
 *
 * Call KsWskInit() from DriverEntry before any ks_* function.
 * Call KsWskCleanup() from DriverUnload.
 *
 * @note All ks_* functions must be called at IRQL == PASSIVE_LEVEL.
 */

#include "ks_windows.h"

 /* netio.lib provides WskRegister / WskCaptureProviderNPI / WskDeregister /
  * WskReleaseProviderNPI.  #pragma comment works for user-mode projects but
  * is ignored by the WDK linker — add netio.lib via project Properties →
  * Linker → Input → Additional Dependencies instead. */
#pragma comment(lib, "netio.lib")

  /* =========================================================================
   * Module-level WSK state
   * ========================================================================= */

   /** WSK client dispatch table — version 1.0, no client-level event callbacks. */
static const WSK_CLIENT_DISPATCH g_WskClientDispatch = {
    MAKE_WSK_VERSION(1, 0),
    0,
    NULL
};

/** Handle filled by WskRegister(); used by cleanup and provider capture. */
WSK_REGISTRATION  g_WskRegistration;

/** Provider NPI filled by WskCaptureProviderNPI(); holds the dispatch table. */
WSK_PROVIDER_NPI  g_WskProvider;

/** Set to TRUE once KsWskInit() succeeds; guards all ks_* entry points. */
BOOLEAN           g_WskReady = FALSE;

/* =========================================================================
 * WSK subsystem initialisation and cleanup
 * ========================================================================= */

 /**
  * @brief Register the WSK client and capture the provider NPI.
  *
  * Must be called once from DriverEntry at PASSIVE_LEVEL before any ks_*
  * call. Blocks until the network stack signals it is ready.
  *
  * @return STATUS_SUCCESS, or an NTSTATUS error code on failure.
  */
NTSTATUS KsWskInit(void)
{
    WSK_CLIENT_NPI clientNpi;
    NTSTATUS       status;

    clientNpi.ClientContext = NULL;
    clientNpi.Dispatch = &g_WskClientDispatch;

    status = WskRegister(&clientNpi, &g_WskRegistration);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] WskRegister failed: 0x%08X\n", status);
        return status;
    }

    /* WSK_INFINITE_WAIT: block until the network stack is fully available.
     * Replace with a timeout if DriverEntry must not stall for too long. */
    status = WskCaptureProviderNPI(&g_WskRegistration,
        WSK_INFINITE_WAIT,
        &g_WskProvider);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] WskCaptureProviderNPI failed: 0x%08X\n",
            status);
        WskDeregister(&g_WskRegistration);
        return status;
    }

    g_WskReady = TRUE;
    DbgPrint("[KernelSocket] WSK provider ready (v%u.%u)\n",
        WSK_MAJOR_VERSION(g_WskProvider.Dispatch->Version),
        WSK_MINOR_VERSION(g_WskProvider.Dispatch->Version));
    return STATUS_SUCCESS;
}

/**
 * @brief Release the WSK provider and deregister the client.
 *
 * Must be called from DriverUnload. WskDeregister blocks until all pending
 * WSK IRPs are complete, so it is safe to call without extra synchronisation.
 */
void KsWskCleanup(void)
{
    if (!g_WskReady)
        return;

    WskReleaseProviderNPI(&g_WskRegistration);
    WskDeregister(&g_WskRegistration);
    g_WskReady = FALSE;
    DbgPrint("[KernelSocket] WSK provider released\n");
}

/* =========================================================================
 * IRP helper infrastructure
 *
 * Pattern used throughout this file:
 *   1. KsAllocateIrp / KsAllocateIrpEx  — allocate IRP + init context
 *   2. Dispatch the WSK operation (WskConnect, WskSend, …)
 *   3. KsWaitIrp (or manual wait for the _Ex variant)  — block + free IRP
 * ========================================================================= */

 /**
  * @brief IRP completion routine for synchronous WSK wrappers.
  *
  * Copies the final NTSTATUS from the IRP into the caller's KS_IRP_CTX and
  * signals its event. Returns STATUS_MORE_PROCESSING_REQUIRED to prevent the
  * I/O manager from freeing the IRP before KsWaitIrp() does so.
  *
  * The typedef IO_COMPLETION_ROUTINE in wdm.h declares Context as _In_opt_.
  * Our Context is always non-NULL, so __analysis_assume suppresses the
  * resulting C28182 "potential NULL dereference" warning from PREfast.
  */
static NTSTATUS NTAPI
KsIrpCompletionRoutine(
    _In_     PDEVICE_OBJECT DeviceObject,
    _In_     PIRP           Irp,
    _In_opt_ PVOID          Context)
{
    KS_IRP_CTX* ctx;
    UNREFERENCED_PARAMETER(DeviceObject);
    __analysis_assume(Context != NULL);
    ctx = (KS_IRP_CTX*)Context;
    ctx->Status = Irp->IoStatus.Status;
    KeSetEvent(&ctx->Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/**
 * @brief Allocate an IRP and wire it to KsIrpCompletionRoutine.
 *
 * Stack size of 1 is sufficient: WSK does not use IRP stack locations
 * beyond the first.
 *
 * @param[out] Ctx  Caller-allocated context (stack allocation is fine).
 * @return          New IRP on success, NULL on allocation failure.
 */
PIRP KsAllocateIrp(KS_IRP_CTX* Ctx)
{
    PIRP irp;

    KeInitializeEvent(&Ctx->Event, NotificationEvent, FALSE);
    Ctx->Status = STATUS_UNSUCCESSFUL;

    irp = IoAllocateIrp(1, FALSE);
    if (!irp) {
        DbgPrint("[KernelSocket] IoAllocateIrp failed\n");
        return NULL;
    }

    IoSetCompletionRoutine(irp, KsIrpCompletionRoutine, Ctx,
        TRUE, TRUE, TRUE);
    return irp;
}

/**
 * @brief Wait for a dispatched IRP to complete, free it, and return status.
 *
 * Must be called at PASSIVE_LEVEL. The IRP must have been allocated with
 * KsAllocateIrp() and already dispatched to a WSK function.
 *
 * @param[in] Irp  The IRP to wait on.
 * @param[in] Ctx  The context passed to KsAllocateIrp().
 * @return         NTSTATUS from Irp->IoStatus.Status.
 */
NTSTATUS KsWaitIrp(PIRP Irp, KS_IRP_CTX* Ctx)
{
    KeWaitForSingleObject(&Ctx->Event, Executive, KernelMode, FALSE, NULL);
    IoFreeIrp(Irp);
    return Ctx->Status;
}

/**
 * @brief Extended IRP context that also captures IoStatus.Information.
 *
 * Used by ks_socket() (to retrieve the new PWSK_SOCKET pointer) and by
 * ks_recv() / ks_recvfrom() (to retrieve the actual byte count).
 */
typedef struct _KS_IRP_CTX_EX {
    KS_IRP_CTX Base;         /**< Must be first — reused by KsWaitIrp. */
    ULONG_PTR  Information;  /**< Copy of Irp->IoStatus.Information.   */
} KS_IRP_CTX_EX;

/** Completion routine for the extended context. */
static NTSTATUS NTAPI
KsIrpCompletionEx(
    _In_     PDEVICE_OBJECT DeviceObject,
    _In_     PIRP           Irp,
    _In_opt_ PVOID          Context)
{
    KS_IRP_CTX_EX* ctx;
    UNREFERENCED_PARAMETER(DeviceObject);
    __analysis_assume(Context != NULL);
    ctx = (KS_IRP_CTX_EX*)Context;
    ctx->Base.Status = Irp->IoStatus.Status;
    ctx->Information = Irp->IoStatus.Information;
    KeSetEvent(&ctx->Base.Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/** Allocate an IRP wired to KsIrpCompletionEx. */
static PIRP KsAllocateIrpEx(KS_IRP_CTX_EX* Ctx)
{
    PIRP irp;

    KeInitializeEvent(&Ctx->Base.Event, NotificationEvent, FALSE);
    Ctx->Base.Status = STATUS_UNSUCCESSFUL;
    Ctx->Information = 0;

    irp = IoAllocateIrp(1, FALSE);
    if (!irp)
        return NULL;

    IoSetCompletionRoutine(irp, KsIrpCompletionEx, Ctx,
        TRUE, TRUE, TRUE);
    return irp;
}

/* =========================================================================
 * Internal helper — convert ASCII IPv4 string + port to SOCKADDR_IN
 * ========================================================================= */

 /**
  * @brief Fill a SOCKADDR_IN from an ASCII IPv4 string and a host-order port.
  *
  * Widens the ASCII string to UTF-16 with a simple per-character cast (safe
  * for IPv4 addresses which contain only ASCII digits and dots), then calls
  * RtlIpv4StringToAddressW. This avoids the C4242 narrowing warning that
  * RtlAnsiStringToUnicodeString triggers due to size_t → ULONG conversion.
  *
  * The third argument of RtlIpv4StringToAddressW must not be NULL per its
  * SAL annotation; we capture @p terminator even though we do not use it.
  *
  * @param[in]  ip    Null-terminated IPv4 address string, e.g. "192.168.1.1".
  * @param[in]  port  Destination port in host byte order.
  * @param[out] out   Caller-allocated SOCKADDR_IN to fill.
  * @return           STATUS_SUCCESS or STATUS_INVALID_PARAMETER.
  */
static NTSTATUS
KsStringToSockaddr(
    _In_  const char* ip,
    _In_  unsigned short port,
    _Out_ SOCKADDR_IN* out)
{
    WCHAR  wideIp[16] = { 0 };
    PCWSTR terminator;
    int    i;

    for (i = 0; i < 15 && ip[i] != '\0'; i++)
        wideIp[i] = (WCHAR)(unsigned char)ip[i];

    if (!NT_SUCCESS(RtlIpv4StringToAddressW(wideIp, TRUE, &terminator,
        (struct in_addr*)&out->sin_addr))) {
        DbgPrint("[KernelSocket] KsStringToSockaddr: invalid IP '%s'\n", ip);
        return STATUS_INVALID_PARAMETER;
    }

    out->sin_family = AF_INET;
    out->sin_port = RtlUshortByteSwap(port); /* host-to-network byte order */
    return STATUS_SUCCESS;
}

/* =========================================================================
 * Public API — ks_socket()
 * ========================================================================= */

 /**
  * @brief Allocate and initialise a kernel-mode socket.
  *
  * Calls WskSocket to create the WSK socket object, then WskBind to bind it
  * to INADDR_ANY:0. WskBind is mandatory before WskConnect (TCP) or
  * WskSendTo (UDP) even when the local endpoint is unimportant.
  *
  * WskBind resides in WSK_PROVIDER_CONNECTION_DISPATCH for TCP and in
  * WSK_PROVIDER_DATAGRAM_DISPATCH for UDP — it is NOT in BASIC_DISPATCH —
  * so the correct dispatch table is selected via the @p protocol parameter.
  *
  * @param[in] protocol  KS_TCP or KS_UDP.
  * @return              Pointer to a new ks_socket_t, or NULL on failure.
  */
ks_socket_t* ks_socket(int protocol)
{
    ks_socket_t* ks;
    KS_IRP_CTX_EX                ctxEx;
    KS_IRP_CTX                   ctx;
    PIRP                         irp;
    NTSTATUS                     status;
    SOCKADDR_IN                  localAddr;
    USHORT                       socketType;
    ULONG                        ipProto;
    ULONG                        wskFlags;
    const WSK_PROVIDER_DISPATCH* provDisp;

    if (!g_WskReady) {
        DbgPrint("[KernelSocket] ks_socket: WSK not initialised\n");
        return NULL;
    }
    if (protocol != KS_TCP && protocol != KS_UDP) {
        DbgPrint("[KernelSocket] ks_socket: unknown protocol %d\n", protocol);
        return NULL;
    }

    /* ExAllocatePool2 zeroes the allocation automatically. */
    ks = (ks_socket_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED,
        sizeof(ks_socket_t), 'ksKS');
    if (!ks) {
        DbgPrint("[KernelSocket] ks_socket: ExAllocatePool2 failed\n");
        return NULL;
    }

    ks->Protocol = protocol;

    if (protocol == KS_TCP) {
        socketType = (USHORT)SOCK_STREAM;
        ipProto = IPPROTO_TCP;
        wskFlags = WSK_FLAG_CONNECTION_SOCKET;
    }
    else {
        socketType = (USHORT)SOCK_DGRAM;
        ipProto = IPPROTO_UDP;
        wskFlags = WSK_FLAG_DATAGRAM_SOCKET;
    }

    provDisp = g_WskProvider.Dispatch;

    /* ------------------------------------------------------------------
     * Step 1 — WskSocket
     * The new PWSK_SOCKET is returned in Irp->IoStatus.Information, so
     * we use KsAllocateIrpEx to capture it before the IRP is freed.
     * ------------------------------------------------------------------ */
    irp = KsAllocateIrpEx(&ctxEx);
    if (!irp)
        goto fail_free;

    status = provDisp->WskSocket(
        g_WskProvider.Client,
        AF_INET, socketType, ipProto, wskFlags,
        NULL,   /* socket context  — unused */
        NULL,   /* dispatch table  — no event callbacks */
        NULL,   /* owning process  — inherit from caller */
        NULL,   /* owning thread   — inherit from caller */
        NULL,   /* security descriptor */
        irp);

    KeWaitForSingleObject(&ctxEx.Base.Event, Executive, KernelMode,
        FALSE, NULL);
    IoFreeIrp(irp);

    status = ctxEx.Base.Status;
    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] WskSocket failed: 0x%08X\n", status);
        goto fail_free;
    }

    ks->WskSocket = (PWSK_SOCKET)ctxEx.Information;

    /* ------------------------------------------------------------------
     * Step 2 — WskBind to INADDR_ANY:0
     * Must use WSK_PROVIDER_CONNECTION_DISPATCH for TCP or
     * WSK_PROVIDER_DATAGRAM_DISPATCH for UDP — not BASIC_DISPATCH.
     * ------------------------------------------------------------------ */
    RtlZeroMemory(&localAddr, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = 0;
    localAddr.sin_addr.S_un.S_addr = INADDR_ANY;

    irp = KsAllocateIrp(&ctx);
    if (!irp)
        goto fail_close;

    if (protocol == KS_TCP) {
        const WSK_PROVIDER_CONNECTION_DISPATCH* d =
            (const WSK_PROVIDER_CONNECTION_DISPATCH*)ks->WskSocket->Dispatch;
        status = d->WskBind(ks->WskSocket, (PSOCKADDR)&localAddr, 0, irp);
    }
    else {
        const WSK_PROVIDER_DATAGRAM_DISPATCH* d =
            (const WSK_PROVIDER_DATAGRAM_DISPATCH*)ks->WskSocket->Dispatch;
        status = d->WskBind(ks->WskSocket, (PSOCKADDR)&localAddr, 0, irp);
    }

    status = KsWaitIrp(irp, &ctx);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] WskBind failed: 0x%08X\n", status);
        goto fail_close;
    }

    DbgPrint("[KernelSocket] ks_socket: created (%s)\n",
        protocol == KS_TCP ? "TCP" : "UDP");
    return ks;

fail_close:
    {
        /* WskCloseSocket is available on all socket types via BASIC_DISPATCH. */
        const WSK_PROVIDER_BASIC_DISPATCH* bd =
            (const WSK_PROVIDER_BASIC_DISPATCH*)ks->WskSocket->Dispatch;
        irp = KsAllocateIrp(&ctx);
        if (irp) {
            bd->WskCloseSocket(ks->WskSocket, irp);
            KsWaitIrp(irp, &ctx);
        }
    }
fail_free:
    ExFreePoolWithTag(ks, 'ksKS');
    return NULL;
}

/* =========================================================================
 * Public API — ks_connect()   [TCP only]
 * ========================================================================= */

 /**
  * @brief Connect a TCP socket to a remote host.
  *
  * Performs a synchronous TCP three-way handshake via WskConnect.
  * ks_socket() must have been called with KS_TCP and must have succeeded
  * before calling this function.
  *
  * @param[in] sock  TCP socket from ks_socket(KS_TCP).
  * @param[in] ip    Remote IPv4 address as an ASCII string.
  * @param[in] port  Remote port in host byte order.
  * @return          KS_OK on success, or a negative KS_ERR_* code.
  */
int ks_connect(ks_socket_t* sock, const char* ip, unsigned short port)
{
    const WSK_PROVIDER_CONNECTION_DISPATCH* connDisp;
    SOCKADDR_IN remoteAddr;
    KS_IRP_CTX  ctx;
    PIRP        irp;
    NTSTATUS    status;

    if (!sock || !ip || port == 0)
        return KS_ERR_INVAL;
    if (sock->Protocol != KS_TCP) {
        DbgPrint("[KernelSocket] ks_connect: only valid for TCP sockets\n");
        return KS_ERR_INVAL;
    }

    RtlZeroMemory(&remoteAddr, sizeof(remoteAddr));
    status = KsStringToSockaddr(ip, port, &remoteAddr);
    if (!NT_SUCCESS(status))
        return KS_ERR_INVAL;

    connDisp = (const WSK_PROVIDER_CONNECTION_DISPATCH*)
        sock->WskSocket->Dispatch;

    irp = KsAllocateIrp(&ctx);
    if (!irp)
        return KS_ERR_NOMEM;

    DbgPrint("[KernelSocket] ks_connect: connecting to %s:%u\n", ip, port);

    status = connDisp->WskConnect(sock->WskSocket,
        (PSOCKADDR)&remoteAddr, 0, irp);
    status = KsWaitIrp(irp, &ctx);

    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] ks_connect: failed 0x%08X\n", status);
        if (status == STATUS_CONNECTION_REFUSED ||
            status == STATUS_NETWORK_UNREACHABLE)
            return KS_ERR_CONN;
        return KS_ERR;
    }

    DbgPrint("[KernelSocket] ks_connect: connected to %s:%u\n", ip, port);
    return KS_OK;
}

/* =========================================================================
 * Public API — ks_send()   [TCP only]
 * ========================================================================= */

 /**
  * @brief Send data over a connected TCP socket.
  *
  * The buffer must reside in non-paged kernel memory because WSK pins it via
  * an MDL and may access it at DISPATCH_LEVEL internally.
  *
  * @param[in] sock  Connected TCP socket.
  * @param[in] buf   Non-paged kernel-space buffer to send.
  * @param[in] len   Number of bytes to send (must be > 0).
  * @return          @p len on success, or a negative KS_ERR_* code.
  */
int ks_send(ks_socket_t* sock, const void* buf, unsigned int len)
{
    const WSK_PROVIDER_CONNECTION_DISPATCH* connDisp;
    WSK_BUF    wskBuf;
    PMDL       mdl;
    KS_IRP_CTX ctx;
    PIRP       irp;
    NTSTATUS   status;

    if (!sock || !buf || len == 0)
        return KS_ERR_INVAL;

    /* Describe the buffer with an MDL.  MmBuildMdlForNonPagedPool is used
     * because the buffer is guaranteed to be in non-paged pool. */
    mdl = IoAllocateMdl((PVOID)buf, len, FALSE, FALSE, NULL);
    if (!mdl)
        return KS_ERR_NOMEM;
    MmBuildMdlForNonPagedPool(mdl);

    wskBuf.Mdl = mdl;
    wskBuf.Offset = 0;
    wskBuf.Length = len;

    connDisp = (const WSK_PROVIDER_CONNECTION_DISPATCH*)
        sock->WskSocket->Dispatch;

    irp = KsAllocateIrp(&ctx);
    if (!irp) {
        IoFreeMdl(mdl);
        return KS_ERR_NOMEM;
    }

    status = connDisp->WskSend(sock->WskSocket, &wskBuf,
        0 /* no MSG_PARTIAL etc. */, irp);
    status = KsWaitIrp(irp, &ctx);
    IoFreeMdl(mdl);

    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] ks_send: failed 0x%08X\n", status);
        return KS_ERR;
    }

    DbgPrint("[KernelSocket] ks_send: sent %u bytes\n", len);
    return (int)len;
}

/* =========================================================================
 * Public API — ks_recv()   [TCP only]
 * ========================================================================= */

 /**
  * @brief Receive data from a connected TCP socket.
  *
  * Blocks until at least one byte is available or the remote end closes the
  * connection. May return fewer bytes than @p len (stream semantics).
  *
  * The actual byte count is read from Irp->IoStatus.Information via the
  * extended IRP context KS_IRP_CTX_EX.
  *
  * @param[in]  sock  Connected TCP socket.
  * @param[out] buf   Non-paged kernel-space destination buffer.
  * @param[in]  len   Size of @p buf in bytes.
  * @return           Bytes received (>0), 0 on peer close, or KS_ERR_*.
  */
int ks_recv(ks_socket_t* sock, void* buf, unsigned int len)
{
    const WSK_PROVIDER_CONNECTION_DISPATCH* connDisp;
    WSK_BUF       wskBuf;
    PMDL          mdl;
    KS_IRP_CTX_EX ctx;
    PIRP          irp;
    NTSTATUS      status;

    if (!sock || !buf || len == 0)
        return KS_ERR_INVAL;

    mdl = IoAllocateMdl(buf, len, FALSE, FALSE, NULL);
    if (!mdl)
        return KS_ERR_NOMEM;
    MmBuildMdlForNonPagedPool(mdl);

    wskBuf.Mdl = mdl;
    wskBuf.Offset = 0;
    wskBuf.Length = len;

    connDisp = (const WSK_PROVIDER_CONNECTION_DISPATCH*)
        sock->WskSocket->Dispatch;

    irp = KsAllocateIrpEx(&ctx);
    if (!irp) {
        IoFreeMdl(mdl);
        return KS_ERR_NOMEM;
    }

    status = connDisp->WskReceive(sock->WskSocket, &wskBuf, 0, irp);

    /* Wait manually so we can read ctx.Information before it is lost. */
    KeWaitForSingleObject(&ctx.Base.Event, Executive, KernelMode, FALSE, NULL);
    IoFreeIrp(irp);
    IoFreeMdl(mdl);

    status = ctx.Base.Status;

    if (status == STATUS_CONNECTION_DISCONNECTED ||
        status == STATUS_CONNECTION_RESET) {
        DbgPrint("[KernelSocket] ks_recv: connection closed by peer\n");
        return 0; /* mirrors POSIX recv() returning 0 on EOF */
    }
    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] ks_recv: failed 0x%08X\n", status);
        return KS_ERR;
    }

    DbgPrint("[KernelSocket] ks_recv: received %llu bytes\n", ctx.Information);
    return (int)ctx.Information;
}

/* =========================================================================
 * Public API — ks_sendto()   [UDP only]
 * ========================================================================= */

 /**
  * @brief Send a UDP datagram to a specific destination.
  *
  * No prior connection is required. The destination address is resolved and
  * passed directly to WskSendTo on every call.
  *
  * @param[in] sock  UDP socket from ks_socket(KS_UDP).
  * @param[in] buf   Non-paged kernel-space payload buffer.
  * @param[in] len   Payload length in bytes (must be > 0, max ~65507).
  * @param[in] ip    Destination IPv4 address as an ASCII string.
  * @param[in] port  Destination port in host byte order.
  * @return          @p len on success, or a negative KS_ERR_* code.
  */
int ks_sendto(ks_socket_t* sock,
    const void* buf,
    unsigned int   len,
    const char* ip,
    unsigned short port)
{
    const WSK_PROVIDER_DATAGRAM_DISPATCH* dgramDisp;
    SOCKADDR_IN  remoteAddr;
    WSK_BUF      wskBuf;
    PMDL         mdl;
    KS_IRP_CTX   ctx;
    PIRP         irp;
    NTSTATUS     status;

    if (!sock || !buf || len == 0 || !ip || port == 0)
        return KS_ERR_INVAL;
    if (sock->Protocol != KS_UDP) {
        DbgPrint("[KernelSocket] ks_sendto: only valid for UDP sockets\n");
        return KS_ERR_INVAL;
    }

    RtlZeroMemory(&remoteAddr, sizeof(remoteAddr));
    status = KsStringToSockaddr(ip, port, &remoteAddr);
    if (!NT_SUCCESS(status))
        return KS_ERR_INVAL;

    mdl = IoAllocateMdl((PVOID)buf, len, FALSE, FALSE, NULL);
    if (!mdl)
        return KS_ERR_NOMEM;
    MmBuildMdlForNonPagedPool(mdl);

    wskBuf.Mdl = mdl;
    wskBuf.Offset = 0;
    wskBuf.Length = len;

    dgramDisp = (const WSK_PROVIDER_DATAGRAM_DISPATCH*)
        sock->WskSocket->Dispatch;

    irp = KsAllocateIrp(&ctx);
    if (!irp) {
        IoFreeMdl(mdl);
        return KS_ERR_NOMEM;
    }

    /* WskSendTo(socket, buffer, flags, remoteAddr,
     *           controlInfoLength, controlInfo, irp)
     * flags = 0, no ancillary control data needed for basic UDP. */
    status = dgramDisp->WskSendTo(sock->WskSocket,
        &wskBuf, 0,
        (PSOCKADDR)&remoteAddr,
        0, NULL, irp);
    status = KsWaitIrp(irp, &ctx);
    IoFreeMdl(mdl);

    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] ks_sendto: failed 0x%08X\n", status);
        return KS_ERR;
    }

    DbgPrint("[KernelSocket] ks_sendto: sent %u bytes to %s:%u\n",
        len, ip, port);
    return (int)len;
}

/* =========================================================================
 * Public API — ks_recvfrom()   [UDP only]
 * ========================================================================= */

 /**
  * @brief Receive a UDP datagram and record the sender's address.
  *
  * Blocks until a datagram arrives. If the datagram exceeds @p len bytes the
  * excess is silently discarded (standard UDP behaviour). The sender's IPv4
  * address is converted back from binary to an ASCII string via
  * RtlIpv4AddressToStringW + a per-character UTF-16 → ASCII narrowing cast.
  *
  * @param[in]  sock      UDP socket from ks_socket(KS_UDP).
  * @param[out] buf       Non-paged kernel-space destination buffer.
  * @param[in]  len       Size of @p buf in bytes.
  * @param[out] src_ip    At least 16-byte buffer for the sender's IPv4 string.
  *                       May be NULL if the address is not needed.
  * @param[out] src_port  Receives sender's port in host byte order.
  *                       May be NULL if not needed.
  * @return               Bytes written into @p buf, or a negative KS_ERR_* code.
  */
int ks_recvfrom(ks_socket_t* sock,
    void* buf,
    unsigned int   len,
    char* src_ip,
    unsigned short* src_port)
{
    const WSK_PROVIDER_DATAGRAM_DISPATCH* dgramDisp;
    WSK_BUF       wskBuf;
    PMDL          mdl;
    KS_IRP_CTX_EX ctx;
    PIRP          irp;
    NTSTATUS      status;
    SOCKADDR_IN   fromAddr;

    if (!sock || !buf || len == 0)
        return KS_ERR_INVAL;
    if (sock->Protocol != KS_UDP) {
        DbgPrint("[KernelSocket] ks_recvfrom: only valid for UDP sockets\n");
        return KS_ERR_INVAL;
    }

    mdl = IoAllocateMdl(buf, len, FALSE, FALSE, NULL);
    if (!mdl)
        return KS_ERR_NOMEM;
    MmBuildMdlForNonPagedPool(mdl);

    wskBuf.Mdl = mdl;
    wskBuf.Offset = 0;
    wskBuf.Length = len;

    RtlZeroMemory(&fromAddr, sizeof(fromAddr));

    dgramDisp = (const WSK_PROVIDER_DATAGRAM_DISPATCH*)
        sock->WskSocket->Dispatch;

    irp = KsAllocateIrpEx(&ctx);
    if (!irp) {
        IoFreeMdl(mdl);
        return KS_ERR_NOMEM;
    }

    /* WskReceiveFrom(socket, buffer, flags, remoteAddr,
     *                controlInfoLength, controlInfo, controlFlags, irp)
     * WSK writes the sender address into fromAddr. */
    status = dgramDisp->WskReceiveFrom(sock->WskSocket,
        &wskBuf, 0,
        (PSOCKADDR)&fromAddr,
        0, NULL, NULL, irp);

    KeWaitForSingleObject(&ctx.Base.Event, Executive, KernelMode, FALSE, NULL);
    IoFreeIrp(irp);
    IoFreeMdl(mdl);

    status = ctx.Base.Status;
    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] ks_recvfrom: failed 0x%08X\n", status);
        return KS_ERR;
    }

    /* Decode sender address if the caller wants it. */
    if (src_ip != NULL) {
        WCHAR wideIp[INET_ADDRSTRLEN] = { 0 };
        int   i;
        RtlIpv4AddressToStringW(&fromAddr.sin_addr, wideIp);
        /* IPv4 strings are pure ASCII — safe to narrow per character. */
        for (i = 0; i < 15 && wideIp[i] != L'\0'; i++)
            src_ip[i] = (char)wideIp[i];
        src_ip[i] = '\0';
    }

    if (src_port != NULL)
        *src_port = RtlUshortByteSwap(fromAddr.sin_port); /* ntohs */

    DbgPrint("[KernelSocket] ks_recvfrom: received %llu bytes\n",
        ctx.Information);
    return (int)ctx.Information;
}

/* =========================================================================
 * Public API — ks_close()
 * ========================================================================= */

 /**
  * @brief Shut down and free a kernel socket.
  *
  * Calls WskCloseSocket which performs a graceful TCP FIN internally, then
  * frees the pool allocation. Safe to call with NULL (no-op).
  *
  * @param[in,out] sock  Socket handle to close and invalidate.
  */
void ks_close(ks_socket_t* sock)
{
    const WSK_PROVIDER_BASIC_DISPATCH* basicDisp;
    KS_IRP_CTX ctx;
    PIRP       irp;

    if (!sock)
        return;

    if (sock->WskSocket) {
        /* WskCloseSocket is the one call available on all socket types
         * through the common BASIC_DISPATCH (first field of every dispatch
         * struct — no cast required beyond BASIC). */
        basicDisp = (const WSK_PROVIDER_BASIC_DISPATCH*)
            sock->WskSocket->Dispatch;

        irp = KsAllocateIrp(&ctx);
        if (irp) {
            basicDisp->WskCloseSocket(sock->WskSocket, irp);
            KsWaitIrp(irp, &ctx);
            if (!NT_SUCCESS(ctx.Status))
                DbgPrint("[KernelSocket] ks_close: WskCloseSocket 0x%08X\n",
                    ctx.Status);
        }
        sock->WskSocket = NULL;
    }

    ExFreePoolWithTag(sock, 'ksKS');
    DbgPrint("[KernelSocket] ks_close: socket freed\n");
}
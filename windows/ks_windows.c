/**
 * @file ks_windows.c
 * @brief KernelSocket — Windows WSK implementation of the public ks_* API
 */

#include "ks_windows.h"

  /* =========================================================================
   * WSK global state
   * ========================================================================= */

static const WSK_CLIENT_DISPATCH g_WskClientDispatch = {
    MAKE_WSK_VERSION(1, 0), 0, NULL
};

WSK_REGISTRATION  g_WskRegistration;
WSK_PROVIDER_NPI  g_WskProvider;
BOOLEAN           g_WskReady = FALSE;

/* =========================================================================
 * WSK init / cleanup
 * ========================================================================= */

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

    status = WskCaptureProviderNPI(&g_WskRegistration,
        WSK_INFINITE_WAIT,
        &g_WskProvider);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] WskCaptureProviderNPI failed: 0x%08X\n", status);
        WskDeregister(&g_WskRegistration);
        return status;
    }

    g_WskReady = TRUE;
    DbgPrint("[KernelSocket] WSK provider ready (v%u.%u)\n",
        WSK_MAJOR_VERSION(g_WskProvider.Dispatch->Version),
        WSK_MINOR_VERSION(g_WskProvider.Dispatch->Version));
    return STATUS_SUCCESS;
}

void KsWskCleanup(void)
{
    if (!g_WskReady) return;
    WskReleaseProviderNPI(&g_WskRegistration);
    WskDeregister(&g_WskRegistration);
    g_WskReady = FALSE;
    DbgPrint("[KernelSocket] WSK provider released\n");
}

/* =========================================================================
 * IRP helpers
 * ========================================================================= */

static NTSTATUS NTAPI
KsIrpCompletionRoutine(
    _In_     PDEVICE_OBJECT DeviceObject,
    _In_     PIRP           Irp,
    _In_opt_ PVOID          Context)
{
    KS_IRP_CTX* ctx;
    UNREFERENCED_PARAMETER(DeviceObject);
    __analysis_assume(Context != NULL);   /* we always pass a valid ctx */
    ctx = (KS_IRP_CTX*)Context;
    ctx->Status = Irp->IoStatus.Status;
    KeSetEvent(&ctx->Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

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

NTSTATUS KsWaitIrp(PIRP Irp, KS_IRP_CTX* Ctx)
{
    KeWaitForSingleObject(&Ctx->Event, Executive, KernelMode, FALSE, NULL);
    IoFreeIrp(Irp);
    return Ctx->Status;
}

/* Extended context — also captures IoStatus.Information (byte count etc.) */
typedef struct _KS_IRP_CTX_EX {
    KS_IRP_CTX Base;
    ULONG_PTR  Information;
} KS_IRP_CTX_EX;

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

static PIRP KsAllocateIrpEx(KS_IRP_CTX_EX* Ctx)
{
    PIRP irp;
    KeInitializeEvent(&Ctx->Base.Event, NotificationEvent, FALSE);
    Ctx->Base.Status = STATUS_UNSUCCESSFUL;
    Ctx->Information = 0;
    irp = IoAllocateIrp(1, FALSE);
    if (!irp) return NULL;
    IoSetCompletionRoutine(irp, KsIrpCompletionEx, Ctx,
        TRUE, TRUE, TRUE);
    return irp;
}

/* =========================================================================
 * Public API — ks_socket()
 * ========================================================================= */

ks_socket_t* ks_socket(int protocol)
{
    ks_socket_t* ks;
    KS_IRP_CTX_EX                            ctxEx;
    KS_IRP_CTX                               ctx;
    PIRP                                     irp;
    NTSTATUS                                 status;
    SOCKADDR_IN                              localAddr;
    USHORT                                   socketType;
    ULONG                                    ipProto;
    const WSK_PROVIDER_DISPATCH* provDisp;
    const WSK_PROVIDER_CONNECTION_DISPATCH* connDisp;

    if (!g_WskReady) {
        DbgPrint("[KernelSocket] ks_socket: WSK not initialised\n");
        return NULL;
    }
    if (protocol != KS_TCP && protocol != KS_UDP) {
        DbgPrint("[KernelSocket] ks_socket: unknown protocol %d\n", protocol);
        return NULL;
    }

    ks = (ks_socket_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED,
        sizeof(ks_socket_t),
        'ksKS');
    if (!ks) {
        DbgPrint("[KernelSocket] ks_socket: ExAllocatePool2 failed\n");
        return NULL;
    }

    ks->Protocol = protocol;
    socketType = (protocol == KS_TCP) ? (USHORT)SOCK_STREAM : (USHORT)SOCK_DGRAM;
    ipProto = (protocol == KS_TCP) ? IPPROTO_TCP : IPPROTO_UDP;
    provDisp = g_WskProvider.Dispatch;

    /* Step 1: create WSK socket — result in IoStatus.Information */
    irp = KsAllocateIrpEx(&ctxEx);
    if (!irp) goto fail_free;

    status = provDisp->WskSocket(
        g_WskProvider.Client,
        AF_INET,
        socketType,
        ipProto,
        WSK_FLAG_CONNECTION_SOCKET,
        NULL, NULL, NULL, NULL, NULL,
        irp);

    KeWaitForSingleObject(&ctxEx.Base.Event, Executive, KernelMode, FALSE, NULL);
    IoFreeIrp(irp);

    status = ctxEx.Base.Status;
    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] WskSocket failed: 0x%08X\n", status);
        goto fail_free;
    }

    ks->WskSocket = (PWSK_SOCKET)ctxEx.Information;

    /* Step 2: WskBind — required before WskConnect even for 0.0.0.0:0 */
    connDisp = (const WSK_PROVIDER_CONNECTION_DISPATCH*)ks->WskSocket->Dispatch;

    RtlZeroMemory(&localAddr, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = 0;
    localAddr.sin_addr.S_un.S_addr = INADDR_ANY;

    irp = KsAllocateIrp(&ctx);
    if (!irp) goto fail_close;

    status = connDisp->WskBind(ks->WskSocket, (PSOCKADDR)&localAddr, 0, irp);
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
 * Public API — ks_connect()
 * ========================================================================= */

int ks_connect(ks_socket_t* sock, const char* ip, unsigned short port)
{
    const WSK_PROVIDER_CONNECTION_DISPATCH* connDisp;
    SOCKADDR_IN  remoteAddr;
    KS_IRP_CTX   ctx;
    PIRP         irp;
    NTSTATUS     status;
    ULONG        ipBinary;
    WCHAR        wideIp[16] = { 0 };
    PCWSTR       terminator;
    int          i;

    if (!sock || !ip || port == 0) {
        DbgPrint("[KernelSocket] ks_connect: invalid argument\n");
        return KS_ERR_INVAL;
    }
    if (sock->Protocol != KS_TCP) {
        DbgPrint("[KernelSocket] ks_connect: only valid for TCP\n");
        return KS_ERR_INVAL;
    }

    for (i = 0; i < 15 && ip[i] != '\0'; i++)
        wideIp[i] = (WCHAR)(unsigned char)ip[i];

    status = RtlIpv4StringToAddressW(wideIp, TRUE, &terminator,
        (struct in_addr*)&ipBinary);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] ks_connect: bad IP '%s'\n", ip);
        return KS_ERR_INVAL;
    }

    RtlZeroMemory(&remoteAddr, sizeof(remoteAddr));
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = RtlUshortByteSwap(port);
    remoteAddr.sin_addr.S_un.S_addr = ipBinary;

    connDisp = (const WSK_PROVIDER_CONNECTION_DISPATCH*)sock->WskSocket->Dispatch;

    irp = KsAllocateIrp(&ctx);
    if (!irp) return KS_ERR_NOMEM;

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
 * Public API — ks_send()
 * ========================================================================= */

int ks_send(ks_socket_t* sock, const void* buf, unsigned int len)
{
    const WSK_PROVIDER_CONNECTION_DISPATCH* connDisp;
    WSK_BUF    wskBuf;
    PMDL       mdl;
    KS_IRP_CTX ctx;
    PIRP       irp;
    NTSTATUS   status;

    if (!sock || !buf || len == 0) return KS_ERR_INVAL;

    mdl = IoAllocateMdl((PVOID)buf, len, FALSE, FALSE, NULL);
    if (!mdl) {
        DbgPrint("[KernelSocket] ks_send: IoAllocateMdl failed\n");
        return KS_ERR_NOMEM;
    }
    MmBuildMdlForNonPagedPool(mdl);

    wskBuf.Mdl = mdl;
    wskBuf.Offset = 0;
    wskBuf.Length = len;

    connDisp = (const WSK_PROVIDER_CONNECTION_DISPATCH*)sock->WskSocket->Dispatch;

    irp = KsAllocateIrp(&ctx);
    if (!irp) { IoFreeMdl(mdl); return KS_ERR_NOMEM; }

    status = connDisp->WskSend(sock->WskSocket, &wskBuf, 0, irp);
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
 * Public API — ks_recv()
 * ========================================================================= */

int ks_recv(ks_socket_t* sock, void* buf, unsigned int len)
{
    const WSK_PROVIDER_CONNECTION_DISPATCH* connDisp;
    WSK_BUF       wskBuf;
    PMDL          mdl;
    KS_IRP_CTX_EX ctx;
    PIRP          irp;
    NTSTATUS      status;

    if (!sock || !buf || len == 0) return KS_ERR_INVAL;

    mdl = IoAllocateMdl(buf, len, FALSE, FALSE, NULL);
    if (!mdl) return KS_ERR_NOMEM;
    MmBuildMdlForNonPagedPool(mdl);

    wskBuf.Mdl = mdl;
    wskBuf.Offset = 0;
    wskBuf.Length = len;

    connDisp = (const WSK_PROVIDER_CONNECTION_DISPATCH*)sock->WskSocket->Dispatch;

    irp = KsAllocateIrpEx(&ctx);
    if (!irp) { IoFreeMdl(mdl); return KS_ERR_NOMEM; }

    status = connDisp->WskReceive(sock->WskSocket, &wskBuf, 0, irp);

    KeWaitForSingleObject(&ctx.Base.Event, Executive, KernelMode, FALSE, NULL);
    IoFreeIrp(irp);
    IoFreeMdl(mdl);

    status = ctx.Base.Status;

    if (status == STATUS_CONNECTION_DISCONNECTED ||
        status == STATUS_CONNECTION_RESET) {
        DbgPrint("[KernelSocket] ks_recv: connection closed by peer\n");
        return 0;
    }
    if (!NT_SUCCESS(status)) {
        DbgPrint("[KernelSocket] ks_recv: failed 0x%08X\n", status);
        return KS_ERR;
    }

    DbgPrint("[KernelSocket] ks_recv: received %llu bytes\n", ctx.Information);
    return (int)ctx.Information;
}

/* =========================================================================
 * Public API — ks_close()
 * ========================================================================= */

void ks_close(ks_socket_t* sock)
{
    const WSK_PROVIDER_BASIC_DISPATCH* basicDisp;
    KS_IRP_CTX ctx;
    PIRP       irp;

    if (!sock) return;

    if (sock->WskSocket) {
        basicDisp = (const WSK_PROVIDER_BASIC_DISPATCH*)sock->WskSocket->Dispatch;
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
/**
 * @file ks_windows.h
 * @brief KernelSocket — Windows-specific internals (WSK layer)
 *
 * Not part of the public API. Include only from ks_windows.c and driver_entry.c.
 */

#ifndef KS_WINDOWS_H
#define KS_WINDOWS_H

#include <ntddk.h>
#include <wsk.h>
#include "ks_api.h"

 /* -------------------------------------------------------------------------
  * Internal socket structure
  * ---------------------------------------------------------------------- */

struct ks_socket {
    PWSK_SOCKET  WskSocket;
    int          Protocol;   /* KS_TCP or KS_UDP */
};

/* -------------------------------------------------------------------------
 * WSK global state
 * ---------------------------------------------------------------------- */

extern WSK_REGISTRATION   g_WskRegistration;
extern WSK_PROVIDER_NPI   g_WskProvider;
extern BOOLEAN            g_WskReady;

/* -------------------------------------------------------------------------
 * WSK init / cleanup
 * ---------------------------------------------------------------------- */

NTSTATUS KsWskInit(void);
void     KsWskCleanup(void);

/* -------------------------------------------------------------------------
 * IRP synchronous helper
 *
 * KsIrpCompletionRoutine is static inside ks_windows.c — not declared here.
 * Only the allocator and waiter are exported for potential reuse.
 * ---------------------------------------------------------------------- */

 /**
  * @brief Context for synchronous IRP wait.
  */
typedef struct _KS_IRP_CTX {
    KEVENT   Event;
    NTSTATUS Status;
} KS_IRP_CTX;

/**
 * @brief Allocate an IRP wired to the internal completion routine.
 */
PIRP KsAllocateIrp(KS_IRP_CTX* Ctx);

/**
 * @brief Wait for the IRP to complete, free it, and return its status.
 */
NTSTATUS KsWaitIrp(PIRP Irp, KS_IRP_CTX* Ctx);

#endif /* KS_WINDOWS_H */
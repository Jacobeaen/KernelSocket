/**
 * @file ks_windows.h
 * @brief KernelSocket — Windows-specific internal declarations (WSK layer)
 *
 * This header is internal to the Windows driver and must NOT be included
 * by user-facing code. Include ks_api.h instead.
 *
 * Included by:
 *   - ks_windows.c  (implementation)
 *   - driver_entry.c (calls KsWskInit / KsWskCleanup)
 */

#ifndef KS_WINDOWS_H
#define KS_WINDOWS_H

#include <ntddk.h>
#include <wsk.h>
#include "../include/ks_api.h"

 /* =========================================================================
  * Internal socket structure
  *
  * ks_socket_t is typedef'd to struct ks_socket in ks_api.h.
  * The layout is hidden from public callers — they only hold a pointer.
  * ========================================================================= */

struct ks_socket {
    PWSK_SOCKET WskSocket;  /**< Opaque WSK socket object.        */
    int         Protocol;   /**< KS_TCP or KS_UDP.                */
};

/* =========================================================================
 * WSK global state  (defined in ks_windows.c)
 * ========================================================================= */

 /** WSK client registration handle. */
extern WSK_REGISTRATION  g_WskRegistration;

/** WSK provider NPI — holds the dispatch table with WskSocket etc. */
extern WSK_PROVIDER_NPI  g_WskProvider;

/** TRUE once KsWskInit() has succeeded. */
extern BOOLEAN           g_WskReady;

/* =========================================================================
 * WSK subsystem init / cleanup  (called from driver_entry.c)
 * ========================================================================= */

 /**
  * @brief Register the WSK client and capture the provider NPI.
  *
  * Call once from DriverEntry at PASSIVE_LEVEL before any ks_* function.
  *
  * @return STATUS_SUCCESS, or an NTSTATUS error on failure.
  */
NTSTATUS KsWskInit(void);

/**
 * @brief Release the WSK provider and deregister the client.
 *
 * Call once from DriverUnload. Blocks until all pending WSK IRPs complete.
 */
void KsWskCleanup(void);

/* =========================================================================
 * Synchronous IRP helper
 *
 * These types and functions allow async WSK calls to be used synchronously.
 * The completion routine signals a KEVENT; the calling thread waits on it.
 * ========================================================================= */

 /**
  * @brief Context block for a synchronous WSK IRP wait.
  *
  * Allocate on the stack, pass to KsAllocateIrp(), dispatch the WSK call,
  * then call KsWaitIrp() to block until completion.
  */
typedef struct _KS_IRP_CTX {
    KEVENT   Event;   /**< Signalled by the completion routine. */
    NTSTATUS Status;  /**< Final NTSTATUS copied from the IRP.  */
} KS_IRP_CTX;

/**
 * @brief Allocate an IRP wired to the internal KsIrpCompletionRoutine.
 *
 * @param[out] Ctx  Caller-allocated KS_IRP_CTX (stack allocation is fine).
 * @return          New IRP, or NULL on allocation failure.
 */
PIRP KsAllocateIrp(KS_IRP_CTX* Ctx);

/**
 * @brief Wait for a dispatched IRP, free it, and return its status.
 *
 * Must be called at PASSIVE_LEVEL.
 *
 * @param[in] Irp  The IRP returned by KsAllocateIrp(), already dispatched.
 * @param[in] Ctx  The context passed to KsAllocateIrp().
 * @return         NTSTATUS result of the WSK operation.
 */
NTSTATUS KsWaitIrp(PIRP Irp, KS_IRP_CTX* Ctx);

#endif /* KS_WINDOWS_H */
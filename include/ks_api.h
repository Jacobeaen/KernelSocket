/**
 * @file ks_api.h
 * @brief KernelSocket — kernel-mode TCP/UDP networking API
 *
 * Unified interface for establishing TCP/UDP connections and transferring
 * data from kernel-mode drivers on Windows (KMDF) and Linux (LKM).
 *
 * ### Typical TCP usage
 * @code
 *   ks_socket_t *sock = ks_socket(KS_TCP);
 *   if (!sock) { // handle error }
 *
 *   if (ks_connect(sock, "192.168.1.100", 9000) != KS_OK) { // handle error }
 *
 *   const char msg[] = "Hello from kernel!";
 *   ks_send(sock, msg, sizeof(msg));
 *
 *   char reply[256] = {0};
 *   int n = ks_recv(sock, reply, sizeof(reply));
 *
 *   ks_close(sock);
 * @endcode
 *
 * ### Typical UDP usage
 * @code
 *   ks_socket_t *sock = ks_socket(KS_UDP);
 *   if (!sock) { // handle error }
 *
 *   const char msg[] = "ping";
 *   ks_sendto(sock, msg, sizeof(msg), "192.168.1.100", 9001);
 *
 *   char buf[256] = {0};
 *   char src_ip[16];
 *   unsigned short src_port;
 *   ks_recvfrom(sock, buf, sizeof(buf), src_ip, &src_port);
 *
 *   ks_close(sock);
 * @endcode
 *
 * @note This header is platform-agnostic. The implementation is provided
 *       separately in @c windows/ks_windows.c and @c linux/ks_linux.c.
 *
 * @authors KernelSocket Team, SPbPU IEiT
 * @version 0.1.0
 */

#ifndef KS_API_H
#define KS_API_H

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Constants
 * ========================================================================= */

/**
 * @defgroup ks_protocols Protocol identifiers
 * @{
 */

/** @brief Use TCP (connection-oriented, reliable, stream-based). */
#define KS_TCP  0

/** @brief Use UDP (connectionless, unreliable, datagram-based). */
#define KS_UDP  1

/** @} */

/**
 * @defgroup ks_errors Return codes
 * @{
 */

/** @brief Operation completed successfully. */
#define KS_OK           0

/** @brief Generic error. Check platform log (dmesg / DbgPrint) for details. */
#define KS_ERR         -1

/** @brief Invalid argument passed to a function. */
#define KS_ERR_INVAL   -2

/** @brief Memory allocation failure. */
#define KS_ERR_NOMEM   -3

/** @brief Connection refused or host unreachable. */
#define KS_ERR_CONN    -4

/** @brief Send or receive timeout. */
#define KS_ERR_TIMEOUT -5

/** @} */

/* =========================================================================
 * Opaque socket type
 * ========================================================================= */

/**
 * @brief Opaque handle representing a kernel socket.
 *
 * The internal layout is defined in the platform-specific implementation
 * files and must not be accessed directly. Always use the @c ks_* functions.
 *
 * Obtain a handle via ks_socket() and release it with ks_close().
 */
typedef struct ks_socket ks_socket_t;

/* =========================================================================
 * Socket lifecycle
 * ========================================================================= */

/**
 * @defgroup ks_lifecycle Socket lifecycle
 * @{
 */

/**
 * @brief Allocate and initialise a new kernel socket.
 *
 * Allocates the platform-specific socket structure and prepares it for use.
 * On Windows this registers a WSK client and creates a WSK socket object.
 * On Linux this calls @c sock_create_kern().
 *
 * @param[in] protocol  Transport protocol to use: @ref KS_TCP or @ref KS_UDP.
 *
 * @return Pointer to a newly allocated @ref ks_socket_t on success,
 *         or @c NULL on failure (out of memory, WSK unavailable, etc.).
 *
 * @note The returned handle must eventually be released with ks_close()
 *       to avoid resource leaks in kernel space.
 *
 * @see ks_close()
 */
ks_socket_t *ks_socket(int protocol);

/**
 * @brief Shut down and free a kernel socket.
 *
 * Performs a graceful shutdown (FIN for TCP), releases all platform
 * resources associated with the socket, and frees the handle.
 *
 * Safe to call with @c NULL — no-op in that case.
 *
 * @param[in,out] sock  Socket handle obtained from ks_socket(). Set to
 *                      @c NULL by the caller after this call.
 *
 * @see ks_socket()
 */
void ks_close(ks_socket_t *sock);

/** @} */

/* =========================================================================
 * TCP operations
 * ========================================================================= */

/**
 * @defgroup ks_tcp TCP operations
 * @{
 */

/**
 * @brief Connect a TCP socket to a remote host.
 *
 * Resolves @p ip to a binary address and performs a synchronous TCP
 * three-way handshake. Blocks until the connection is established or an
 * error occurs.
 *
 * @param[in] sock   A TCP socket created with @c ks_socket(KS_TCP).
 * @param[in] ip     Remote IPv4 address as a null-terminated string,
 *                   e.g. @c "192.168.1.100". Domain names are not resolved.
 * @param[in] port   Remote port in host byte order (1–65535).
 *
 * @retval KS_OK         Connection established.
 * @retval KS_ERR_INVAL  @p sock is @c NULL, @p ip is @c NULL, or
 *                       @p protocol is not @c KS_TCP.
 * @retval KS_ERR_CONN   Connection refused or host unreachable.
 * @retval KS_ERR        Other platform error.
 *
 * @pre  @p sock must have been created with @c ks_socket(KS_TCP).
 * @post On success the socket is in a connected state and ready for
 *       ks_send() / ks_recv().
 *
 * @see ks_send(), ks_recv(), ks_close()
 */
int ks_connect(ks_socket_t *sock, const char *ip, unsigned short port);

/**
 * @brief Send data over a connected TCP socket.
 *
 * Transmits exactly @p len bytes from @p buf. The call blocks until all
 * bytes have been handed to the network stack or an error occurs.
 *
 * @param[in] sock  Connected TCP socket.
 * @param[in] buf   Pointer to the data buffer. Must be a kernel-space address
 *                  (i.e. accessible without paging during the call).
 * @param[in] len   Number of bytes to send. Must be > 0.
 *
 * @return Number of bytes sent on success (equal to @p len),
 *         or a negative @ref ks_errors code on failure.
 *
 * @pre  ks_connect() must have completed successfully.
 *
 * @see ks_recv()
 */
int ks_send(ks_socket_t *sock, const void *buf, unsigned int len);

/**
 * @brief Receive data from a connected TCP socket.
 *
 * Blocks until at least 1 byte is available or the connection is closed.
 * May return fewer bytes than @p len (stream semantics — loop if needed).
 *
 * @param[in]  sock  Connected TCP socket.
 * @param[out] buf   Destination buffer. Must be a kernel-space address.
 * @param[in]  len   Maximum number of bytes to read (size of @p buf).
 *
 * @return Number of bytes written into @p buf on success (1 ≤ n ≤ len),
 *         @c 0 if the remote end closed the connection,
 *         or a negative @ref ks_errors code on failure.
 *
 * @see ks_send()
 */
int ks_recv(ks_socket_t *sock, void *buf, unsigned int len);

/** @} */

/* =========================================================================
 * UDP operations
 * ========================================================================= */

/**
 * @defgroup ks_udp UDP operations
 * @{
 */

/**
 * @brief Send a UDP datagram to a specific destination.
 *
 * Each call sends one self-contained datagram. No connection state is
 * required; the destination is specified per call.
 *
 * @param[in] sock   A UDP socket created with @c ks_socket(KS_UDP).
 * @param[in] buf    Datagram payload. Must be a kernel-space address.
 * @param[in] len    Payload length in bytes. Must be ≤ 65507 bytes
 *                   (maximum UDP payload over IPv4).
 * @param[in] ip     Destination IPv4 address as a null-terminated string.
 * @param[in] port   Destination port in host byte order.
 *
 * @return Number of bytes sent on success,
 *         or a negative @ref ks_errors code on failure.
 *
 * @see ks_recvfrom()
 */
int ks_sendto(ks_socket_t *sock,
              const void   *buf,
              unsigned int  len,
              const char   *ip,
              unsigned short port);

/**
 * @brief Receive a UDP datagram and record the sender's address.
 *
 * Blocks until a datagram arrives. If the datagram is larger than @p len,
 * the excess bytes are silently discarded (standard UDP behaviour).
 *
 * @param[in]  sock      A UDP socket created with @c ks_socket(KS_UDP).
 * @param[out] buf       Destination buffer. Must be a kernel-space address.
 * @param[in]  len       Size of @p buf in bytes.
 * @param[out] src_ip    Caller-allocated buffer (at least 16 bytes) that
 *                       will receive the sender's IPv4 address as a
 *                       null-terminated string, e.g. @c "10.0.0.1".
 *                       May be @c NULL if the address is not needed.
 * @param[out] src_port  Receives the sender's port in host byte order.
 *                       May be @c NULL if not needed.
 *
 * @return Number of bytes written into @p buf on success,
 *         or a negative @ref ks_errors code on failure.
 *
 * @see ks_sendto()
 */
int ks_recvfrom(ks_socket_t   *sock,
                void          *buf,
                unsigned int   len,
                char          *src_ip,
                unsigned short *src_port);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* KS_API_H */
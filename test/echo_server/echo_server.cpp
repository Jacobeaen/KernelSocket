/**
 * @file echo_server.cpp
 * @brief TCP + UDP echo server for KernelSocket self-tests
 *
 * Starts two listener threads simultaneously:
 *   - TCP on @c BASE_PORT       (default 9000)
 *   - UDP on @c BASE_PORT + 1   (default 9001)
 *
 * Every byte received on TCP is echoed back to the same client.
 * Every UDP datagram received is sent back to the sender unchanged.
 *
 * ## Build
 *
 * **Windows** (Visual Studio Developer Command Prompt):
 * @code
 *   cl echo_server.cpp /Fe:echo_server.exe /EHsc /link ws2_32.lib
 * @endcode
 *
 * **Linux**:
 * @code
 *   g++ -std=c++11 -o echo_server echo_server.cpp -lpthread
 * @endcode
 *
 * ## Run
 * @code
 *   echo_server.exe 9000    (Windows)
 *   ./echo_server 9000      (Linux)
 * @endcode
 */

/* ------------------------------------------------------------------
 * Platform abstraction
 * ------------------------------------------------------------------ */

#ifdef _WIN32
    #define _WINSOCK_DEPRECATED_NO_WARNINGS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #include <process.h>
    typedef int    socklen_t;
    typedef SOCKET socket_t;
    #define THREAD_RET  unsigned __stdcall
    #define CLOSE(s)    closesocket(s)
    #define SLEEP_MS(n) Sleep(n)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <pthread.h>
    typedef int socket_t;
    #define INVALID_SOCKET  (-1)
    #define SOCKET_ERROR    (-1)
    #define THREAD_RET      void *
    #define CLOSE(s)        close(s)
    #define SLEEP_MS(n)     usleep((n) * 1000)
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>

/* =========================================================================
 * Global configuration set from main()
 * ========================================================================= */

static int g_tcp_port; /**< TCP listen port. */
static int g_udp_port; /**< UDP listen port (g_tcp_port + 1). */

/* =========================================================================
 * TCP — per-connection worker thread
 * ========================================================================= */

/**
 * @brief Thread function for a single accepted TCP connection.
 *
 * Receives data in a loop and echoes every byte back until the client
 * disconnects or an error occurs.
 *
 * @param[in] arg  Connected socket cast to (void *)(size_t).
 */
static THREAD_RET tcp_worker(void *arg)
{
    socket_t client = (socket_t)(size_t)arg;
    char     buf[4096];
    int      session_bytes = 0;

    while (true) {
        int n = recv(client, buf, (int)sizeof(buf), 0);
        if (n <= 0)
            break;

        /* Echo all received bytes back. */
        int sent = 0;
        while (sent < n) {
            int r = send(client, buf + sent, n - sent, 0);
            if (r <= 0)
                goto disconnect;
            sent += r;
        }

        session_bytes += n;
        printf("[TCP] echoed %d bytes  (session total: %d)\n",
               n, session_bytes);
        fflush(stdout);
    }

disconnect:
    printf("[TCP] client disconnected - session bytes: %d\n", session_bytes);
    fflush(stdout);
    CLOSE(client);
    return (THREAD_RET)0;
}

/* =========================================================================
 * TCP — listener thread
 * ========================================================================= */

/**
 * @brief Thread function that accepts TCP connections and spawns workers.
 */
static THREAD_RET tcp_listener(void * /*arg*/)
{
    socket_t    server;
    int         opt = 1;
    sockaddr_in addr{};

    server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        printf("[TCP] socket() failed\n");
        return (THREAD_RET)1;
    }

    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));

    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((unsigned short)g_tcp_port);

    bind(server, (sockaddr *)&addr, sizeof(addr));
    listen(server, 5);

    printf("[TCP] Listening on 0.0.0.0:%d\n", g_tcp_port);
    fflush(stdout);

    while (true) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);
        char        client_ip[INET_ADDRSTRLEN];

        socket_t client = accept(server, (sockaddr *)&client_addr, &client_len);
        if (client == INVALID_SOCKET)
            continue;

        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("[TCP] client connected: %s:%d\n",
               client_ip, ntohs(client_addr.sin_port));
        fflush(stdout);

#ifdef _WIN32
        _beginthreadex(NULL, 0, tcp_worker, (void *)(size_t)client, 0, NULL);
#else
        pthread_t t;
        pthread_create(&t, NULL, tcp_worker, (void *)(size_t)client);
        pthread_detach(t);
#endif
    }

    CLOSE(server);
    return (THREAD_RET)0;
}

/* =========================================================================
 * UDP — listener thread
 * ========================================================================= */

/**
 * @brief Thread function that receives UDP datagrams and echoes them back.
 *
 * Uses a single recvfrom / sendto loop — UDP is connectionless so no
 * per-client threads are needed.
 */
static THREAD_RET udp_listener(void * /*arg*/)
{
    socket_t    sock;
    int         opt = 1;
    sockaddr_in addr{};
    char        buf[65536];

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printf("[UDP] socket() failed\n");
        return (THREAD_RET)1;
    }

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));

    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((unsigned short)g_udp_port);

    bind(sock, (sockaddr *)&addr, sizeof(addr));

    printf("[UDP] Listening on 0.0.0.0:%d\n", g_udp_port);
    fflush(stdout);

    while (true) {
        sockaddr_in from{};
        socklen_t   fromlen = sizeof(from);
        char        from_ip[INET_ADDRSTRLEN];

        int n = recvfrom(sock, buf, (int)sizeof(buf), 0,
                         (sockaddr *)&from, &fromlen);
        if (n <= 0)
            continue;

        inet_ntop(AF_INET, &from.sin_addr, from_ip, sizeof(from_ip));
        printf("[UDP] %d bytes from %s:%d - echoing\n",
               n, from_ip, ntohs(from.sin_port));
        fflush(stdout);

        sendto(sock, buf, n, 0, (sockaddr *)&from, fromlen);
    }

    CLOSE(sock);
    return (THREAD_RET)0;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(int argc, char *argv[])
{
    g_tcp_port = (argc > 1) ? atoi(argv[1]) : 9000;
    g_udp_port = g_tcp_port + 1;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[echo] WSAStartup failed\n");
        return 1;
    }
#endif

    printf("[echo] Starting - TCP:%d  UDP:%d\n", g_tcp_port, g_udp_port);
    printf("[echo] Press Ctrl+C to stop\n");
    fflush(stdout);

#ifdef _WIN32
    _beginthreadex(NULL, 0, tcp_listener, NULL, 0, NULL);
    _beginthreadex(NULL, 0, udp_listener, NULL, 0, NULL);
#else
    pthread_t t1, t2;
    pthread_create(&t1, NULL, tcp_listener, NULL);
    pthread_create(&t2, NULL, udp_listener, NULL);
    pthread_detach(t1);
    pthread_detach(t2);
#endif

    /* Keep the main thread alive; listener threads run until Ctrl+C. */
    while (true)
        SLEEP_MS(1000);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
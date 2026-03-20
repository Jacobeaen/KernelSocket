/**
 * @file main.cpp
 * @brief Simple TCP echo server for testing KernelSocket
 *
 * Listens on the specified port, accepts one client at a time,
 * echoes every received byte back, logs each exchange to stdout.
 *
 * Build on Windows (Developer Command Prompt):
 *   cl main.cpp /Fe:echo_server.exe /link ws2_32.lib
 *
 * Build on Linux:
 *   g++ -o echo_server main.cpp
 *
 * Run:
 *   echo_server.exe 9000     (Windows)
 *   ./echo_server 9000       (Linux)
 */

#ifdef _WIN32
    #define _WINSOCK_DEPRECATED_NO_WARNINGS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
    #define closesocket(s) close(s)
    typedef int SOCKET;
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char *argv[])
{
    int port = (argc > 1) ? atoi(argv[1]) : 9000;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[echo] WSAStartup failed\n");
        return 1;
    }
#endif

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        printf("[echo] socket() failed\n");
        return 1;
    }

    /* Allow immediate reuse of the port after restart */
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((unsigned short)port);

    if (bind(server, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("[echo] bind() failed\n");
        closesocket(server);
        return 1;
    }

    if (listen(server, 5) == SOCKET_ERROR) {
        printf("[echo] listen() failed\n");
        closesocket(server);
        return 1;
    }

    printf("[echo] Listening on 0.0.0.0:%d  (Ctrl+C to stop)\n", port);
    fflush(stdout);

    while (1) {
        sockaddr_in clientAddr{};
        socklen_t   clientLen = sizeof(clientAddr);

        SOCKET client = accept(server, (sockaddr *)&clientAddr, &clientLen);
        if (client == INVALID_SOCKET) {
            printf("[echo] accept() failed, retrying...\n");
            continue;
        }

        char clientIp[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));
        printf("[echo] Client connected: %s:%d\n",
               clientIp, ntohs(clientAddr.sin_port));
        fflush(stdout);

        char buf[4096];
        int  total = 0;

        while (1) {
            int n = recv(client, buf, sizeof(buf), 0);
            if (n <= 0) break;          /* connection closed or error */

            /* Echo every byte straight back */
            int sent = 0;
            while (sent < n) {
                int r = send(client, buf + sent, n - sent, 0);
                if (r <= 0) goto disconnect;
                sent += r;
            }

            total += n;
            printf("[echo] Echoed %d bytes  (session total: %d)\n", n, total);
            fflush(stdout);
        }

disconnect:
        printf("[echo] Client disconnected. Session bytes: %d\n", total);
        fflush(stdout);
        closesocket(client);
    }

    closesocket(server);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
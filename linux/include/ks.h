#ifndef KS_H
#define KS_H

#include <linux/types.h>

/* protocol */
#define KS_TCP 1
#define KS_UDP 2

/* return codes */
#define KS_OK 0
#define KS_ERR -1
#define KS_ERR_INVAL -2
#define KS_ERR_NOMEM -3
#define KS_ERR_CONN -4

typedef struct ks_socket ks_socket_t;

ks_socket_t* ks_socket(int protocol);
int ks_connect(ks_socket_t *ks, const char *ip, unsigned short port);
int ks_send(ks_socket_t *ks, const void *buf, unsigned int len);
int ks_recv(ks_socket_t *ks, void *buf, unsigned int len);
int ks_sendto(ks_socket_t *ks, const void *buf, unsigned int len, const char *ip, unsigned short port);
int ks_recvfrom(ks_socket_t *ks, void *buf, unsigned int len, char *src_ip, unsigned short *src_port);
void ks_close(ks_socket_t *ks);
int ks_bind(ks_socket_t *ks, const char *ip, unsigned short port);
int ks_listen(ks_socket_t *ks, int backlog);
ks_socket_t* ks_accept(ks_socket_t *ks);

void ks_test(void);

int ks_echo_start_tcp(void);
int ks_echo_start_udp(void); 

void ks_test_us_krnl(void);
void ks_test_client_udp(void);
void ks_test_client_tcp(void);

#endif
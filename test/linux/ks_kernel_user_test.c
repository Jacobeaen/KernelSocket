#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>

#include "ks.h"

#define PYTHON_SERVER_IP "127.0.0.1"
#define PYTHON_SERVER_TCP_PORT 5555
#define PYTHON_SERVER_UDP_PORT 6666

void ks_test_client_tcp(void)
{
    ks_socket_t *s;
    char buf[256];
    int ret;
    
    printk(KERN_INFO "KS TCP Client test start\n");
    printk(KERN_INFO "Connecting to Python server at %s:%d\n", 
           PYTHON_SERVER_IP, PYTHON_SERVER_TCP_PORT);
    
    s = ks_socket(KS_TCP);
    if (!s) {
        printk(KERN_ERR "Failed to create TCP socket\n");
        return;
    }
    
    ret = ks_connect(s, PYTHON_SERVER_IP, PYTHON_SERVER_TCP_PORT);
    if (ret != KS_OK) {
        printk(KERN_ERR "Failed to connect to Python server (error: %d)\n", ret);
        ks_close(s);
        return;
    }
    
    printk(KERN_INFO "Connected to Python server\n");
    
    // 1
    const char *msg1 = "Hello from kernel TCP client!";
    ret = ks_send(s, msg1, strlen(msg1));
    if (ret == KS_OK) {
        printk(KERN_INFO "Sent: %s\n", msg1);
    } else {
        printk(KERN_ERR "Failed to send first message\n");
    }
    
    msleep(100);
    
    // 2
    const char *msg2 = "exit";
    ret = ks_send(s, msg2, strlen(msg2));
    if (ret == KS_OK) {
        printk(KERN_INFO "Sent: %s\n", msg2);
    } else {
        printk(KERN_ERR "Failed to send exit command\n");
    }
    
    // answer
    memset(buf, 0, sizeof(buf));
    ret = ks_recv(s, buf, sizeof(buf) - 1);
    if (ret > 0) {
        buf[ret] = '\0';
        printk(KERN_INFO "Received from server: %s\n", buf);
    } else if (ret == KS_ERR_CONN) {
        printk(KERN_INFO "Connection closed by server (expected after 'exit')\n");
    } else {
        printk(KERN_INFO "No data received or recv error: %d\n", ret);
    }
    
    ks_close(s);
    printk(KERN_INFO "TCP client test finished\n");
}

void ks_test_client_udp(void)
{
    ks_socket_t *s;
    char buf[256];
    char src_ip[16];
    unsigned short src_port;
    int ret;
    
    printk(KERN_INFO "KS UDP Client test start\n");
    printk(KERN_INFO "Sending UDP datagrams to Python server at %s:%d\n", 
           PYTHON_SERVER_IP, PYTHON_SERVER_UDP_PORT);
    
    
    s = ks_socket(KS_UDP);
    if (!s) {
        printk(KERN_ERR "Failed to create UDP socket\n");
        return;
    }
    
    // 1
    const char *msg1 = "Hello from kernel UDP client!";
    ret = ks_sendto(s, msg1, strlen(msg1), PYTHON_SERVER_IP, PYTHON_SERVER_UDP_PORT);
    if (ret == KS_OK) {
        printk(KERN_INFO "Sent (UDP): %s\n", msg1);
    } else {
        printk(KERN_ERR "Failed to send first UDP message\n");
    }
    
    msleep(100);
    
    // 2
    const char *msg2 = "exit";
    ret = ks_sendto(s, msg2, strlen(msg2), PYTHON_SERVER_IP, PYTHON_SERVER_UDP_PORT);
    if (ret == KS_OK) {
        printk(KERN_INFO "Sent (UDP): %s\n", msg2);
    } else {
        printk(KERN_ERR "Failed to send exit command\n");
    }
    
    // answer
    memset(buf, 0, sizeof(buf));
    memset(src_ip, 0, sizeof(src_ip));
    ret = ks_recvfrom(s, buf, sizeof(buf) - 1, src_ip, &src_port);
    if (ret > 0) {
        buf[ret] = '\0';
        printk(KERN_INFO "Received from server (%s:%d): %s\n", 
               src_ip, src_port, buf);
    } else {
        printk(KERN_INFO "No response from server or recv timeout\n");
    }
    
   
    ks_close(s);
    printk(KERN_INFO "UDP client test finished\n");
}


void ks_test_us_krnl(void)
{
    printk(KERN_INFO "=== KS Socket Tests ===\n");   
    
    ks_test_client_tcp();
    printk(KERN_INFO "-------------------\n");
    
    
    ks_test_client_udp();
    printk(KERN_INFO "=== All tests completed ===\n");
}
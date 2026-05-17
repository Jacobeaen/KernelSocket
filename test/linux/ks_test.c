#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/string.h>
#include "ks.h"

void ks_test(void)
{
    ks_socket_t *s;
    char buf[128];

    printk(KERN_INFO "KS test start\n");

    s = ks_socket(KS_TCP);
    if (!s)
        return;

    if (ks_connect(s, "77.88.55.88", 80) == KS_OK) {
        ks_send(s, "GET / HTTP/1.1\r\nHost: yandex.ru\r\n\r\n", strlen("GET / HTTP/1.1\r\nHost: yandex.ru\r\n\r\n"));

        int r = ks_recv(s, buf, sizeof(buf) - 1);
        if (r > 0) {
            buf[r] = '\0';
            printk(KERN_INFO "recv: %s\n", buf);
        }
    }

    ks_close(s);
}
# KernelSocket

> Kernel-mode TCP/UDP networking API for Windows and Linux drivers

[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE.)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey)](README.md)
[![Language](https://img.shields.io/badge/language-C-orange)](README.md)
[![Status](https://img.shields.io/badge/status-In%20Development-yellow)](README.md)

KernelSocket provides a unified, user-friendly API for establishing TCP and UDP
connections from **kernel-mode drivers** on both Windows and Linux — something
that currently lacks a convenient open-source solution.

---

## The Problem

Working with network protocols in kernel mode is fundamentally different from
user-space socket programming. Each OS exposes low-level, cumbersome interfaces:

- **Windows** — Winsock Kernel (WSK) requires manual NPI provider registration,
  IRP-based asynchronous operations, and separate dispatch tables for TCP and UDP
- **Linux** — raw `sock_create` / `kernel_connect` / `kernel_sendmsg` calls with
  full manual socket lifecycle management

There is no unified, documented, easy-to-use open-source library that abstracts
these differences. **KernelSocket fills that gap.**

---

## API Overview

The same header works on both platforms — the implementation differs, the
interface does not.

```c
/* Create a kernel socket — KS_TCP or KS_UDP */
ks_socket_t *ks_socket(int protocol);

/* TCP: connect to a remote host */
int ks_connect(ks_socket_t *sock, const char *ip, unsigned short port);

/* TCP: send data / receive data */
int ks_send   (ks_socket_t *sock, const void *buf, unsigned int len);
int ks_recv   (ks_socket_t *sock,       void *buf, unsigned int len);

/* UDP: send a datagram / receive a datagram */
int ks_sendto  (ks_socket_t *sock, const void *buf, unsigned int len,
                const char *ip, unsigned short port);
int ks_recvfrom(ks_socket_t *sock,       void *buf, unsigned int len,
                char *src_ip, unsigned short *src_port);

/* Close and free the socket */
void ks_close(ks_socket_t *sock);
```

Return values follow the `KS_OK` / `KS_ERR_*` codes defined in `include/ks_api.h`.

### Typical TCP usage

```c
ks_socket_t *sock = ks_socket(KS_TCP);

if (ks_connect(sock, "192.168.1.100", 9000) != KS_OK) { /* handle error */ }

const char msg[] = "Hello from kernel!";
ks_send(sock, msg, sizeof(msg));

char reply[256] = {0};
int n = ks_recv(sock, reply, sizeof(reply));

ks_close(sock);
```

### Typical UDP usage

```c
ks_socket_t *sock = ks_socket(KS_UDP);

const char msg[] = "ping";
ks_sendto(sock, msg, sizeof(msg), "192.168.1.100", 9001);

char buf[256] = {0};
char src_ip[16];
unsigned short src_port;
ks_recvfrom(sock, buf, sizeof(buf), src_ip, &src_port);

ks_close(sock);
```

---

## Repository Structure

```
KernelSocket/
│
├── include/
│   └── ks_api.h                  # Public API header (platform-agnostic)
│
├── windows/
│   ├── ks_windows.c              # WSK-based TCP/UDP implementation
│   ├── ks_windows.h              # Windows-specific internals
│   └── driver_entry.c            # KMDF DriverEntry / DriverUnload + self-tests
│
├── linux/
│   ├── ks_linux.c                # sock_create / kernel_connect implementation
│   ├── ks_linux.h                # Linux-specific internals
│   ├── module_main.c             # module_init / module_exit + self-tests
│   └── Makefile                  # Kbuild configuration
│
├── test/
│   └── echo_server/
│       └── echo_server.cpp       # Cross-platform TCP+UDP echo server
│
├── docs/
│   └── Doxyfile                  # Doxygen configuration
│
└── README.md
```

---

## Building — Windows

### Requirements

| Tool | Notes |
|---|---|
| [Visual Studio 2022](https://visualstudio.microsoft.com/) | Workload: **Desktop development with C++** |
| [Windows Driver Kit (WDK) 10.0+](https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk) | Version must match the installed Windows SDK |
| Windows 10/11 x64 VM | VMware Workstation or VirtualBox |

### 1 — Prepare the target VM

Test-signing must be enabled on the VM so unsigned drivers can be loaded.
Run the following in an elevated command prompt **inside the VM**, then reboot:

```cmd
bcdedit /set testsigning on
bcdedit /set nointegritychecks on
```

After rebooting, a **"Test Mode"** watermark appears in the bottom-right corner
of the desktop — this confirms test-signing is active.

Install **[DebugView](https://learn.microsoft.com/sysinternals/downloads/debugview)**
on the VM to see `DbgPrint` output from the driver.
Enable *Capture → Capture Kernel* before loading the driver.

### 2 — Build the driver

Open the solution in Visual Studio, select **Debug | x64**, and press `Ctrl+Shift+B`.

> **Important:** `netio.lib` must be added manually — WDK does not include it
> automatically. Go to:
> Project Properties → Linker → Input → Additional Dependencies → add `netio.lib`

The compiled driver is placed at `x64\Debug\KernelSocket.sys`.

### 3 — Set the test server IP

Before building, open `windows\driver_entry.c` and update:

```c
#define TEST_SERVER_IP   "192.168.x.x"   // IP of the machine running echo_server
```

### 4 — Load the driver on the VM

Copy `KernelSocket.sys` to the VM, then from an elevated command prompt:

```cmd
sc create KernelSocket type= kernel binPath= C:\path\to\KernelSocket.sys
sc start  KernelSocket
```

To unload:

```cmd
sc stop   KernelSocket
sc delete KernelSocket
```

---

## Building — Linux

### Requirements

```bash
# Install kernel headers and build tools (Debian / Ubuntu)
sudo apt install linux-headers-$(uname -r) build-essential
```

Verify the headers are present:

```bash
ls /lib/modules/$(uname -r)/build   # must exist
```

### Build

```bash
cd linux/
make
```

### Load / unload the module

```bash
# Load
sudo insmod ks_linux.ko

# Check output
dmesg | tail -30

# Unload
sudo rmmod ks_linux
```

---

## Running the Tests

Both the Windows driver and the Linux module contain built-in self-tests that
run automatically on load. They connect to a user-mode echo server, exchange
data over TCP and UDP, and log the results.

### Start the echo server

The echo server listens on **TCP port N** and **UDP port N+1** simultaneously.

**Build on Windows** (Visual Studio Developer Command Prompt):

```cmd
cd test\echo_server
cl echo_server.cpp /Fe:echo_server.exe /EHsc /link ws2_32.lib
echo_server.exe 9000
```

**Build on Linux:**

```bash
cd test/echo_server
g++ -std=c++11 -o echo_server echo_server.cpp -lpthread
./echo_server 9000
```

Expected server output when the driver connects:

```
[echo] Starting — TCP:9000  UDP:9001
[TCP]  Listening on 0.0.0.0:9000
[UDP]  Listening on 0.0.0.0:9001
[TCP]  client connected: 192.168.x.x:xxxxx
[TCP]  echoed 29 bytes  (session total: 29)
[TCP]  client disconnected — session bytes: 29
[UDP]  29 bytes from 192.168.x.x:xxxxx — echoing
```

### Expected driver / module output

```
[KernelSocket] WSK provider ready (v1.0)
[KernelSocket] ========== TCP test ==========
[KernelSocket] PASS ks_socket
[KernelSocket] PASS ks_connect to 192.168.x.x:9000
[KernelSocket] PASS ks_send 29 bytes
[KernelSocket] PASS ks_recv 29 bytes: 'Hello from KernelSocket TCP!'
[KernelSocket] PASS echo content matches
[KernelSocket] PASS ks_close
[KernelSocket] TCP test COMPLETE
[KernelSocket] ========== UDP test ==========
[KernelSocket] PASS ks_socket
[KernelSocket] PASS ks_sendto 29 bytes to 192.168.x.x:9001
[KernelSocket] PASS ks_recvfrom 29 bytes from 192.168.x.x:9001
[KernelSocket] PASS ks_close
[KernelSocket] UDP test COMPLETE
```

---

## Platform Notes

| | Windows | Linux |
|---|---|---|
| Kernel socket API | Winsock Kernel (WSK) | `sock_create` / `kernel_connect` |
| Driver model | KMDF (`.sys`) | LKM (`.ko`) |
| Build system | MSBuild / WDK | Kbuild / Makefile |
| Logging | `DbgPrint` → DebugView / WinDbg | `printk` → `dmesg` |
| Test signing | Required (VM only) | Not required |
| TCP dispatch | `WSK_PROVIDER_CONNECTION_DISPATCH` | `SOCK_STREAM` |
| UDP dispatch | `WSK_PROVIDER_DATAGRAM_DISPATCH` | `SOCK_DGRAM` |

---

## Documentation

Full API reference is generated with Doxygen:

```bash
cd docs/
doxygen Doxyfile
# open docs/html/index.html in a browser
```

---

## Authors

Developed as part of the **Основы Проектной Деятельности** course at **SPbPU IEiT**.

---

## License

[MIT](LICENSE.)
# KernelSocket

> Kernel-mode TCP/UDP networking API for Windows and Linux drivers

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey)](README.md)
[![Language](https://img.shields.io/badge/language-C-orange)](README.md)
[![Status](https://img.shields.io/badge/status-In%20Development-yellow)](README.md)

KernelSocket provides a unified, user-friendly API for establishing TCP and UDP connections from **kernel-mode drivers** on both Windows and Linux — something that currently lacks a convenient open-source solution.

---

## Problem

Working with network protocols in kernel mode is fundamentally different from user-space socket programming. Each OS exposes low-level, cumbersome interfaces:

- **Windows** — Winsock Kernel (WSK), requiring manual NPI provider registration and complex IRP handling
- **Linux** — raw `sock_create` / `kernel_connect` / `kernel_sendmsg` calls with manual socket lifecycle management

There is no unified, documented, easy-to-use open-source library that abstracts these differences — **KernelSocket fills that gap.**

---

## Goals

- Provide a clean `ks_*` API (analogous to POSIX sockets) usable from kernel-mode code
- Support both **TCP** (connection-oriented) and **UDP** (datagram) protocols
- Target **Windows** (KMDF driver, `.sys`) and **Linux** (loadable kernel module, `.ko`)
- Ship with full **API documentation** (Doxygen) and a **demo kernel driver** showing real usage

---

## API Overview

```c
/* Create a kernel socket */
ks_socket_t *ks_socket(int protocol);          // KS_TCP or KS_UDP

/* Connect to remote host (TCP) */
int ks_connect(ks_socket_t *sock,
               const char *ip, uint16_t port);

/* Send data */
int ks_send(ks_socket_t *sock,
            const void *buf, size_t len);

/* Receive data */
int ks_recv(ks_socket_t *sock,
            void *buf, size_t len);

/* Send UDP datagram (connectionless) */
int ks_sendto(ks_socket_t *sock, const void *buf, size_t len,
              const char *ip, uint16_t port);

/* Receive UDP datagram */
int ks_recvfrom(ks_socket_t *sock, void *buf, size_t len,
                char *src_ip, uint16_t *src_port);

/* Close and free socket */
void ks_close(ks_socket_t *sock);
```

The same header works on both platforms — the implementation files differ.

---

## Repository Structure

```
KernelSocket/
├── include/
│   └── ks_api.h              # Public API header (platform-agnostic)
│
├── windows/
│   ├── ks_windows.c          # WSK-based implementation
│   ├── ks_windows.h          # Windows-specific internals
│   ├── driver_entry.c        # KMDF DriverEntry, device setup
│   └── KernelSocket.inf      # Driver installation descriptor
│
├── linux/
│   ├── ks_linux.c            # sock_create/kernel_connect implementation
│   ├── ks_linux.h            # Linux-specific internals
│   ├── module_main.c         # module_init / module_exit
│   └── Makefile              # Kbuild configuration
│
├── test/
│   ├── echo_server/          # User-mode echo server (C++) for testing
│   ├── test_driver_win/      # Kernel-mode test driver (Windows)
│   └── test_module_linux/    # Kernel-mode test module (Linux)
│
└── docs/
    ├── Doxyfile              # Doxygen config
    ├── api_reference.md      # Generated API docs
    └── build_guide.md        # How to build & install
```

---

## Building

### Windows

**Requirements:** Visual Studio 2022, Windows Driver Kit (WDK) 10.0+, Windows 10/11 VM with Test Signing enabled.

```cmd
:: Enable test signing on the target VM (run as Administrator, then reboot)
bcdedit /set testsigning on

:: Open KernelSocket.sln in Visual Studio and build in Debug|x64
:: Or from Developer Command Prompt:
msbuild KernelSocket.sln /p:Configuration=Debug /p:Platform=x64
```

**Install the driver:**
```cmd
sc create KernelSocket type= kernel binPath= C:\path\to\KernelSocket.sys
sc start KernelSocket
```

### Linux

**Requirements:** Linux kernel headers, GCC, make.

```bash
# Install kernel headers (Debian/Ubuntu)
sudo apt install linux-headers-$(uname -r) build-essential

# Build the module
cd linux/
make

# Load the module
sudo insmod ks_linux.ko

# Check kernel log
dmesg | tail -20

# Unload
sudo rmmod ks_linux
```

---

## Running Tests

Start the echo server on any machine accessible from the test target:

```bash
# Build and run echo server (listens on port 9000)
cd test/echo_server
g++ -o echo_server main.cpp
./echo_server 9000
```

Then load the test driver/module and observe kernel logs:

```bash
# Linux
sudo insmod test/test_module_linux/ks_test.ko server_ip="192.168.1.100" server_port=9000
dmesg | grep KernelSocket

# Windows — check DebugView or WinDbg for DbgPrint output
```

---

## Platform Notes

| Feature | Windows | Linux |
|---|---|---|
| Kernel socket API | Winsock Kernel (WSK) | `sock_create` / `kernel_connect` |
| Driver model | KMDF (`.sys`) | LKM (`.ko`) |
| Build system | MSBuild / WDK | Kbuild / Makefile |
| Logging | `DbgPrint` → WinDbg/DebugView | `printk` → `dmesg` |
| Test signing | Required (or UEFI Secure Boot off) | Not required |

---

## Documentation

Full API reference is generated with Doxygen:

```bash
cd docs/
doxygen Doxyfile
# Open docs/html/index.html in browser
```

---

## Authors

Developed as part of the **Основы Проектной Деятельности** course at **SPbPU IEiT**.

---

## License

[MIT](LICENSE)

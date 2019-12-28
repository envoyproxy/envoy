#pragma once
// NOLINT(namespace-envoy)

// This common "platform.h" header exists to simplify the most common references
// to non-ANSI C/C++ headers, required on Windows, Posix, Linux, BSD etc,
// and to provide substitute definitions when absolutely required.
//
// The goal is to eventually not require this file of envoy header declarations,
// but limit the use of these architecture-specific types and declarations
// to the corresponding .cc implementation files.

#ifdef _MSC_VER

#include <windows.h>
#include <winsock2.h>

// These must follow afterwards
#include <mswsock.h>
#include <ws2tcpip.h>

// <windows.h> defines some frequently used symbols, so we need to undef these interfering symbols.
#undef DELETE
#undef ERROR
#undef GetMessage
#undef interface
#undef TRUE

#include <io.h>
#include <stdint.h>

#define PACKED_STRUCT(definition, ...)                                                             \
  __pragma(pack(push, 1)) definition, ##__VA_ARGS__;                                               \
  __pragma(pack(pop))

using ssize_t = ptrdiff_t;

typedef unsigned int sa_family_t;

#else // POSIX

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/mman.h> // for mode_t
#include <sys/socket.h>
#include <sys/uio.h> // for iovec
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/netfilter_ipv4.h>
#endif

#define PACKED_STRUCT(definition, ...) definition, ##__VA_ARGS__ __attribute__((packed))

#ifndef IP6T_SO_ORIGINAL_DST
// From linux/netfilter_ipv6/ip6_tables.h
#define IP6T_SO_ORIGINAL_DST 80
#endif

#endif

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

// This is introduced in Windows SDK 10.0.17063.0 which is required
// to build Envoy on Windows (we will reevaluate whether earlier builds
// of Windows can be detected and PipeInstance marked unsupported at runtime.)
#include <afunix.h>

// <windows.h> defines some frequently used symbols, so we need to undef these
// interfering symbols.
#undef DELETE
#undef ERROR
#undef GetMessage
#undef interface
#undef TRUE

#include <io.h>
#include <stdint.h>

#define htole16(x) (x)
#define htole32(x) (x)
#define htole64(x) (x)
#define le16toh(x) (x)
#define le32toh(x) (x)
#define le64toh(x) (x)

#define htobe16(x) htons((x))
#define htobe32(x) htonl((x))
#define htobe64(x) htonll((x))
#define be16toh(x) ntohs((x))
#define be32toh(x) ntohl((x))
#define be64toh(x) ntohll((x))

#define PACKED_STRUCT(definition, ...)                                                             \
  __pragma(pack(push, 1)) definition, ##__VA_ARGS__;                                               \
  __pragma(pack(pop))

using ssize_t = ptrdiff_t;

// This is needed so the OsSysCalls interface compiles on Windows,
// shmOpen takes mode_t as an argument.
using mode_t = uint32_t;

using os_fd_t = SOCKET;

typedef unsigned int sa_family_t;

// Posix structure for scatter/gather I/O, not present on Windows.
struct iovec {
  void* iov_base;
  size_t iov_len;
};

// Posix structure for describing messages sent by 'sendmsg` and received by
// 'recvmsg'
struct msghdr {
  void* msg_name;
  socklen_t msg_namelen;
  iovec* msg_iov;
  size_t msg_iovlen;
  void* msg_control;
  size_t msg_controllen;
  int msg_flags;
};

// Windows cmsghdr elements are just slightly different than Posix,
// they are defined with the cmsg_len as a 32 bit uint, not size_t
// We express our sendmsg and recvmsg in terms of Posix structures
// and semantics, but won't adjust the cmsghdr elements.
// The default Windows macros dereference the Windows style msghdr
// member Control, which is an indirection that doesn't exist on posix.

#undef CMSG_FIRSTHDR
#define CMSG_FIRSTHDR(msg)                                                                         \
  (((msg)->msg_controllen >= sizeof(WSACMSGHDR)) ? (LPWSACMSGHDR)(msg)->msg_control                \
                                                 : (LPWSACMSGHDR)NULL)

#undef CMSG_NXTHDR
#define CMSG_NXTHDR(msg, cmsg)                                                                     \
  (((cmsg) == NULL)                                                                                \
       ? CMSG_FIRSTHDR(msg)                                                                        \
       : ((((PUCHAR)(cmsg) + WSA_CMSGHDR_ALIGN((cmsg)->cmsg_len) + sizeof(WSACMSGHDR)) >           \
           (PUCHAR)((msg)->msg_control) + (msg)->msg_controllen)                                   \
              ? (LPWSACMSGHDR)NULL                                                                 \
              : (LPWSACMSGHDR)((PUCHAR)(cmsg) + WSA_CMSGHDR_ALIGN((cmsg)->cmsg_len))))

#ifdef CMSG_DATA
#undef CMSG_DATA
#endif
#define CMSG_DATA(msg) WSA_CMSG_DATA(msg)

// Following Cygwin's porting example (may not be comprehensive)
#define SO_REUSEPORT SO_REUSEADDR

// Solve for rfc2292 (need to address rfc3542?)
#ifndef IPV6_RECVPKTINFO
#define IPV6_RECVPKTINFO IPV6_PKTINFO
#endif

#define SOCKET_VALID(sock) ((sock) != INVALID_SOCKET)
#define SOCKET_INVALID(sock) ((sock) == INVALID_SOCKET)
#define SOCKET_FAILURE(rc) ((rc) == SOCKET_ERROR)
#define SET_SOCKET_INVALID(sock) (sock) = INVALID_SOCKET

// arguments to shutdown
#define ENVOY_SHUT_RD SD_RECEIVE
#define ENVOY_SHUT_WR SD_SEND
#define ENVOY_SHUT_RDWR SD_BOTH

#else // POSIX

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/mman.h> // for mode_t
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h> // for iovec
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define htole16(x) OSSwapHostToLittleInt16((x))
#define htole32(x) OSSwapHostToLittleInt32((x))
#define htole64(x) OSSwapHostToLittleInt64((x))
#define le16toh(x) OSSwapLittleToHostInt16((x))
#define le32toh(x) OSSwapLittleToHostInt32((x))
#define le64toh(x) OSSwapLittleToHostInt64((x))
#define htobe16(x) OSSwapHostToBigInt16((x))
#define htobe32(x) OSSwapHostToBigInt32((x))
#define htobe64(x) OSSwapHostToBigInt64((x))
#define be16toh(x) OSSwapBigToHostInt16((x))
#define be32toh(x) OSSwapBigToHostInt32((x))
#define be64toh(x) OSSwapBigToHostInt64((x))
#else
#include <endian.h>
#endif

#if defined(__linux__)
#include <linux/netfilter_ipv4.h>
#endif

#define PACKED_STRUCT(definition, ...) definition, ##__VA_ARGS__ __attribute__((packed))

#ifndef IP6T_SO_ORIGINAL_DST
// From linux/netfilter_ipv6/ip6_tables.h
#define IP6T_SO_ORIGINAL_DST 80
#endif

using os_fd_t = int;

#define INVALID_SOCKET -1
#define SOCKET_VALID(sock) ((sock) >= 0)
#define SOCKET_INVALID(sock) ((sock) == -1)
#define SOCKET_FAILURE(rc) ((rc) == -1)
#define SET_SOCKET_INVALID(sock) (sock) = -1

// arguments to shutdown
#define ENVOY_SHUT_RD SHUT_RD
#define ENVOY_SHUT_WR SHUT_WR
#define ENVOY_SHUT_RDWR SHUT_RDWR

#endif

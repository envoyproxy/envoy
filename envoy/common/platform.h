#pragma once
// NOLINT(namespace-envoy)

// This common "platform.h" header exists to simplify the most common references
// to non-ANSI C/C++ headers, required on Windows, Posix, Linux, BSD etc,
// and to provide substitute definitions when absolutely required.
//
// The goal is to eventually not require this file of envoy header declarations,
// but limit the use of these architecture-specific types and declarations
// to the corresponding .cc implementation files.
#include "absl/strings/string_view.h"

#ifdef _MSC_VER

#include <windows.h>
#include <winsock2.h>

// These must follow afterwards
#include <mswsock.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

// This is introduced in Windows SDK 10.0.17063.0 which is required
// to build Envoy on Windows (we will reevaluate whether earlier builds
// of Windows can be detected and PipeInstance marked unsupported at runtime.)
#include <afunix.h>

// <windows.h> defines some frequently used symbols, so we need to undef these
// interfering symbols.
#undef ASSERT
#undef DELETE
#undef ERROR
#undef GetMessage
#undef interface
#undef TRUE
#undef IGNORE

#include <io.h>
#include <stdint.h>
#include <time.h>

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

typedef ptrdiff_t ssize_t;

// This is needed so the OsSysCalls interface compiles on Windows,
// shmOpen takes mode_t as an argument.
typedef uint32_t mode_t;

typedef SOCKET os_fd_t;
typedef HANDLE filesystem_os_id_t; // NOLINT(modernize-use-using)
typedef DWORD signal_t;            // NOLINT(modernize-use-using)

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

#define INVALID_HANDLE INVALID_HANDLE_VALUE
#define SOCKET_VALID(sock) ((sock) != INVALID_SOCKET)
#define SOCKET_INVALID(sock) ((sock) == INVALID_SOCKET)
#define SOCKET_FAILURE(rc) ((rc) == SOCKET_ERROR)
#define SET_SOCKET_INVALID(sock) (sock) = INVALID_SOCKET

// arguments to shutdown
#define ENVOY_SHUT_RD SD_RECEIVE
#define ENVOY_SHUT_WR SD_SEND
#define ENVOY_SHUT_RDWR SD_BOTH

// winsock2 functions return distinct set of error codes, disjoint from POSIX errors (that are
// also available on Windows and set by POSIX function invocations). Here we map winsock2 error
// codes with platform agnostic macros that correspond to the same or roughly similar errors on
// POSIX systems for use in cross-platform socket error handling.
#define SOCKET_ERROR_AGAIN WSAEWOULDBLOCK
#define SOCKET_ERROR_NOT_SUP WSAEOPNOTSUPP
#define SOCKET_ERROR_AF_NO_SUP WSAEAFNOSUPPORT
#define SOCKET_ERROR_IN_PROGRESS WSAEINPROGRESS
// winsock2 does not differentiate between PERM and ACCESS violations
#define SOCKET_ERROR_PERM WSAEACCES
#define SOCKET_ERROR_ACCESS WSAEACCES
#define SOCKET_ERROR_MSG_SIZE WSAEMSGSIZE
#define SOCKET_ERROR_INTR WSAEINTR
#define SOCKET_ERROR_ADDR_NOT_AVAIL WSAEADDRNOTAVAIL
#define SOCKET_ERROR_INVAL WSAEINVAL
#define SOCKET_ERROR_ADDR_IN_USE WSAEADDRINUSE
#define SOCKET_ERROR_BADF WSAEBADF
#define SOCKET_ERROR_CONNRESET WSAECONNRESET
#define SOCKET_ERROR_NETUNREACH WSAENETUNREACH

#define HANDLE_ERROR_PERM ERROR_ACCESS_DENIED
#define HANDLE_ERROR_INVALID ERROR_INVALID_HANDLE

#define ENVOY_WIN32_SIGNAL_COUNT 1
#define ENVOY_SIGTERM 0

namespace Platform {
constexpr absl::string_view null_device_path{"NUL"};

constexpr bool win32SupportsOriginalDestination() {
#if defined(SIO_QUERY_WFP_CONNECTION_REDIRECT_RECORDS) && defined(SO_ORIGINAL_DST)
  return true;
#else
  return false;
#endif
}

} // namespace Platform

#else // POSIX

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#if !defined(DO_NOT_INCLUDE_NETINET_TCP_H)
#include <netinet/tcp.h>
#endif
#include <netinet/udp.h> // for UDP_GRO
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

#undef TRUE
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

#ifndef SOL_UDP
#define SOL_UDP 17
#endif

#ifndef UDP_GRO
#define UDP_GRO 104
#endif

#ifndef UDP_SEGMENT
#define UDP_SEGMENT 103
#endif

#ifndef IPPROTO_MPTCP
#define IPPROTO_MPTCP 262
#endif

typedef int os_fd_t;            // NOLINT(modernize-use-using)
typedef int filesystem_os_id_t; // NOLINT(modernize-use-using)
typedef int signal_t;           // NOLINT(modernize-use-using)

#define INVALID_HANDLE -1
#define INVALID_SOCKET -1
#define SOCKET_VALID(sock) ((sock) >= 0)
#define SOCKET_INVALID(sock) ((sock) == -1)
#define SOCKET_FAILURE(rc) ((rc) == -1)
#define SET_SOCKET_INVALID(sock) (sock) = -1

// arguments to shutdown
#define ENVOY_SHUT_RD SHUT_RD
#define ENVOY_SHUT_WR SHUT_WR
#define ENVOY_SHUT_RDWR SHUT_RDWR

// Mapping POSIX socket errors to common error names
#define SOCKET_ERROR_AGAIN EAGAIN
#define SOCKET_ERROR_NOT_SUP ENOTSUP
#define SOCKET_ERROR_AF_NO_SUP EAFNOSUPPORT
#define SOCKET_ERROR_IN_PROGRESS EINPROGRESS
#define SOCKET_ERROR_PERM EPERM
#define SOCKET_ERROR_ACCESS EACCES
#define SOCKET_ERROR_MSG_SIZE EMSGSIZE
#define SOCKET_ERROR_INTR EINTR
#define SOCKET_ERROR_ADDR_NOT_AVAIL EADDRNOTAVAIL
#define SOCKET_ERROR_INVAL EINVAL
#define SOCKET_ERROR_ADDR_IN_USE EADDRINUSE
#define SOCKET_ERROR_BADF EBADF
#define SOCKET_ERROR_CONNRESET ECONNRESET
#define SOCKET_ERROR_NETUNREACH ENETUNREACH

// Mapping POSIX file errors to common error names
#define HANDLE_ERROR_PERM EACCES
#define HANDLE_ERROR_INVALID EBADF

#define ENVOY_SIGTERM SIGTERM

namespace Platform {
constexpr absl::string_view null_device_path{"/dev/null"};
}
#endif

// Note: chromium disabled recvmmsg regardless of ndk version. However, the only Android target
// currently actively using Envoy is Envoy Mobile, where recvmmsg is not actively disabled. In fact,
// defining mmsghdr here caused a conflicting definition with the ndk's definition of the struct
// (https://github.com/envoyproxy/envoy-mobile/pull/772/checks?check_run_id=534152886#step:4:64).
// Therefore, we decided to remove the Android check introduced here in
// https://github.com/envoyproxy/envoy/pull/10120. If someone out there encounters problems with
// this please bring up in Envoy's slack channel #envoy-udp-quic-dev.
#if defined(__linux__) || defined(__EMSCRIPTEN__)
#define ENVOY_MMSG_MORE 1
#else
#define ENVOY_MMSG_MORE 0
#define MSG_WAITFORONE 0x10000 // recvmmsg(): block until 1+ packets avail.
// Posix structure for describing messages sent by 'sendmmsg` and received by
// 'recvmmsg'
struct mmsghdr {
  struct msghdr msg_hdr;
  unsigned int msg_len;
};
#endif

// TODO: Remove once bazel supports NDKs > 21
#define SUPPORTS_CPP_17_CONTIGUOUS_ITERATOR
#ifdef __ANDROID_API__
#if __ANDROID_API__ < 24
#undef SUPPORTS_CPP_17_CONTIGUOUS_ITERATOR
#endif // __ANDROID_API__ < 24
#endif // ifdef __ANDROID_API__

// https://android.googlesource.com/platform/bionic/+/master/docs/status.md
// ``pthread_getname_np`` is introduced in API 26
#define SUPPORTS_PTHREAD_NAMING 0
#if defined(__ANDROID_API__)
#if __ANDROID_API__ >= 26
#undef SUPPORTS_PTHREAD_NAMING
#define SUPPORTS_PTHREAD_NAMING 1
#endif // __ANDROID_API__ >= 26
#elif defined(__linux__)
#undef SUPPORTS_PTHREAD_NAMING
#define SUPPORTS_PTHREAD_NAMING 1
#endif // defined(__ANDROID_API__)

#if defined(__linux__)
// On Linux, default listen backlog size to net.core.somaxconn which is runtime configurable
#define ENVOY_TCP_BACKLOG_SIZE -1
#else
// On non-Linux platforms use 128 which is libevent listener default
#define ENVOY_TCP_BACKLOG_SIZE 128
#endif

#if defined(__linux__)
#define ENVOY_PLATFORM_ENABLE_SEND_RST 1
#else
#define ENVOY_PLATFORM_ENABLE_SEND_RST 0
#endif

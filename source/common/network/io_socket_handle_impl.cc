#include "common/network/io_socket_handle_impl.h"

#include <errno.h>

#include <iostream>

#include "envoy/buffer/buffer.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/stack_array.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_error_impl.h"

#include "absl/types/optional.h"

using Envoy::Api::SysCallIntResult;
using Envoy::Api::SysCallSizeResult;

namespace Envoy {
namespace Network {

IoSocketHandleImpl::~IoSocketHandleImpl() {
  if (fd_ != -1) {
    IoSocketHandleImpl::close();
  }
}

Api::IoCallUint64Result IoSocketHandleImpl::close() {
  ASSERT(fd_ != -1);
  const int rc = ::close(fd_);
  fd_ = -1;
  return Api::IoCallUint64Result(rc, Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError));
}

bool IoSocketHandleImpl::isOpen() const { return fd_ != -1; }

Api::IoCallUint64Result IoSocketHandleImpl::readv(uint64_t max_length, Buffer::RawSlice* slices,
                                                  uint64_t num_slice) {
  STACK_ARRAY(iov, iovec, num_slice);
  uint64_t num_slices_to_read = 0;
  uint64_t num_bytes_to_read = 0;
  for (; num_slices_to_read < num_slice && num_bytes_to_read < max_length; num_slices_to_read++) {
    iov[num_slices_to_read].iov_base = slices[num_slices_to_read].mem_;
    const size_t slice_length = std::min(slices[num_slices_to_read].len_,
                                         static_cast<size_t>(max_length - num_bytes_to_read));
    iov[num_slices_to_read].iov_len = slice_length;
    num_bytes_to_read += slice_length;
  }
  ASSERT(num_bytes_to_read <= max_length);
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSizeResult result =
      os_syscalls.readv(fd_, iov.begin(), static_cast<int>(num_slices_to_read));
  return sysCallResultToIoCallResult(result);
}

Api::IoCallUint64Result IoSocketHandleImpl::writev(const Buffer::RawSlice* slices,
                                                   uint64_t num_slice) {
  STACK_ARRAY(iov, iovec, num_slice);
  uint64_t num_slices_to_write = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      iov[num_slices_to_write].iov_base = slices[i].mem_;
      iov[num_slices_to_write].iov_len = slices[i].len_;
      num_slices_to_write++;
    }
  }
  if (num_slices_to_write == 0) {
    return Api::ioCallUint64ResultNoError();
  }
  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSizeResult result = os_syscalls.writev(fd_, iov.begin(), num_slices_to_write);
  return sysCallResultToIoCallResult(result);
}

Api::IoCallUint64Result IoSocketHandleImpl::sendto(const Buffer::RawSlice& slice, int flags,
                                                   const Address::Instance& address) {
  const auto* address_base = dynamic_cast<const Address::InstanceBase*>(&address);
  sockaddr* sock_addr = const_cast<sockaddr*>(address_base->sockAddr());

  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSizeResult result = os_syscalls.sendto(fd_, slice.mem_, slice.len_, flags,
                                                           sock_addr, address_base->sockAddrLen());
  return sysCallResultToIoCallResult(result);
}

Api::IoCallUint64Result IoSocketHandleImpl::sendmsg(const Buffer::RawSlice* slices,
                                                    uint64_t num_slice, int flags,
                                                    const Address::Instance& address) {
  const auto* address_base = dynamic_cast<const Address::InstanceBase*>(&address);
  sockaddr* sock_addr = const_cast<sockaddr*>(address_base->sockAddr());

  STACK_ARRAY(iov, iovec, num_slice);
  uint64_t num_slices_to_write = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      iov[num_slices_to_write].iov_base = slices[i].mem_;
      iov[num_slices_to_write].iov_len = slices[i].len_;
      num_slices_to_write++;
    }
  }
  if (num_slices_to_write == 0) {
    return Api::ioCallUint64ResultNoError();
  }

  struct msghdr message;
  message.msg_name = reinterpret_cast<void*>(sock_addr);
  message.msg_namelen = address_base->sockAddrLen();
  message.msg_iov = iov.begin();
  message.msg_iovlen = num_slices_to_write;
  message.msg_control = nullptr;
  message.msg_controllen = 0;
  message.msg_flags = 0;

  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSizeResult result = os_syscalls.sendmsg(fd_, &message, flags);

  return sysCallResultToIoCallResult(result);
}

Api::IoCallUint64Result
IoSocketHandleImpl::sysCallResultToIoCallResult(const Api::SysCallSizeResult& result) {
  if (result.rc_ >= 0) {
    // Return nullptr as IoError upon success.
    return Api::IoCallUint64Result(result.rc_,
                                   Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError));
  }
  return Api::IoCallUint64Result(
      /*rc=*/0,
      (result.errno_ == EAGAIN
           // EAGAIN is frequent enough that its memory allocation should be avoided.
           ? Api::IoErrorPtr(IoSocketError::getIoSocketEagainInstance(),
                             IoSocketError::deleteIoError)
           : Api::IoErrorPtr(new IoSocketError(result.errno_), IoSocketError::deleteIoError)));
}

Address::InstanceConstSharedPtr maybeGetDstAddressFromHeader(const struct cmsghdr& cmsg,
                                                             uint32_t self_port) {
  if (cmsg.cmsg_type == IPV6_PKTINFO) {
    const struct in6_pktinfo* info = reinterpret_cast<const in6_pktinfo*>(CMSG_DATA(&cmsg));
    sockaddr_in6 ipv6_addr;
    memset(&ipv6_addr, 0, sizeof(sockaddr_in6));
    ipv6_addr.sin6_family = AF_INET6;
    ipv6_addr.sin6_addr = info->ipi6_addr;
    ipv6_addr.sin6_port = htons(self_port);
    return Address::addressFromSockAddr(reinterpret_cast<sockaddr_storage&>(ipv6_addr),
                                        sizeof(sockaddr_in6), /*v6only=*/false);
  }
#ifndef IP_RECVDSTADDR
  if (cmsg.cmsg_type == IP_PKTINFO) {
    const struct in_pktinfo* info = reinterpret_cast<const in_pktinfo*>(CMSG_DATA(&cmsg));
#else
  if (cmsg.cmsg_type == IP_RECVDSTADDR) {
    const struct in_addr* addr = reinterpret_cast<const in_addr*>(CMSG_DATA(&cmsg));
#endif
    sockaddr_in ipv4_addr;
    memset(&ipv4_addr, 0, sizeof(sockaddr_in));
    ipv4_addr.sin_family = AF_INET;
    ipv4_addr.sin_addr =
#ifndef IP_RECVDSTADDR
        info->ipi_addr;
#else
        *addr;
#endif
    ipv4_addr.sin_port = htons(self_port);
    return Address::addressFromSockAddr(reinterpret_cast<sockaddr_storage&>(ipv4_addr),
                                        sizeof(sockaddr_in), /*v6only=*/false);
  }
  return nullptr;
}

absl::optional<uint32_t> maybeGetPacketsDroppedFromHeader(
#ifdef SO_RXQ_OVFL
    const struct cmsghdr& cmsg) {
  if (cmsg.cmsg_type == SO_RXQ_OVFL) {
    return *reinterpret_cast<const uint32_t*>(CMSG_DATA(&cmsg));
  }
#else
    const struct cmsghdr&) {
#endif
  return absl::nullopt;
}

Api::IoCallUint64Result IoSocketHandleImpl::recvmsg(Buffer::RawSlice* slices,
                                                    const uint64_t num_slice, uint32_t self_port,
                                                    RecvMsgOutput& output) {

#ifndef __APPLE__
  // The minimum cmsg buffer size to filled in destination address and packets dropped when
  // receiving a packet. It is possible for a received packet to contain both IPv4 and IPv6
  // addresses.
  constexpr int cmsg_space = CMSG_SPACE(sizeof(int)) + CMSG_SPACE(sizeof(struct in_pktinfo)) +
                             CMSG_SPACE(sizeof(struct in6_pktinfo));
  char cbuf[cmsg_space];
#else
  // CMSG_SPACE() is supposed to be a constant expression, and most
  // BSD-ish code uses it to determine the size of buffers used to
  // send/receive descriptors in the control message for
  // sendmsg/recvmsg. Such buffers are often automatic variables.

  // In Darwin, CMSG_SPACE uses __DARWIN_ALIGN. And __DARWIN_ALIGN(p)
  // expands to

  // ((__darwin_size_t)((char *)(__darwin_size_t)(p) + __DARNWIN_ALIGNBYTES)
  //  &~ __DARWIN_ALIGNBYTES)

  // which Clang (to which many Apple employees contribute!) complains
  // about when invoked with -pedantic -- and with our -Werror, causes
  // Clang-based builds to fail. Clang says this is not a constant
  // expression:

  // error: variable length array folded to constant array as an
  // extension [-Werror,-pedantic]

  // Possibly true per standard definition, since that cast to (char *)
  // is ugly, but actually Clang was able to convert to a compile-time
  // constant... it just had to work harder.

  // As a ugly workaround, we define the following constant for use in
  // automatic array declarations, and in all cases have an assert to
  // verify that the value is of sufficient size. (The assert should
  // constantly propagate and be dead-code eliminated in normal compiles
  // and should cause a quick death if ever violated.)
  constexpr int cmsg_space = 128;
  char cbuf[cmsg_space];
#endif

  STACK_ARRAY(iov, iovec, num_slice);
  uint64_t num_slices_for_read = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      iov[num_slices_for_read].iov_base = slices[i].mem_;
      iov[num_slices_for_read].iov_len = slices[i].len_;
      ++num_slices_for_read;
    }
  }

  sockaddr_storage peer_addr;
  msghdr hdr;
  hdr.msg_name = &peer_addr;
  hdr.msg_namelen = sizeof(sockaddr_storage);
  hdr.msg_iov = iov.begin();
  hdr.msg_iovlen = num_slices_for_read;
  hdr.msg_flags = 0;

  struct cmsghdr* cmsg = reinterpret_cast<struct cmsghdr*>(cbuf);
  cmsg->cmsg_len = cmsg_space;
  hdr.msg_control = cmsg;
  hdr.msg_controllen = cmsg_space;
  auto& os_sys_calls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSizeResult result = os_sys_calls.recvmsg(fd_, &hdr, 0);
  if (result.rc_ < 0) {
    return sysCallResultToIoCallResult(result);
  }

  RELEASE_ASSERT((hdr.msg_flags & MSG_CTRUNC) == 0,
                 fmt::format("Incorrectly set control message length: {}", hdr.msg_controllen));
  RELEASE_ASSERT(hdr.msg_namelen > 0,
                 fmt::format("Unable to get remote address from recvmsg() for fd: {}", fd_));
  try {
    // Set v6only to false so that mapped-v6 address can be normalize to v4
    // address. Though dual stack may be disabled, it's still okay to assume the
    // address is from a dual stack socket. This is because mapped-v6 address
    // must come from a dual stack socket. An actual v6 address can come from
    // both dual stack socket and v6 only socket. If |peer_addr| is an actual v6
    // address and the socket is actually v6 only, the returned address will be
    // regarded as a v6 address from dual stack socket. However, this address is not going to be
    // used to create socket. Wrong knowledge of dual stack support won't hurt.
    output.peer_address_ =
        Address::addressFromSockAddr(peer_addr, hdr.msg_namelen, /*v6only=*/false);
  } catch (const EnvoyException& e) {
    PANIC(fmt::format("Invalid remote address for fd: {}, error: {}", fd_, e.what()));
  }

  // Get overflow, local and peer addresses from control message.
  if (hdr.msg_controllen > 0) {
    struct cmsghdr* cmsg;
    for (cmsg = CMSG_FIRSTHDR(&hdr); cmsg != nullptr; cmsg = CMSG_NXTHDR(&hdr, cmsg)) {
      if (output.local_address_ == nullptr) {
        try {
          Address::InstanceConstSharedPtr addr = maybeGetDstAddressFromHeader(*cmsg, self_port);
          if (addr != nullptr) {
            // This is a IP packet info message.
            output.local_address_ = std::move(addr);
            continue;
          }
        } catch (const EnvoyException& e) {
          PANIC(fmt::format("Invalid destination address for fd: {}, error: {}", fd_, e.what()));
        }
      }
      if (output.dropped_packets_ != nullptr) {
        absl::optional<uint32_t> maybe_dropped = maybeGetPacketsDroppedFromHeader(*cmsg);
        if (maybe_dropped) {
          *output.dropped_packets_ = *maybe_dropped;
        }
      }
    }
  }
  return sysCallResultToIoCallResult(result);
}

} // namespace Network
} // namespace Envoy

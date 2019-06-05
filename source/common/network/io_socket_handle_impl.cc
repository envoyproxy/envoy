#include "common/network/io_socket_handle_impl.h"

#include <errno.h>

#include "envoy/buffer/buffer.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/stack_array.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_error_impl.h"

using Envoy::Api::SysCallIntResult;
using Envoy::Api::SysCallSizeResult;

#ifndef SO_RXQ_OVFL
#define SO_RXQ_OVFL 40
#endif

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
  const Address::InstanceBase* address_base = dynamic_cast<const Address::InstanceBase*>(&address);
  sockaddr* sock_addr = const_cast<sockaddr*>(address_base->sockAddr());

  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSizeResult result = os_syscalls.sendto(fd_, slice.mem_, slice.len_, flags,
                                                           sock_addr, address_base->sockAddrLen());
  return sysCallResultToIoCallResult(result);
}

Api::IoCallUint64Result IoSocketHandleImpl::sendmsg(const Buffer::RawSlice* slices,
                                                    uint64_t num_slice, int flags,
                                                    const Address::Instance& address) {
  const Address::InstanceBase* address_base = dynamic_cast<const Address::InstanceBase*>(&address);
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

Api::IoCallUint64Result IoSocketHandleImpl::recvmsg(Buffer::RawSlice* slices,
                                                    const uint64_t num_slice, uint32_t self_port,
                                                    bool v6only, uint32_t* dropped_packets,
                                                    Address::InstanceConstSharedPtr& local_address,
                                                    Address::InstanceConstSharedPtr& peer_address) {

  // The minimum cmsg buffer size to filled in destination address and packets dropped when
  // receiving a packet. It is possible for a received packet to contain both IPv4 and IPv6
  // addresses.
  const int cmsg_space =
      CMSG_SPACE(sizeof(int)) + CMSG_SPACE(sizeof(in_pktinfo)) + CMSG_SPACE(sizeof(in6_pktinfo));
  char cbuf[cmsg_space];

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
  if (hdr.msg_flags & MSG_CTRUNC) {
    ENVOY_LOG(error, "Incorrectly set control message length: {}", hdr.msg_controllen);
    return sysCallResultToIoCallResult(result);
  }

  RELEASE_ASSERT(hdr.msg_namelen > 0,
                 fmt::format("Unable to get remote address from recvmsg() for fd: {}", fd_));
  try {
    peer_address = Address::addressFromSockAddr(peer_addr, hdr.msg_namelen, v6only);
  } catch (const EnvoyException& e) {
    ENVOY_LOG(error, "Invalid remote address for fd: {}, error: {}", fd_, e.what());
  }

  // Get overflow, local and peer addresses from control message.
  if (hdr.msg_controllen > 0) {
    struct cmsghdr* cmsg;
    for (cmsg = CMSG_FIRSTHDR(&hdr); cmsg != nullptr; cmsg = CMSG_NXTHDR(&hdr, cmsg)) {
#if defined(IPV6_PKTINFO)
      if (cmsg->cmsg_type == IPV6_PKTINFO) {
#else
      if (cmsg->cmsg_type == IPV6_RECVPKTINFO) {

#endif
        struct in6_pktinfo* info = reinterpret_cast<in6_pktinfo*>(CMSG_DATA(cmsg));
        sockaddr_in6 ipv6_addr;
        memset(&ipv6_addr, 0, sizeof(sockaddr_in6));
        ipv6_addr.sin6_family = AF_INET6;
        ipv6_addr.sin6_addr = info->ipi6_addr;
        ipv6_addr.sin6_port = htons(self_port);
        local_address = Address::addressFromSockAddr(reinterpret_cast<sockaddr_storage&>(ipv6_addr),
                                                     sizeof(sockaddr_in6), v6only);
      }
#if defined(IP_PKTINFO)
      if (cmsg->cmsg_type == IP_PKTINFO) {
        struct in_pktinfo* info = reinterpret_cast<in_pktinfo*>(CMSG_DATA(cmsg));
#else
      if (cmsgptr->cmsg_type == IP_RECVDSTADDR) {
        struct in_addr* addr = reinterpret_cast<in_addr*>(CMSG_DATA(cmsg));
#endif
        sockaddr_in ipv4_addr;
        memset(&ipv4_addr, 0, sizeof(sockaddr_in));
        ipv4_addr.sin_family = AF_INET;
        ipv4_addr.sin_addr =
#if defined(IP_PKTINFO)
            info->ipi_addr;
#else
            ipv4_addr.sin_addr = addr;
#endif
        ipv4_addr.sin_port = htons(self_port);
        local_address = Address::addressFromSockAddr(reinterpret_cast<sockaddr_storage&>(ipv4_addr),
                                                     sizeof(sockaddr_in), v6only);
      }
      if (dropped_packets != nullptr && cmsg->cmsg_type == SO_RXQ_OVFL) {
        *dropped_packets = *(reinterpret_cast<uint32_t*> CMSG_DATA(cmsg));
      }
    }
  }
  return sysCallResultToIoCallResult(result);
}

} // namespace Network
} // namespace Envoy

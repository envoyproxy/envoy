#include "common/network/io_socket_handle_impl.h"

#include <errno.h>

#include <iostream>

#include "envoy/buffer/buffer.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/stack_array.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_error_impl.h"

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
                                                    const Address::Ip* self_ip,
                                                    const Address::Instance& peer_address) {
  const auto* address_base = dynamic_cast<const Address::InstanceBase*>(&peer_address);
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
  message.msg_flags = 0;
  if (self_ip == nullptr) {
    message.msg_control = nullptr;
    message.msg_controllen = 0;
  } else {
#ifndef __APPLE__
    constexpr int kSpaceForIpv4 = CMSG_SPACE(sizeof(in_pktinfo));
    constexpr int kSpaceForIpv6 = CMSG_SPACE(sizeof(in6_pktinfo));
    // kSpaceForIp should be big enough to hold both IPv4 and IPv6 packet info.
    constexpr int kSpaceForIp = (kSpaceForIpv4 < kSpaceForIpv6) ? kSpaceForIpv6 : kSpaceForIpv4;
    std::cerr << "size of in_pktinfo " << kSpaceForIpv4 << " size of in6_pktinfo " << kSpaceForIpv6
              << "\n";
    char cbuf[kSpaceForIp]{0};
#else
    char cbuf[16]{0};
#endif
    message.msg_control = cbuf;
    cmsghdr* cmsg = CMSG_FIRSTHDR(&message);
    if (self_ip->version() == Address::IpVersion::v4) {
      cmsg->cmsg_len = CMSG_LEN(sizeof(in_pktinfo));
      cmsg->cmsg_level = IPPROTO_IP;
      cmsg->cmsg_type = IP_PKTINFO;
      in_pktinfo* pktinfo = reinterpret_cast<in_pktinfo*>(CMSG_DATA(cmsg));
      pktinfo->ipi_ifindex = 0;
      pktinfo->ipi_spec_dst.s_addr = self_ip->ipv4()->address();
    } else if (self_ip->version() == Address::IpVersion::v6) {
      cmsg->cmsg_len = CMSG_LEN(sizeof(in6_pktinfo));
      cmsg->cmsg_level = IPPROTO_IPV6;
      cmsg->cmsg_type = IPV6_PKTINFO;
      in6_pktinfo* pktinfo = reinterpret_cast<in6_pktinfo*>(CMSG_DATA(cmsg));
      pktinfo->ipi6_ifindex = 0;
      *(reinterpret_cast<absl::uint128*>(pktinfo->ipi6_addr.s6_addr)) = self_ip->ipv6()->address();
    }
    message.msg_controllen = cmsg->cmsg_len;
  }
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

} // namespace Network
} // namespace Envoy

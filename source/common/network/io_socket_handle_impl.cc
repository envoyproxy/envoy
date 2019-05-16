#include "common/network/io_socket_handle_impl.h"

#include <errno.h>
#include <netinet/in.h>

#include <iostream>

#include "envoy/buffer/buffer.h"
#include "envoy/network/address.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/stack_array.h"
#include "common/network/io_socket_error_impl.h"

using Envoy::Api::SysCallIntResult;
using Envoy::Api::SysCallSizeResult;

namespace Envoy {
namespace Network {

namespace {

void getSocketAddressInfo(const Address::Instance& address, sockaddr_storage& addr, socklen_t& sz) {
  // TODO(sumukhs): Add support for unix domain sockets

  const Address::Ip* ip = address.ip();
  ASSERT(ip != nullptr);
  memset(&addr, 0, sizeof(addr));

  switch (ip->version()) {
  case Address::IpVersion::v4: {
    addr.ss_family = AF_INET;
    auto const* ipv4 = ip->ipv4();
    ASSERT(ipv4 != nullptr);
    sockaddr_in* addrv4 = reinterpret_cast<sockaddr_in*>(&addr);
    addrv4->sin_port = htons(ip->port());
    addrv4->sin_addr.s_addr = ipv4->address();
    sz = sizeof(sockaddr_in);
    break;
  }
  case Address::IpVersion::v6: {
    addr.ss_family = AF_INET6;
    auto const* ipv6 = ip->ipv6();
    ASSERT(ipv6 != nullptr);
    sockaddr_in6* addrv6 = reinterpret_cast<sockaddr_in6*>(&addr);
    addrv6->sin6_port = htons(ip->port());

    const auto address = ipv6->address();
    memcpy(static_cast<void*>(&addrv6->sin6_addr.s6_addr), static_cast<const void*>(&address),
           sizeof(absl::uint128));

    sz = sizeof(sockaddr_in6);
    break;
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
    break;
  }
}

} // namespace

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

Api::IoCallUint64Result IoSocketHandleImpl::sendto(const void* buffer, size_t size, int flags,
                                                   const Address::Instance& address) {
  sockaddr_storage ss;
  socklen_t ss_len = sizeof ss;
  getSocketAddressInfo(address, ss, ss_len);

  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSizeResult result =
      os_syscalls.sendto(fd_, buffer, size, flags, reinterpret_cast<sockaddr*>(&ss), ss_len);
  return sysCallResultToIoCallResult(result);
}

Api::IoCallUint64Result IoSocketHandleImpl::sendmsg(const Buffer::RawSlice* slices,
                                                    uint64_t num_slice, int flags,
                                                    const Address::Instance& address) {
  sockaddr_storage ss;
  socklen_t ss_len = sizeof ss;
  getSocketAddressInfo(address, ss, ss_len);

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
  message.msg_name = reinterpret_cast<void*>(&ss);
  message.msg_namelen = ss_len;
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

} // namespace Network
} // namespace Envoy

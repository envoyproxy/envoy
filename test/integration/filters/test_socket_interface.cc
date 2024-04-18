#include "test/integration/filters/test_socket_interface.h"

#include <algorithm>

#include "envoy/common/exception.h"
#include "envoy/extensions/network/socket_interface/v3/default_socket_interface.pb.h"
#include "envoy/network/socket.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"

namespace Envoy {
namespace Network {

Api::IoCallUint64Result TestIoSocketHandle::sendmsg(const Buffer::RawSlice* slices,
                                                    uint64_t num_slice, int flags,
                                                    const Address::Ip* self_ip,
                                                    const Address::Instance& peer_address) {
  if (write_override_) {
    peer_address_override_ = peer_address;
    auto result = write_override_(this, slices, num_slice);
    peer_address_override_.reset();
    if (result.has_value()) {
      return std::move(result).value();
    }
  }
  return Test::IoSocketHandlePlatformImpl::sendmsg(slices, num_slice, flags, self_ip, peer_address);
}
Api::IoCallUint64Result TestIoSocketHandle::writev(const Buffer::RawSlice* slices,
                                                   uint64_t num_slice) {
  if (write_override_) {
    auto result = write_override_(this, slices, num_slice);
    if (result.has_value()) {
      return std::move(result).value();
    }
  }
  return Test::IoSocketHandlePlatformImpl::writev(slices, num_slice);
}

IoHandlePtr TestIoSocketHandle::accept(struct sockaddr* addr, socklen_t* addrlen) {
  auto result = Api::OsSysCallsSingleton::get().accept(fd_, addr, addrlen);
  if (SOCKET_INVALID(result.return_value_)) {
    return nullptr;
  }

  return std::make_unique<TestIoSocketHandle>(write_override_, result.return_value_, socket_v6only_,
                                              domain_);
}

IoHandlePtr TestIoSocketHandle::duplicate() {
  auto result = Api::OsSysCallsSingleton::get().duplicate(fd_);
  if (result.return_value_ == -1) {
    throw EnvoyException(fmt::format("duplicate failed for '{}': ({}) {}", fd_, result.errno_,
                                     errorDetails(result.errno_)));
  }
  return std::make_unique<TestIoSocketHandle>(write_override_, result.return_value_, socket_v6only_,
                                              domain_);
}

Api::SysCallIntResult TestIoSocketHandle::connect(Address::InstanceConstSharedPtr address) {
  if (write_override_) {
    auto result = write_override_(this, nullptr, 0);
    if (result.has_value()) {
      return Api::SysCallIntResult{-1, EINPROGRESS};
    }
  }
  return Test::IoSocketHandlePlatformImpl::connect(address);
}

IoHandlePtr TestSocketInterface::makeSocket(int socket_fd, bool socket_v6only,
                                            absl::optional<int> domain) const {
  return std::make_unique<TestIoSocketHandle>(write_override_proc_, socket_fd, socket_v6only,
                                              domain);
}

} // namespace Network
} // namespace Envoy

#include "test/integration/filters/test_socket_interface.h"

#include <algorithm>

#include "envoy/common/exception.h"
#include "envoy/extensions/network/socket_interface/v3/default_socket_interface.pb.h"
#include "envoy/network/socket.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/utility.h"
#include "common/network/address_impl.h"

namespace Envoy {
namespace Network {

Api::IoCallUint64Result TestIoSocketHandle::writev(const Buffer::RawSlice* slices,
                                                   uint64_t num_slice) {
  if (writev_override_) {
    auto result = writev_override_(this, slices, num_slice);
    if (result.has_value()) {
      return std::move(result).value();
    }
  }
  return IoSocketHandleImpl::writev(slices, num_slice);
}

IoHandlePtr TestIoSocketHandle::accept(struct sockaddr* addr, socklen_t* addrlen) {
  auto result = Api::OsSysCallsSingleton::get().accept(fd_, addr, addrlen);
  if (SOCKET_INVALID(result.rc_)) {
    return nullptr;
  }

  return std::make_unique<TestIoSocketHandle>(writev_override_, result.rc_, socket_v6only_);
}

IoHandlePtr TestSocketInterface::socket(Socket::Type socket_type, Address::Type addr_type,
                                        Address::IpVersion version, bool socket_v6only) const {
#if defined(__APPLE__) || defined(WIN32)
  int flags = 0;
#else
  int flags = SOCK_NONBLOCK;
#endif

  if (socket_type == Socket::Type::Stream) {
    flags |= SOCK_STREAM;
  } else {
    flags |= SOCK_DGRAM;
  }

  int domain;
  if (addr_type == Address::Type::Ip) {
    if (version == Address::IpVersion::v6) {
      domain = AF_INET6;
    } else {
      ASSERT(version == Address::IpVersion::v4);
      domain = AF_INET;
    }
  } else {
    ASSERT(addr_type == Address::Type::Pipe);
    domain = AF_UNIX;
  }

  const Api::SysCallSocketResult result = Api::OsSysCallsSingleton::get().socket(domain, flags, 0);
  RELEASE_ASSERT(SOCKET_VALID(result.rc_),
                 fmt::format("socket(2) failed, got error: {}", errorDetails(result.errno_)));
  auto io_handle =
      std::make_unique<TestIoSocketHandle>(writev_override_proc_, result.rc_, socket_v6only);

#if defined(__APPLE__) || defined(WIN32)
  // Cannot set SOCK_NONBLOCK as a ::socket flag.
  const int rc = Api::OsSysCallsSingleton::get().setsocketblocking(io_handle->fd(), false).rc_;
  RELEASE_ASSERT(!SOCKET_FAILURE(rc), "");
#endif

  return io_handle;
}

} // namespace Network
} // namespace Envoy

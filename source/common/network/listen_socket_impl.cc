#include "common/network/listen_socket_impl.h"

#include <sys/types.h>

#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/exception.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Network {

Api::SysCallIntResult ListenSocketImpl::bind(Network::Address::InstanceConstSharedPtr address) {
  local_address_ = address;

  const Api::SysCallIntResult result = SocketImpl::bind(local_address_);
  if (SOCKET_FAILURE(result.rc_)) {
    close();
    throw SocketBindException(fmt::format("cannot bind '{}': {}", local_address_->asString(),
                                          errorDetails(result.errno_)),
                              result.errno_);
  }
  return {0, 0};
}

void ListenSocketImpl::setListenSocketOptions(const Network::Socket::OptionsSharedPtr& options) {
  if (!Network::Socket::applyOptions(options, *this,
                                     envoy::config::core::v3::SocketOption::STATE_PREBIND)) {
    throw CreateListenerException("ListenSocket: Setting socket options failed");
  }
}

void ListenSocketImpl::setupSocket(const Network::Socket::OptionsSharedPtr& options,
                                   bool bind_to_port) {
  setListenSocketOptions(options);

  if (bind_to_port) {
    bind(local_address_);
  }
}

template <>
void NetworkListenSocket<NetworkSocketTrait<Socket::Type::Stream>>::setPrebindSocketOptions() {
// On Windows, SO_REUSEADDR does not restrict subsequent bind calls when there is a listener as on
// Linux and later BSD socket stacks
#ifndef WIN32
  int on = 1;
  auto status = setSocketOption(SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  RELEASE_ASSERT(status.rc_ != -1, "failed to set SO_REUSEADDR socket option");
#endif
}

template <>
void NetworkListenSocket<NetworkSocketTrait<Socket::Type::Datagram>>::setPrebindSocketOptions() {}

UdsListenSocket::UdsListenSocket(const Address::InstanceConstSharedPtr& address)
    : ListenSocketImpl(SocketInterfaceSingleton::get().socket(Socket::Type::Stream, address),
                       address) {
  RELEASE_ASSERT(io_handle_->fd() != -1, "");
  bind(local_address_);
}

UdsListenSocket::UdsListenSocket(IoHandlePtr&& io_handle,
                                 const Address::InstanceConstSharedPtr& address)
    : ListenSocketImpl(std::move(io_handle), address) {}

std::atomic<uint64_t> AcceptedSocketImpl::global_accepted_socket_count_;

} // namespace Network
} // namespace Envoy

#include "source/common/network/connection_socket_impl.h"

#include <memory>
#include <utility>

#include "source/common/network/socket_interface.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Network {

absl::StatusOr<std::unique_ptr<ConnectionSocketImpl>> ConnectionSocketImpl::create(
    Socket::Type type, const Address::InstanceConstSharedPtr& local_address,
    const Address::InstanceConstSharedPtr& remote_address, const SocketCreationOptions& options) {
  absl::StatusOr<IoHandlePtr> io_handle_or = ioHandleForAddr(type, local_address, options);
  if (!io_handle_or.ok()) {
    return io_handle_or.status();
  }

  auto socket = std::unique_ptr<ConnectionSocketImpl>(new ConnectionSocketImpl(
      std::move(*io_handle_or), type, local_address->type(), local_address, remote_address));
  return socket;
}

absl::StatusOr<std::unique_ptr<ClientSocketImpl>>
ClientSocketImpl::create(const Address::InstanceConstSharedPtr& remote_address,
                         const OptionsSharedPtr& options) {
  absl::StatusOr<IoHandlePtr> io_handle_or =
      ioHandleForAddr(Socket::Type::Stream, remote_address, {});
  if (!io_handle_or.ok()) {
    return io_handle_or.status();
  }

  auto socket = std::unique_ptr<ClientSocketImpl>(
      new ClientSocketImpl(std::move(*io_handle_or), remote_address, options));
  return socket;
}

} // namespace Network
} // namespace Envoy

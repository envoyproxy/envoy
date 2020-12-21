#include "common/network/tcp_listener_impl.h"

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/exception.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/file_event_impl.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_handle_impl.h"

namespace Envoy {
namespace Network {

const absl::string_view TcpListenerImpl::GlobalMaxCxRuntimeKey =
    "overload.global_downstream_max_connections";

bool TcpListenerImpl::rejectCxOverGlobalLimit() {
  // Enforce the global connection limit if necessary, immediately closing the accepted connection.
  Runtime::Loader* runtime = Runtime::LoaderSingleton::getExisting();

  if (runtime == nullptr) {
    // The runtime singleton won't exist in most unit tests that do not need global downstream limit
    // enforcement. Therefore, there is no need to enforce limits if the singleton doesn't exist.
    // TODO(tonya11en): Revisit this once runtime is made globally available.
    return false;
  }

  // If the connection limit is not set, don't limit the connections, but still track them.
  // TODO(tonya11en): In integration tests, threadsafeSnapshot is necessary since the FakeUpstreams
  // use a listener and do not run in a worker thread. In practice, this code path will always be
  // run on a worker thread, but to prevent failed assertions in test environments, threadsafe
  // snapshots must be used. This must be revisited.
  const uint64_t global_cx_limit = runtime->threadsafeSnapshot()->getInteger(
      GlobalMaxCxRuntimeKey, std::numeric_limits<uint64_t>::max());
  return AcceptedSocketImpl::acceptedSocketCount() >= global_cx_limit;
}

void TcpListenerImpl::onSocketEvent(short flags) {
  ASSERT(flags & (Event::FileReadyType::Read));

  // TODO(fcoras): Add limit on number of accepted calls per wakeup
  while (1) {
    if (!socket_->ioHandle().isOpen()) {
      PANIC(fmt::format("listener accept failure: {}", errorDetails(errno)));
    }

    sockaddr_storage remote_addr;
    socklen_t remote_addr_len = sizeof(remote_addr);

    IoHandlePtr io_handle =
        socket_->ioHandle().accept(reinterpret_cast<sockaddr*>(&remote_addr), &remote_addr_len);
    if (io_handle == nullptr) {
      break;
    }

    if (rejectCxOverGlobalLimit()) {
      // The global connection limit has been reached.
      io_handle->close();
      cb_.onReject(TcpListenerCallbacks::RejectCause::GlobalCxLimit);
      continue;
    } else if (random_.bernoulli(reject_fraction_)) {
      io_handle->close();
      cb_.onReject(TcpListenerCallbacks::RejectCause::OverloadAction);
      continue;
    }

    // Get the local address from the new socket if the listener is listening on IP ANY
    // (e.g., 0.0.0.0 for IPv4) (local_address_ is nullptr in this case).
    const Address::InstanceConstSharedPtr& local_address =
        local_address_ ? local_address_ : io_handle->localAddress();

    // The accept() call that filled in remote_addr doesn't fill in more than the sa_family field
    // for Unix domain sockets; apparently there isn't a mechanism in the kernel to get the
    // `sockaddr_un` associated with the client socket when starting from the server socket.
    // We work around this by using our own name for the socket in this case.
    // Pass the 'v6only' parameter as true if the local_address is an IPv6 address. This has no
    // effect if the socket is a v4 socket, but for v6 sockets this will create an IPv4 remote
    // address if an IPv4 local_address was created from an IPv6 mapped IPv4 address.
    const Address::InstanceConstSharedPtr& remote_address =
        (remote_addr.ss_family == AF_UNIX)
            ? io_handle->peerAddress()
            : Address::addressFromSockAddr(remote_addr, remote_addr_len,
                                           local_address->ip()->version() ==
                                               Address::IpVersion::v6);

    cb_.onAccept(
        std::make_unique<AcceptedSocketImpl>(std::move(io_handle), local_address, remote_address));
  }
}

void TcpListenerImpl::setupServerSocket(Event::DispatcherImpl& dispatcher, Socket& socket) {
  socket.ioHandle().listen(backlog_size_);

  // Although onSocketEvent drains to completion, use level triggered mode to avoid potential
  // loss of the trigger due to transient accept errors.
  socket.ioHandle().initializeFileEvent(
      dispatcher, [this](uint32_t events) -> void { onSocketEvent(events); },
      Event::FileTriggerType::Level, Event::FileReadyType::Read);

  if (!Network::Socket::applyOptions(socket.options(), socket,
                                     envoy::config::core::v3::SocketOption::STATE_LISTENING)) {
    throw CreateListenerException(fmt::format("cannot set post-listen socket option on socket: {}",
                                              socket.localAddress()->asString()));
  }
}

TcpListenerImpl::TcpListenerImpl(Event::DispatcherImpl& dispatcher, Random::RandomGenerator& random,
                                 SocketSharedPtr socket, TcpListenerCallbacks& cb,
                                 bool bind_to_port, uint32_t backlog_size)
    : BaseListenerImpl(dispatcher, std::move(socket)), cb_(cb), backlog_size_(backlog_size),
      random_(random), reject_fraction_(0.0) {
  if (bind_to_port) {
    setupServerSocket(dispatcher, *socket_);
  }
}

void TcpListenerImpl::enable() { socket_->ioHandle().enableFileEvents(Event::FileReadyType::Read); }

void TcpListenerImpl::disable() { socket_->ioHandle().enableFileEvents(0); }

void TcpListenerImpl::setRejectFraction(const float reject_fraction) {
  ASSERT(0 <= reject_fraction && reject_fraction <= 1);
  reject_fraction_ = reject_fraction;
}

} // namespace Network
} // namespace Envoy

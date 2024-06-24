#include "source/common/network/tcp_listener_impl.h"

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/event/file_event_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/runtime/runtime_keys.h"

namespace Envoy {
namespace Network {

bool TcpListenerImpl::rejectCxOverGlobalLimit() const {
  // Enforce the global connection limit if necessary, immediately closing the accepted connection.
  if (ignore_global_conn_limit_) {
    return false;
  }
  // TODO(nezdolik): deprecate `overload.global_downstream_max_connections` key once
  // downstream connections monitor extension is stable.
  if (track_global_cx_limit_in_overload_manager_) {
    // Check if runtime flag `overload.global_downstream_max_connections` is configured
    // simultaneously with downstream connections monitor in overload manager.
    if (runtime_.threadsafeSnapshot()->get(Runtime::Keys::GlobalMaxCxRuntimeKey)) {
      ENVOY_LOG_ONCE_MISC(
          warn,
          "Global downstream connections limits is configured via deprecated runtime key {} and in "
          "{}. Using overload manager config.",
          Runtime::Keys::GlobalMaxCxRuntimeKey,
          Server::OverloadProactiveResources::get().GlobalDownstreamMaxConnections);
    }
    // Try to allocate resource within overload manager. We do it once here, instead of checking if
    // it is possible to allocate resource in this method and then actually allocating it later in
    // the code to avoid race conditions.
    return !(overload_state_->tryAllocateResource(
        Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections, 1));
  } else {
    // If the connection limit is not set, don't limit the connections, but still track them.
    // TODO(tonya11en): In integration tests, threadsafeSnapshot is necessary since the
    // FakeUpstreams use a listener and do not run in a worker thread. In practice, this code path
    // will always be run on a worker thread, but to prevent failed assertions in test environments,
    // threadsafe snapshots must be used. This must be revisited.
    const uint64_t global_cx_limit = runtime_.threadsafeSnapshot()->getInteger(
        Runtime::Keys::GlobalMaxCxRuntimeKey, std::numeric_limits<uint64_t>::max());
    return AcceptedSocketImpl::acceptedSocketCount() >= global_cx_limit;
  }
}

void TcpListenerImpl::onSocketEvent(short flags) {
  ASSERT(bind_to_port_);
  ASSERT(flags & (Event::FileReadyType::Read));

  uint32_t connections_accepted_from_kernel_count = 0;
  for (; connections_accepted_from_kernel_count < max_connections_to_accept_per_socket_event_;
       ++connections_accepted_from_kernel_count) {
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
    } else if ((listener_accept_ != nullptr && listener_accept_->shouldShedLoad()) ||
               random_.bernoulli(reject_fraction_)) {
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

    const Address::InstanceConstSharedPtr remote_address =
        (remote_addr.ss_family == AF_UNIX)
            ? io_handle->peerAddress()
            : Address::addressFromSockAddrOrThrow(remote_addr, remote_addr_len,
                                                  local_address->ip()->version() ==
                                                      Address::IpVersion::v6);

    cb_.onAccept(std::make_unique<AcceptedSocketImpl>(std::move(io_handle), local_address,
                                                      remote_address, overload_state_,
                                                      track_global_cx_limit_in_overload_manager_));
  }

  ENVOY_LOG_MISC(trace, "TcpListener accepted {} new connections.",
                 connections_accepted_from_kernel_count);
  cb_.recordConnectionsAcceptedOnSocketEvent(connections_accepted_from_kernel_count);
}

TcpListenerImpl::TcpListenerImpl(Event::Dispatcher& dispatcher, Random::RandomGenerator& random,
                                 Runtime::Loader& runtime, SocketSharedPtr socket,
                                 TcpListenerCallbacks& cb, bool bind_to_port,
                                 bool ignore_global_conn_limit, bool bypass_overload_manager,
                                 uint32_t max_connections_to_accept_per_socket_event,
                                 Server::ThreadLocalOverloadStateOptRef overload_state)
    : BaseListenerImpl(dispatcher, std::move(socket)), cb_(cb), random_(random), runtime_(runtime),
      bind_to_port_(bind_to_port), reject_fraction_(0.0),
      ignore_global_conn_limit_(ignore_global_conn_limit),
      bypass_overload_manager_(bypass_overload_manager),
      max_connections_to_accept_per_socket_event_(max_connections_to_accept_per_socket_event),
      overload_state_(overload_state),
      track_global_cx_limit_in_overload_manager_(
          overload_state_
              ? overload_state_->isResourceMonitorEnabled(
                    Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections)
              : false) {
  if (bind_to_port) {
    // Use level triggered mode to avoid potential loss of the trigger due to
    // transient accept errors or early termination due to accepting
    // max_connections_to_accept_per_socket_event connections.
    socket_->ioHandle().initializeFileEvent(
        dispatcher,
        [this](uint32_t events) {
          onSocketEvent(events);
          return absl::OkStatus();
        },
        Event::FileTriggerType::Level, Event::FileReadyType::Read);
  }
}

void TcpListenerImpl::enable() {
  if (bind_to_port_) {
    socket_->ioHandle().enableFileEvents(Event::FileReadyType::Read);
  } else {
    ENVOY_LOG_MISC(debug, "The listener cannot be enabled since it's not bind to port.");
  }
}

void TcpListenerImpl::disable() {
  if (bind_to_port_) {
    socket_->ioHandle().enableFileEvents(0);
  } else {
    ENVOY_LOG_MISC(debug, "The listener cannot be disable since it's not bind to port.");
  }
}

void TcpListenerImpl::setRejectFraction(const UnitFloat reject_fraction) {
  reject_fraction_ = reject_fraction;
}

void TcpListenerImpl::configureLoadShedPoints(
    Server::LoadShedPointProvider& load_shed_point_provider) {
  listener_accept_ =
      load_shed_point_provider.getLoadShedPoint(Server::LoadShedPointName::get().TcpListenerAccept);
  ENVOY_LOG_ONCE_MISC_IF(
      trace, listener_accept_ == nullptr,
      "LoadShedPoint envoy.load_shed_points.tcp_listener_accept is not found. Is it configured?");
}

bool TcpListenerImpl::shouldBypassOverloadManager() const { return bypass_overload_manager_; }

} // namespace Network
} // namespace Envoy

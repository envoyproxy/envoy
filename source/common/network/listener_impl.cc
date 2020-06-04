#include "common/network/listener_impl.h"

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

#include "event2/listener.h"

namespace Envoy {
namespace Network {

const absl::string_view ListenerImpl::GlobalMaxCxRuntimeKey =
    "overload.global_downstream_max_connections";

bool ListenerImpl::rejectCxOverGlobalLimit() {
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

void ListenerImpl::listenCallback(evconnlistener*, evutil_socket_t fd, sockaddr* remote_addr,
                                  int remote_addr_len, void* arg) {
  ListenerImpl* listener = static_cast<ListenerImpl*>(arg);

  // Wrap raw socket fd in IoHandle.
  IoHandlePtr io_handle = SocketInterfaceSingleton::get().socket(fd);

  if (rejectCxOverGlobalLimit()) {
    // The global connection limit has been reached.
    io_handle->close();
    listener->cb_.onReject();
    return;
  }

  // Get the local address from the new socket if the listener is listening on IP ANY
  // (e.g., 0.0.0.0 for IPv4) (local_address_ is nullptr in this case).
  const Address::InstanceConstSharedPtr& local_address =
      listener->local_address_ ? listener->local_address_ : io_handle->localAddress();

  // The accept() call that filled in remote_addr doesn't fill in more than the sa_family field
  // for Unix domain sockets; apparently there isn't a mechanism in the kernel to get the
  // `sockaddr_un` associated with the client socket when starting from the server socket.
  // We work around this by using our own name for the socket in this case.
  // Pass the 'v6only' parameter as true if the local_address is an IPv6 address. This has no effect
  // if the socket is a v4 socket, but for v6 sockets this will create an IPv4 remote address if an
  // IPv4 local_address was created from an IPv6 mapped IPv4 address.
  const Address::InstanceConstSharedPtr& remote_address =
      (remote_addr->sa_family == AF_UNIX)
          ? io_handle->peerAddress()
          : Address::addressFromSockAddr(*reinterpret_cast<const sockaddr_storage*>(remote_addr),
                                         remote_addr_len,
                                         local_address->ip()->version() == Address::IpVersion::v6);
  listener->cb_.onAccept(
      std::make_unique<AcceptedSocketImpl>(std::move(io_handle), local_address, remote_address));
}

void ListenerImpl::setupServerSocket(Event::DispatcherImpl& dispatcher, Socket& socket) {
  listener_.reset(
      evconnlistener_new(&dispatcher.base(), listenCallback, this, 0, -1, socket.ioHandle().fd()));

  if (!listener_) {
    throw CreateListenerException(
        fmt::format("cannot listen on socket: {}", socket.localAddress()->asString()));
  }

  if (!Network::Socket::applyOptions(socket.options(), socket,
                                     envoy::config::core::v3::SocketOption::STATE_LISTENING)) {
    throw CreateListenerException(fmt::format("cannot set post-listen socket option on socket: {}",
                                              socket.localAddress()->asString()));
  }

  evconnlistener_set_error_cb(listener_.get(), errorCallback);
}

ListenerImpl::ListenerImpl(Event::DispatcherImpl& dispatcher, SocketSharedPtr socket,
                           ListenerCallbacks& cb, bool bind_to_port)
    : BaseListenerImpl(dispatcher, std::move(socket)), cb_(cb), listener_(nullptr) {
  if (bind_to_port) {
    setupServerSocket(dispatcher, *socket_);
  }
}

void ListenerImpl::errorCallback(evconnlistener*, void*) {
  // We should never get an error callback. This can happen if we run out of FDs or memory. In those
  // cases just crash.
  PANIC(fmt::format("listener accept failure: {}", errorDetails(errno)));
}

void ListenerImpl::enable() {
  if (listener_.get()) {
    evconnlistener_enable(listener_.get());
  }
}

void ListenerImpl::disable() {
  if (listener_.get()) {
    evconnlistener_disable(listener_.get());
  }
}

} // namespace Network
} // namespace Envoy

#include "listener_impl.h"
#include "utility.h"

#include "envoy/common/exception.h"
#include "envoy/network/connection_handler.h"

#include "common/common/empty_string.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/file_event_impl.h"
#include "common/network/address_impl.h"
#include "common/network/connection_impl.h"
#include "common/ssl/connection_impl.h"

#include "event2/listener.h"

namespace Network {

Address::InstancePtr ListenerImpl::getOriginalDst(int fd) { return Utility::getOriginalDst(fd); }

void ListenerImpl::listenCallback(evconnlistener*, evutil_socket_t fd, sockaddr* remote_addr, int,
                                  void* arg) {
  ListenerImpl* listener = static_cast<ListenerImpl*>(arg);

  Address::InstancePtr final_local_address = listener->socket_.localAddress();
  if (listener->use_original_dst_ && final_local_address->type() == Address::Type::Ip) {
    Address::InstancePtr original_local_address = listener->getOriginalDst(fd);
    if (original_local_address) {
      final_local_address = original_local_address;
    }

    // A listener that has the use_original_dst flag set to true can still receive connections
    // that are NOT redirected using iptables. If a connection was not redirected,
    // the address and port returned by getOriginalDst() match the listener port.
    // In this case the listener handles the connection directly and does not hand it off.
    if (listener->socket_.localAddress()->ip()->port() != final_local_address->ip()->port()) {
      ListenerImpl* new_listener = dynamic_cast<ListenerImpl*>(
          listener->connection_handler_.findListenerByPort(final_local_address->ip()->port()));

      if (new_listener != nullptr) {
        listener = new_listener;
      }
    }
  }

  if (listener->use_proxy_proto_) {
    listener->proxy_protocol_.newConnection(listener->dispatcher_, fd, *listener);
  } else {
    Address::InstancePtr final_remote_address;
    if (remote_addr->sa_family == AF_INET) {
      final_remote_address.reset(
          new Address::Ipv4Instance(reinterpret_cast<sockaddr_in*>(remote_addr)));
    } else {
      // TODO(mklein123): IPv6 support.
      ASSERT(remote_addr->sa_family == AF_UNIX);
      final_remote_address.reset(
          new Address::PipeInstance(reinterpret_cast<sockaddr_un*>(remote_addr)));
    }

    listener->newConnection(fd, final_remote_address, final_local_address);
  }
}

ListenerImpl::ListenerImpl(Network::ConnectionHandler& conn_handler,
                           Event::DispatcherImpl& dispatcher, ListenSocket& socket,
                           ListenerCallbacks& cb, Stats::Store& stats_store, bool bind_to_port,
                           bool use_proxy_proto, bool use_orig_dst)
    : connection_handler_(conn_handler), dispatcher_(dispatcher), socket_(socket), cb_(cb),
      bind_to_port_(bind_to_port), use_proxy_proto_(use_proxy_proto), proxy_protocol_(stats_store),
      use_original_dst_(use_orig_dst), listener_(nullptr) {

  if (bind_to_port_) {
    listener_.reset(
        evconnlistener_new(&dispatcher_.base(), listenCallback, this, 0, -1, socket.fd()));

    if (!listener_) {
      throw CreateListenerException(
          fmt::format("cannot listen on socket: {}", socket.localAddress()->asString()));
    }

    evconnlistener_set_error_cb(listener_.get(), errorCallback);
  }
}

void ListenerImpl::errorCallback(evconnlistener*, void*) {
  // We should never get an error callback. This can happen if we run out of FDs or memory. In those
  // cases just crash.
  PANIC(fmt::format("listener accept failure: {}", strerror(errno)));
}

void ListenerImpl::newConnection(int fd, Address::InstancePtr remote_address,
                                 Address::InstancePtr local_address) {
  ConnectionPtr new_connection(new ConnectionImpl(dispatcher_, fd, remote_address, local_address));
  cb_.onNewConnection(std::move(new_connection));
}

void SslListenerImpl::newConnection(int fd, Address::InstancePtr remote_address,
                                    Address::InstancePtr local_address) {
  ConnectionPtr new_connection(new Ssl::ConnectionImpl(dispatcher_, fd, remote_address,
                                                       local_address, ssl_ctx_,
                                                       Ssl::ConnectionImpl::InitialState::Server));
  cb_.onNewConnection(std::move(new_connection));
}

} // Network

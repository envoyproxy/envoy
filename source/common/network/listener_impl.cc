#include "common/network/listener_impl.h"

#include "envoy/common/exception.h"
#include "envoy/network/connection_handler.h"

#include "common/common/empty_string.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/file_event_impl.h"
#include "common/network/address_impl.h"
#include "common/network/connection_impl.h"
#include "common/network/utility.h"
#include "common/ssl/connection_impl.h"

#include "event2/listener.h"

namespace Network {

Address::InstanceConstSharedPtr ListenerImpl::getOriginalDst(int fd) {
  return Utility::getOriginalDst(fd);
}

void ListenerImpl::listenCallback(evconnlistener*, evutil_socket_t fd, sockaddr* remote_addr, int,
                                  void* arg) {
  ListenerImpl* listener = static_cast<ListenerImpl*>(arg);

  Address::InstanceConstSharedPtr final_local_address = listener->socket_.localAddress();
  if (listener->options_.use_original_dst_ && final_local_address->type() == Address::Type::Ip) {
    Address::InstanceConstSharedPtr original_local_address = listener->getOriginalDst(fd);
    if (original_local_address) {
      final_local_address = original_local_address;
    }

    // Hands off redirected connections (from iptables) to the listener associated with the
    // original destination address. If there is no listener associated with the original
    // destination address, the connection is handled by the listener that receives it.
    // Note: A listener that has the use_original_dst flag set to true can still receive.
    // connections that are NOT redirected using iptables. If a connection was not redirected,
    // the address returned by getOriginalDst() match the listener address. In this case the
    // listener handles the connection directly and does not hand it off.
    if (listener->socket_.localAddress() != final_local_address) {
      ListenerImpl* new_listener = dynamic_cast<ListenerImpl*>(
          listener->connection_handler_.findListenerByAddress(*final_local_address));

      if (new_listener != nullptr) {
        listener = new_listener;
      }
    }
  }

  if (listener->options_.use_proxy_proto_) {
    listener->proxy_protocol_.newConnection(listener->dispatcher_, fd, *listener);
  } else {
    Address::InstanceConstSharedPtr final_remote_address;
    if (remote_addr->sa_family == AF_INET) {
      final_remote_address.reset(
          new Address::Ipv4Instance(reinterpret_cast<sockaddr_in*>(remote_addr)));
    } else {
      // TODO(mattklein123): IPv6 support.
      ASSERT(remote_addr->sa_family == AF_UNIX);
      final_remote_address.reset(
          new Address::PipeInstance(reinterpret_cast<sockaddr_un*>(remote_addr)));
    }
    listener->newConnection(fd, final_remote_address, final_local_address);
  }
}

ListenerImpl::ListenerImpl(Network::ConnectionHandler& conn_handler,
                           Event::DispatcherImpl& dispatcher, ListenSocket& socket,
                           ListenerCallbacks& cb, Stats::Scope& scope,
                           const Network::ListenerOptions& listener_options)
    : connection_handler_(conn_handler), dispatcher_(dispatcher), socket_(socket), cb_(cb),
      proxy_protocol_(scope), options_(listener_options), listener_(nullptr) {

  if (options_.bind_to_port_) {
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

void ListenerImpl::newConnection(int fd, Address::InstanceConstSharedPtr remote_address,
                                 Address::InstanceConstSharedPtr local_address) {
  ConnectionPtr new_connection(new ConnectionImpl(dispatcher_, fd, remote_address, local_address));
  new_connection->setReadBufferLimit(options_.per_connection_buffer_limit_bytes_);
  cb_.onNewConnection(std::move(new_connection));
}

void SslListenerImpl::newConnection(int fd, Address::InstanceConstSharedPtr remote_address,
                                    Address::InstanceConstSharedPtr local_address) {
  ConnectionPtr new_connection(new Ssl::ConnectionImpl(dispatcher_, fd, remote_address,
                                                       local_address, ssl_ctx_,
                                                       Ssl::ConnectionImpl::InitialState::Server));
  new_connection->setReadBufferLimit(options_.per_connection_buffer_limit_bytes_);
  cb_.onNewConnection(std::move(new_connection));
}

} // Network

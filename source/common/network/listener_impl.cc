#include "listener_impl.h"
#include "utility.h"

#include "envoy/common/exception.h"

#include "common/common/empty_string.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/file_event_impl.h"
#include "common/network/connection_impl.h"
#include "common/ssl/connection_impl.h"

#include "server/connection_handler.h"

#include "event2/listener.h"

namespace Network {

void ListenerImpl::listenCallback(evconnlistener*, evutil_socket_t fd, sockaddr* addr, int,
                                  void* arg) {
  ListenerImpl* listener = static_cast<ListenerImpl*>(arg);

  if (listener->use_original_dst_ && listener->connection_handler_ != nullptr) {
    struct sockaddr_storage orig_dst_addr;
    memset(&orig_dst_addr, 0, sizeof(orig_dst_addr));

    bool success = Utility::getOriginalDst(fd, &orig_dst_addr);

    if (success) {
      std::string orig_sock_name = std::to_string(
          listener->getAddressPort(reinterpret_cast<struct sockaddr*>(&orig_dst_addr)));

      if (listener->socket_.name() != orig_sock_name) {
        ListenerImpl* new_listener =
            static_cast<ListenerImpl*>(listener->connection_handler_->findListener(orig_sock_name));

        if (new_listener != nullptr) {
          listener = new_listener;
        }
      }
    }
  }

  listener->newConnection(fd, addr);
}

ListenerImpl::ListenerImpl(Event::DispatcherImpl& dispatcher, ListenSocket& socket,
                           ListenerCallbacks& cb, Stats::Store& stats_store, bool bind_to_port,
                           bool use_proxy_proto, bool use_orig_dst)
    : dispatcher_(dispatcher), socket_(socket), cb_(cb), bind_to_port_(bind_to_port),
      use_proxy_proto_(use_proxy_proto), proxy_protocol_(stats_store),
      use_original_dst_(use_orig_dst), connection_handler_(nullptr) {

  if (bind_to_port_) {
    listener_.reset(
        evconnlistener_new(&dispatcher_.base(), listenCallback, this, 0, -1, socket.fd()));
  } else {
    listener_.reset(evconnlistener_new(&dispatcher_.base(), nullptr, this, 0, -1, socket.fd()));
  }

  if (!listener_) {
    throw CreateListenerException(fmt::format("cannot listen on socket: {}", socket.name()));
  }

  evconnlistener_set_error_cb(listener_.get(), errorCallback);
}

void ListenerImpl::errorCallback(evconnlistener*, void*) {
  // We should never get an error callback. This can happen if we run out of FDs or memory. In those
  // cases just crash.
  PANIC(fmt::format("listener accept failure: {}", strerror(errno)));
}

void ListenerImpl::newConnection(int fd, sockaddr* addr) {
  evutil_make_socket_nonblocking(fd);
  if (use_proxy_proto_) {
    proxy_protocol_.newConnection(dispatcher_, fd, *this);
  } else {
    newConnection(fd, getAddressName(addr));
  }
}

void ListenerImpl::newConnection(int fd, const std::string& remote_address) {
  ConnectionPtr new_connection(new ConnectionImpl(dispatcher_, fd, remote_address));
  cb_.onNewConnection(std::move(new_connection));
}

void SslListenerImpl::newConnection(int fd, sockaddr* addr) {
  if (use_proxy_proto_) {
    proxy_protocol_.newConnection(dispatcher_, fd, *this);
  } else {
    newConnection(fd, getAddressName(addr));
  }
}

void SslListenerImpl::newConnection(int fd, const std::string& remote_address) {
  ConnectionPtr new_connection(new Ssl::ConnectionImpl(dispatcher_, fd, remote_address, ssl_ctx_,
                                                       Ssl::ConnectionImpl::InitialState::Server));
  cb_.onNewConnection(std::move(new_connection));
}

const std::string ListenerImpl::getAddressName(sockaddr* addr) {
  return (addr->sa_family == AF_INET)
             ? Utility::getAddressName(reinterpret_cast<sockaddr_in*>(addr))
             : EMPTY_STRING;
}

uint16_t ListenerImpl::getAddressPort(sockaddr* addr) {
  return (addr->sa_family == AF_INET) ? ntohs(reinterpret_cast<sockaddr_in*>(addr)->sin_port) : 0;
}

} // Network

#pragma once

#include "envoy/network/connection_handler.h"
#include "envoy/network/listener.h"

#include "common/event/dispatcher_impl.h"
#include "common/event/libevent.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/proxy_protocol.h"

#include "event2/event.h"

namespace Envoy {
namespace Network {

/**
 * libevent implementation of Network::Listener.
 */
class ListenerImpl : public Listener {
public:
  ListenerImpl(Network::ConnectionHandler& conn_handler, Event::DispatcherImpl& dispatcher,
               ListenSocket& socket, ListenerCallbacks& cb, Stats::Scope& scope,
               const ListenerOptions& listener_options);

  /**
   * Accept/process a new connection.
   * @param fd supplies the new connection's fd.
   * @param remote_address supplies the remote address for the new connection.
   * @param local_address supplies the local address for the new connection.
   */
  virtual void newConnection(int fd, Address::InstanceConstSharedPtr remote_address,
                             Address::InstanceConstSharedPtr local_address,
                             bool using_original_dst);

  /**
   * @return the socket supplied to the listener at construction time
   */
  ListenSocket& socket() { return socket_; }

protected:
  virtual Address::InstanceConstSharedPtr getLocalAddress(int fd);
  virtual Address::InstanceConstSharedPtr getOriginalDst(int fd);

  Network::ConnectionHandler& connection_handler_;
  Event::DispatcherImpl& dispatcher_;
  ListenSocket& socket_;
  ListenerCallbacks& cb_;
  ProxyProtocol proxy_protocol_;
  const ListenerOptions options_;

private:
  static void errorCallback(evconnlistener* listener, void* context);
  static void listenCallback(evconnlistener*, evutil_socket_t fd, sockaddr* remote_addr,
                             int remote_addr_len, void* arg);

  Event::Libevent::ListenerPtr listener_;
};

class SslListenerImpl : public ListenerImpl {
public:
  SslListenerImpl(Network::ConnectionHandler& conn_handler, Event::DispatcherImpl& dispatcher,
                  Ssl::Context& ssl_ctx, ListenSocket& socket, ListenerCallbacks& cb,
                  Stats::Scope& scope, const Network::ListenerOptions& listener_options)
      : ListenerImpl(conn_handler, dispatcher, socket, cb, scope, listener_options),
        ssl_ctx_(ssl_ctx) {}

  // ListenerImpl
  void newConnection(int fd, Address::InstanceConstSharedPtr remote_address,
                     Address::InstanceConstSharedPtr local_address,
                     bool using_original_dst) override;

private:
  Ssl::Context& ssl_ctx_;
};

} // namespace Network
} // namespace Envoy

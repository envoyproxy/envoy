#pragma once

#include "listen_socket_impl.h"
#include "proxy_protocol.h"

#include "envoy/network/listener.h"

#include "common/event/dispatcher_impl.h"
#include "common/event/libevent.h"

namespace Network {

/**
 * libevent implementation of Network::Listener.
 */
class ListenerImpl : public Listener {
public:
  ListenerImpl(Event::DispatcherImpl& dispatcher, ListenSocket& socket, ListenerCallbacks& cb,
               Stats::Store& stats_store, bool use_proxy_proto);

  /**
   * Accept/process a new connection.
   * @param fd supplies the new connection's fd.
   * @param remote_address supplies the remote address for the new connection.
   */
  virtual void newConnection(int fd, sockaddr* addr);

  /**
   * Accept/process a new connection with the given remote address.
   * @param fd supplies the new connection's fd.
   * @param remote_address supplies the remote address for the new connection.
   */
  virtual void newConnection(int fd, const std::string& remote_address);

protected:
  const std::string getAddressName(sockaddr* addr);

  Event::DispatcherImpl& dispatcher_;
  ListenerCallbacks& cb_;
  bool use_proxy_proto_;
  ProxyProtocol proxy_protocol_;

private:
  static void errorCallback(evconnlistener* listener, void* context);

  Event::Libevent::ListenerPtr listener_;
};

class SslListenerImpl : public ListenerImpl {
public:
  SslListenerImpl(Event::DispatcherImpl& dispatcher, Ssl::Context& ssl_ctx, ListenSocket& socket,
                  ListenerCallbacks& cb, Stats::Store& stats_store, bool use_proxy_proto)
      : ListenerImpl(dispatcher, socket, cb, stats_store, use_proxy_proto), ssl_ctx_(ssl_ctx) {}

  // ListenerImpl
  void newConnection(int fd, sockaddr* addr) override;
  void newConnection(int fd, const std::string& remote_address) override;

private:
  Ssl::Context& ssl_ctx_;
};

} // Network

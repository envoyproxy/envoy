#pragma once

#include "listen_socket_impl.h"
#include "proxy_protocol.h"

#include "envoy/network/listener.h"

#include "common/event/dispatcher_impl.h"
#include "common/event/libevent.h"

#include "event2/event.h"

class ConnectionHandler;

namespace Network {

/**
 * libevent implementation of Network::Listener.
 */
class ListenerImpl : public Listener {
public:
  ListenerImpl(Event::DispatcherImpl& dispatcher, ListenSocket& socket, ListenerCallbacks& cb,
               Stats::Store& stats_store, bool bind_to_port, bool use_proxy_proto,
               bool use_orig_dst);

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

  /**
   * @return the socket supplied to the listener at construction time
   */
  ListenSocket& socket() { return socket_; }

  /**
   * Set a pointer to the connection handler that handles connections for this listener
   * Invoked when the listener becomes active.
   * @param conn_handler the connection handler associated to this listener
   */
  void connectionHandler(ConnectionHandler* conn_handler) { connection_handler_ = conn_handler; }

protected:
  const std::string getAddressName(sockaddr* addr);
  uint16_t getAddressPort(sockaddr* addr);

  Event::DispatcherImpl& dispatcher_;
  ListenSocket& socket_;
  ListenerCallbacks& cb_;
  bool bind_to_port_;
  bool use_proxy_proto_;
  ProxyProtocol proxy_protocol_;
  bool use_original_dst_;
  ConnectionHandler* connection_handler_;

private:
  static void errorCallback(evconnlistener* listener, void* context);
  static void listenCallback(evconnlistener*, evutil_socket_t fd, sockaddr* addr, int, void* arg);

  Event::Libevent::ListenerPtr listener_;
};

class SslListenerImpl : public ListenerImpl {
public:
  SslListenerImpl(Event::DispatcherImpl& dispatcher, Ssl::Context& ssl_ctx, ListenSocket& socket,
                  ListenerCallbacks& cb, Stats::Store& stats_store, bool bind_to_port,
                  bool use_proxy_proto, bool use_orig_dst)
      : ListenerImpl(dispatcher, socket, cb, stats_store, bind_to_port, use_proxy_proto,
                     use_orig_dst),
        ssl_ctx_(ssl_ctx) {}

  // ListenerImpl
  void newConnection(int fd, sockaddr* addr) override;
  void newConnection(int fd, const std::string& remote_address) override;

private:
  Ssl::Context& ssl_ctx_;
};

} // Network

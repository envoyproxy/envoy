#pragma once

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener.h"
#include "envoy/network/listen_socket.h"
#include "envoy/ssl/context.h"

namespace Network {

/**
 * Abstract connection handler.
 */
class ConnectionHandler {
public:
  virtual ~ConnectionHandler(){};

  virtual uint64_t numConnections() PURE;

  /**
   * Adds listener to the handler.
   * @param factory supplies the configuration factory for new connections.
   * @param socket supplies the already bound socket to listen on.
   * @param bind_to_port specifies if the listener should actually bind to the port.
   *        a listener that doesn't bind can only receive connections redirected from
   *        other listeners that set use_origin_dst to true
   * @param use_proxy_proto whether to use the PROXY Protocol V1
   * (http://www.haproxy.org/download/1.5/doc/proxy-protocol.txt)
   * @param use_orig_dst if a connection was redirected to this port using iptables,
   *        allow the listener to hand it off to the listener associated to the original port
   * @param per_connection_buffer_limit_bytes soft limit on size of the listener's new connection
   *        read and write buffers.
   */
  virtual void addListener(Network::FilterChainFactory& factory, Network::ListenSocket& socket,
                           bool bind_to_port, bool use_proxy_proto, bool use_orig_dst,
                           size_t per_connection_buffer_limit_bytes) PURE;

  /**
   * Adds listener to the handler.
   * @param factory supplies the configuration factory for new connections.
   * @param socket supplies the already bound socket to listen on.
   * @param bind_to_port specifies if the listener should actually bind to the port.
   *        a listener that doesn't bind can only receive connections redirected from
   *        other listeners that set use_origin_dst to true
   * @param use_proxy_proto whether to use the PROXY Protocol V1
   * (http://www.haproxy.org/download/1.5/doc/proxy-protocol.txt)
   * @param use_orig_dst if a connection was redirected to this port using iptables,
   *        allow the listener to hand it off to the listener associated to the original port
   * @param per_connection_buffer_limit_bytes soft limit on size of the listener's new connection
   *        read and write buffers.
   */
  virtual void addSslListener(Network::FilterChainFactory& factory, Ssl::ServerContext& ssl_ctx,
                              Network::ListenSocket& socket, bool bind_to_port,
                              bool use_proxy_proto, bool use_orig_dst,
                              size_t per_connection_buffer_limit_bytes) PURE;

  /**
   * Find a listener based on the provided listener port value.
   * @param port supplies the port value
   * @return a pointer to the listener or nullptr if not found.
   * Ownership of the listener is NOT transferred
   */
  virtual Network::Listener* findListenerByPort(uint32_t port) PURE;

  /**
   * Close and destroy all listeners.
   */
  virtual void closeListeners() PURE;
};

typedef std::unique_ptr<ConnectionHandler> ConnectionHandlerPtr;

} // Network

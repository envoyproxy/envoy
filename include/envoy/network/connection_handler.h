#pragma once

#include <cstdint>
#include <memory>

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"
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
   * @param scope supplies the stats scope to use for listener specific stats.
   * @param listener_options listener configuration options.
   */
  virtual void addListener(Network::FilterChainFactory& factory, Network::ListenSocket& socket,
                           Stats::Scope& scope,
                           const Network::ListenerOptions& listener_options) PURE;

  /**
   * Adds listener to the handler.
   * @param factory supplies the configuration factory for new connections.
   * @param socket supplies the already bound socket to listen on.
   * @param scope supplies the stats scope to use for listener specific stats.
   * @param listener_options listener configuration options.
   */
  virtual void addSslListener(Network::FilterChainFactory& factory, Ssl::ServerContext& ssl_ctx,
                              Network::ListenSocket& socket, Stats::Scope& scope,
                              const Network::ListenerOptions& listener_options) PURE;

  /**
   * Find a listener based on the provided listener address value.
   * @param address supplies the address value.
   * @return a pointer to the listener or nullptr if not found.
   * Ownership of the listener is NOT transferred
   */
  virtual Network::Listener* findListenerByAddress(const Network::Address::Instance& address) PURE;

  /**
   * Close and destroy all listeners.
   */
  virtual void closeListeners() PURE;
};

typedef std::unique_ptr<ConnectionHandler> ConnectionHandlerPtr;

} // Network

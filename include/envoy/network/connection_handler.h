#pragma once

#include <cstdint>
#include <memory>

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"
#include "envoy/ssl/context.h"

namespace Envoy {
namespace Network {

/**
 * Abstract connection handler.
 */
class ConnectionHandler {
public:
  virtual ~ConnectionHandler() {}

  /**
   * @return uint64_t the number of active connections owned by the handler.
   */
  virtual uint64_t numConnections() PURE;

  /**
   * Adds listener to the handler.
   * @param config listener configuration options.
   */
  virtual void addListener(ListenerConfig& config) PURE;

  /**
   * Find a listener based on the provided listener address value.
   * @param address supplies the address value.
   * @return a pointer to the listener or nullptr if not found.
   * Ownership of the listener is NOT transferred
   */
  virtual Network::Listener* findListenerByAddress(const Network::Address::Instance& address) PURE;

  /**
   * Remove listeners using the listener tag as a key. All connections owned by the removed
   * listeners will be closed.
   * @param listener_tag supplies the tag passed to addListener().
   */
  virtual void removeListeners(uint64_t listener_tag) PURE;

  /**
   * Stop listeners using the listener tag as a key. This will not close any connections and is used
   * for draining.
   * @param listener_tag supplies the tag passed to addListener().
   */
  virtual void stopListeners(uint64_t listener_tag) PURE;

  /**
   * Stop all listeners. This will not close any connections and is used for draining.
   */
  virtual void stopListeners() PURE;
};

typedef std::unique_ptr<ConnectionHandler> ConnectionHandlerPtr;

} // namespace Network
} // namespace Envoy

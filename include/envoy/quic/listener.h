#pragma once

#include <memory>

#include "envoy/network/listen_socket.h"

namespace Envoy {
namespace Event {
class Dispatcher;
}

namespace Quic {

/**
 * Callbacks used by QuicListener instances to communicate with their execution
 * environment.
 */
class QuicListenerCallbacks {
public:
  virtual ~QuicListenerCallbacks() {}

  /**
   * @return the socket via which the listener can send and receive datagrams.
   */
  virtual Network::Socket& socket() PURE;

  /**
   * @return the dispatcher via which the listener can register for events.
   */
  virtual Event::Dispatcher& dispatcher() PURE;
};

/**
 * Abstract interface for QUIC listeners.
 */
// Ideally, this class would extend Network::Listener, or Network::Listener
// could even be used directly instead of it. However, as currently stands, this
// would introduce a circular dependency, since Network::ListenerConfig (which
// refers to QuicListenerFactory) and Network::Listener (to which
// QuicListenerFactory would refer) are defined in the same build target
// (//include/envoy/network:listener_interface).
class QuicListener {
public:
  virtual ~QuicListener() {}

  /**
   * Temporarily disable accepting new connections.
   */
  virtual void disable() PURE;

  /**
   * Enable accepting new connections.
   */
  virtual void enable() PURE;
};

typedef std::unique_ptr<QuicListener> QuicListenerPtr;

/**
 * Factory for creating QUIC listeners.
 */
class QuicListenerFactory {
public:
  virtual ~QuicListenerFactory() {}

  /**
   * Create a QUIC listener. The listener is responsible for registering for
   * socket events as needed; both socket and dispatcher are accessible via the
   * callbacks provided.
   * @return the newly created listener.
   */
  virtual QuicListenerPtr createQuicListener(QuicListenerCallbacks& callbacks) PURE;
};

typedef std::unique_ptr<QuicListenerFactory> QuicListenerFactoryPtr;

} // namespace Quic
} // namespace Envoy

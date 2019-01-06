#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"

namespace Envoy {
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
  virtual Network::ListenerPtr createQuicListener(QuicListenerCallbacks& callbacks) PURE;
};

typedef std::unique_ptr<QuicListenerFactory> QuicListenerFactoryPtr;

} // namespace Quic
} // namespace Envoy

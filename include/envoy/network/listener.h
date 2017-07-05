#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/network/connection.h"

namespace Envoy {
namespace Network {

/**
 * Listener configurations options.
 */
struct ListenerOptions {
  // Specifies if the listener should actually bind to the port. A listener that doesn't bind can
  // only receive connections redirected from other listeners that set use_origin_dst_ to true.
  bool bind_to_port_;
  // Whether to use the PROXY Protocol V1
  // (http://www.haproxy.org/download/1.5/doc/proxy-protocol.txt)
  bool use_proxy_proto_;
  // If a connection was redirected to this port using iptables, allow the listener to hand it off
  // to the listener associated to the original port.
  bool use_original_dst_;
  // Soft limit on size of the listener's new connection read and write buffers.
  uint32_t per_connection_buffer_limit_bytes_;

  /**
   * Factory for ListenerOptions with bind_to_port_ set.
   * @return ListenerOptions object initialized with bind_to_port_ set.
   */
  static ListenerOptions listenerOptionsWithBindToPort() {
    return {.bind_to_port_ = true,
            .use_proxy_proto_ = false,
            .use_original_dst_ = false,
            .per_connection_buffer_limit_bytes_ = 0};
  }
};

/**
 * Callbacks invoked by a listener.
 */
class ListenerCallbacks {
public:
  virtual ~ListenerCallbacks() {}

  /**
   * Called when a new connection is accepted.
   * @param new_connection supplies the new connection that is moved into the callee.
   */
  virtual void onNewConnection(ConnectionPtr&& new_connection) PURE;
};

/**
 * An abstract socket listener. Free the listener to stop listening on the socket.
 */
class Listener {
public:
  virtual ~Listener() {}
};

typedef std::unique_ptr<Listener> ListenerPtr;

/**
 * Thrown when there is a runtime error creating/binding a listener.
 */
class CreateListenerException : public EnvoyException {
public:
  CreateListenerException(const std::string& what) : EnvoyException(what) {}
};

} // namespace Network
} // namespace Envoy

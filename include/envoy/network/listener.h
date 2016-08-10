#pragma once

#include "envoy/common/exception.h"

namespace Network {

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
  virtual ~Listener(){};
};

typedef std::unique_ptr<Listener> ListenerPtr;

/**
 * Thrown when there is a runtime error creating/binding a listener.
 */
class CreateListenerException : public EnvoyException {
public:
  CreateListenerException(const std::string& what) : EnvoyException(what) {}
};

} // Network

#pragma once

#include "envoy/common/pure.h"

namespace Network {

/**
 * An abstract listen socket.
 */
class ListenSocket {
public:
  virtual ~ListenSocket() {}

  /**
   * @return a name used for stats.  For TCP listen sockets, this is the port.
   */
  virtual const std::string name() PURE;

  /**
   * @return fd the listen socket's file descriptor.
   */
  virtual int fd() PURE;

  /**
   * Close the underlying socket.
   */
  virtual void close() PURE;
};

typedef std::unique_ptr<ListenSocket> ListenSocketPtr;

} // Network

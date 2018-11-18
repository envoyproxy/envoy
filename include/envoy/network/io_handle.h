#pragma once

#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Network {

/**
 * IoHandle: an abstract interface for all I/O operations
 */
class IoHandle {
public:
  IoHandle() {}

  virtual ~IoHandle() {}

  // TODO(sbelair2) remove fd() method
  virtual int fd() const PURE;

  /**
   * @param the socket file descriptor to set in the handle. Assigns an fd from
   * an external socket operation such as from libevent or the dispatcher after construction
   *
   * TODO(sbelair2):  To be removed when the IoSocketHandle derivative is integrated
   * and the fd is fully abstracted from clients.
   */
  virtual void operator=(int fd) PURE;
};
typedef std::shared_ptr<IoHandle> IoHandlePtr;
typedef std::shared_ptr<IoHandle> IoHandleConstPtr;

} // namespace Network
} // namespace Envoy

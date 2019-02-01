#pragma once

#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Network {

/**
 * IoHandle: an abstract interface for all I/O operations
 */
template <class T> class IoHandle {
public:
  virtual ~IoHandle() {}

  /**
   * Return data associated with IoHandle.
   *
   * TODO(sbelair2) remove fd() method
   */
  virtual int fd() const PURE;

  /**
   * Clean up IoHandle resources
   */
  virtual T close() PURE;

  virtual bool isClosed() const PURE;
};

typedef std::unique_ptr<IoHandle<T>> IoHandlePtr;

} // namespace Network
} // namespace Envoy

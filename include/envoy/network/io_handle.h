#pragma once

#include <memory>

#include "envoy/api/io_error.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Network {

/**
 * IoHandle: an abstract interface for all I/O operations
 */
class IoHandle {
public:
  virtual ~IoHandle() {}

  /**
   * Return data associated with IoHandle.
   *
   * TODO(danzh) move it to IoSocketHandle after replacing the calls to it with
   * calls to IoHandle API's everywhere.
   */
  virtual int fd() const PURE;

  /**
   * Clean up IoHandle resources
   */
  virtual Api::IoCallUintResult close() PURE;

  /**
   * Return true if close() hasn't been called.
   */
  virtual bool isOpen() const PURE;
};

typedef std::unique_ptr<IoHandle> IoHandlePtr;

} // namespace Network
} // namespace Envoy

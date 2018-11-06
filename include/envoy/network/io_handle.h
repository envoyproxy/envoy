#pragma once

#include "envoy/common/pure.h"
#include <memory>

namespace Envoy {
namespace Network {

/**
 * IoHandle
 */
class IoHandle {
public:
  IoHandle(int fd=-1):fd_(fd) { }
  virtual ~IoHandle() { }

  int fd() const { return fd_; }

  // implicit cast operator
  operator int() const { return fd_; }

  // Assignment operators
  void operator = (int fd) { fd_ = fd; }
  void operator = (const IoHandle& ioHandle) { fd_ = ioHandle.fd(); }

  // Logical operators
  bool	operator == (int fd) const { return fd==fd_; }
  bool	operator != (int fd) const { return fd!=fd_; }

private:
  int fd_;
};
typedef std::unique_ptr<IoHandle> IoHandlePtr;


} // namespace Network
} // namespace Envoy

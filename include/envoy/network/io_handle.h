#pragma once

#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Network {

namespace Address {
class Instance;
}  // namespace Address

template <typename T> struct IoHandleCallResult {
  T rc_;
  int errno_;
};

typedef IoHandleCallResult<int> IoHandleCallIntResult;
typedef IoHandleCallResult<ssize_t> IoHandleCallSizeResult;

/**
 * IoHandle: an abstract interface for all I/O operations
 */
class IoHandle {
public:
  virtual ~IoHandle() {}

  /**
   * Return data associated with IoHandle.
   *
   * TODO(sbelair2) remove fd() method
   * We probably still need some method similar to this one for
   * evconnlistener_new(). Or We can move it to IoSocketHandle and down cast the
   * IoHandle to IoSocketHandle wherever needed.
   */
  virtual int fd() const PURE;

  /**
   * Clean up IoHandle resources
   */
  virtual IoHandleCallIntResult close() PURE;

  virtual bool isClosed() PURE;

  virtual IoHandleCallSizeResult readv(const iovec* iovec, int num_iovec) PURE;

  virtual IoHandleCallSizeResult writev(const iovec* iovec, int num_iovec) PURE;

  virtual IoHandleCallIntResult bind(const Network::Address::Instance& address) PURE;

  virtual IoHandleCallIntResult connect(const Network::Address::Instance& server_address) PURE;

  /**
   * Wrap setsockopt()
   */
  virtual IoHandleCallIntResult setIoHandleOption(int level, int optname, const void* optval,
                                                  socklen_t optlen) PURE;
  /**
   * Wrap getsockopt()
   */
  virtual IoHandleCallIntResult getIoHandleOption(int level, int optname, void* optval,
                                                  socklen_t* optlen) PURE;

  /**
   * Wrap getsockname()
   */
  virtual IoHandleCallIntResult getIoHandleName(const Network::Address::Instance& address) PURE;

  virtual IoHandleCallIntResult getPeerName(const Network::Address::Instance& address) PURE;

  /**
   * Wrap fcntl(fd_, F_SETFL...)
   */
  virtual IoHandleCallIntResult setIoHandleFlag(int flag) PURE;
  /**
   * Wrap fcntl(fd_, F_GETFL...)
   */
  virtual IoHandleCallIntResult getIoHandleFlag() PURE;

  virtual IoHandleCallIntResult listen(int backlog) PURE;

  /**
   * Wrap dup()
   */
  virtual std::unique_ptr<IoHandle> dup() PURE;

  virtual IoHandleCallIntResult shutdown(int how) PURE;
};

typedef std::unique_ptr<IoHandle> IoHandlePtr;

} // namespace Network
} // namespace Envoy

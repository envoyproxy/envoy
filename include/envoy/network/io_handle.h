#pragma once

#include <bitset>
#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {

namespace Buffer {
struct RawSlice;
}

namespace Network {

namespace Address {
class Instance;
} // namespace Address

class IoError {
public:
  enum IoErrorCode {
    IO_NO_ERROR = 0,
    IO_EAGAIN,
    IO_ENOTSUP,
    IO_EAFNOSUPPORT,
    IO_EINPROGRESS,
    IO_EPERM,
    IO_UNKNOWN_ERROR
  };
  virtual ~IoError() {}

  // Map platform specific error into IoErrorCode.
  virtual IoErrorCode getErrorCode() PURE;

  virtual std::string getErrorDetails() PURE;
};

/**
 * Basic type for return result which has a return code and error code defined
 * according to different implementation.
 */
template <typename T> struct IoHandleCallResult {
  virtual ~IoHandleCallResult() {}

  T rc_;
  std::unique_ptr<IoError> err_;
};

using IoHandleCallIntResult = IoHandleCallResult<int>;
using IoHandleCallSizeResult = IoHandleCallResult<ssize_t>;

/**
 * IoHandle: an abstract interface for all I/O operations
 */
class IoHandle {
public:
  enum ShutdownType { READ = 0, WRITE, BOTH };

  enum IoHandleFlag { NONBLOCK = 1, APPEND = 2 };

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

  virtual IoHandleCallSizeResult readv(Buffer::RawSlice* iovecs, int num_iovec) PURE;

  virtual IoHandleCallIntResult recvmmsg(struct mmsghdr* msgvec, unsigned int vlen, int flags,
                                         struct timespec* timeout) PURE;

  virtual IoHandleCallSizeResult writev(const Buffer::RawSlice* iovec, int num_iovec) PURE;

  virtual IoHandleCallIntResult sendmmsg(struct mmsghdr* msgvec, unsigned int vlen, int flags) PURE;

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
   * Wrap Address::addressFromFd()
   */
  virtual IoHandleCallIntResult getBindToAddress(Network::Address::Instance** address) PURE;

  /**
   * Wrap Address::peerAddressFromFd()
   */
  virtual IoHandleCallIntResult getPeerAddress(Network::Address::Instance** address) PURE;

  /**
   * Wrap fcntl(fd_, F_SETFL...)
   */
  virtual IoHandleCallIntResult setIoHandleFlag(std::bitset<2> flag) PURE;
  /**
   * Wrap fcntl(fd_, F_GETFL...)
   */
  virtual IoHandleCallResult<std::bitset<2>> getIoHandleFlags() PURE;

  virtual IoHandleCallIntResult listen(int backlog) PURE;

  /**
   * Wrap dup()
   */
  virtual std::unique_ptr<IoHandle> dup() PURE;

  virtual IoHandleCallIntResult shutdown(ShutdownType how) PURE;
};

typedef std::unique_ptr<IoHandle> IoHandlePtr;

} // namespace Network
} // namespace Envoy

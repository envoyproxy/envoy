#pragma once

#include <bitset>
#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {

namespace Buffer {
struct RawSlice;
} // namespace Buffer

namespace Network {

namespace Address {
class Instance;
} // namespace Address

class IoError {
public:
  enum class IoErrorCode {
    // Success.
    NoError = 0,
    // No data available right now, try again later.
    Again,
    // Not supported.
    NoSupport,
    // Address family not supported.
    AddressFamilyNoSupport,
    // During non-blocking connect, the connection cannot be completed immediately.
    InProgress,
    // Permission denied.
    Permission,
    // Other error codes cannot be mapped to any one above in getErrorCode().
    UnknownError
  };
  virtual ~IoError() {}

  // Map platform specific error into IoErrorCode.
  virtual IoErrorCode getErrorCode() const PURE;

  virtual std::string getErrorDetails() const PURE;
};

/**
 * Basic type for return result which has a return code and error code defined
 * according to different implementations.
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
  enum class ShutdownType { Read = 0, Write, Both };

  enum class IoHandleFlag { NonBlock = 1, Append = 2 };

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
   * @param flag each bit stands for a flag in enum IoHandleFlag. From low bit
   * to high bit:
   * 1st -- NonBlock
   * 2nd -- Append
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

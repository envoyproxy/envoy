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

class IoError;

// IoErrorCode::Again is used frequently. Define it to be a distinguishable address to avoid
// frequent memory allocation of IoError instance.
// If this is used, IoHandleCallResult has to be instantiated with a deleter that does not
// deallocate memory for this error.
#define ENVOY_ERROR_AGAIN reinterpret_cast<IoError*>(0x01)

class IoError {
public:
  enum class IoErrorCode {
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
  // Needed to hide errorCode() in case of ENVOY_ERROR_AGAIN.
  static IoErrorCode getErrorCode(const IoError& err) {
    if (&err == ENVOY_ERROR_AGAIN) {
      return IoErrorCode::Again;
    }
    return err.errorCode();
  }

  static std::string getErrorDetails(const IoError& err) {
    if (&err == ENVOY_ERROR_AGAIN) {
      return "Try again later";
    }
    return err.errorDetails();
  }

protected:
  // Map platform specific error into IoErrorCode.
  virtual IoErrorCode getErrorCode() const PURE;

  virtual std::string getErrorDetails() const PURE;
};

using IoErrorDeleterType = void (*)(IoError*);
using IoErrorPtr = std::unique_ptr<IoError, IoErrorDeleterType>;

/**
 * Basic type for return result which has a return code and error code defined
 * according to different implementations.
 * If the call succeeds, |err_| is nullptr and |rc_| is valid. Otherwise |err_|
 * can be passed into IoError::getErrorCode() to extract the error. In this
 * case, |rc_| is invalid.
 */
template <typename T> struct IoHandleCallResult {
  IoHandleCallResult(T rc, IoErrorPtr err) : rc_(rc), err_(std::move(err)) {}
  virtual ~IoHandleCallResult() {}

  T rc_;
  IoErrorPtr err_;
};

using IoHandleCallIntResult = IoHandleCallResult<int>;
using IoHandleCallSizeResult = IoHandleCallResult<ssize_t>;

/**
 * IoHandle: an abstract interface for all I/O operations
 */
class IoHandle {
public:
  enum class ShutdownType { Read = 0, Write, Both };

  enum class IoHandleFlag {
    // Each of these maps to a unique bit.
    NonBlock = 0b01,
    Append = 0b10
  };

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
   * @param flag each bit stands for a flag in enum IoHandleFlag.
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

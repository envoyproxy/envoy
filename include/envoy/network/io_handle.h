#pragma once

#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Network {

class IoError;

// IoErrorCode::Again is used frequently. Define it to be a distinguishable address to avoid
// frequent memory allocation of IoError instance.
// If this is used, IoHandleCallResult has to be instantiated with a deleter that does not
// deallocate memory for this error.
#define ENVOY_ERROR_AGAIN reinterpret_cast<IoError*>(0x01)

/**
 * Base class for any I/O error.
 */
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
  virtual IoErrorCode errorCode() const PURE;
  virtual std::string errorDetails() const PURE;
};

using IoErrorDeleterType = void (*)(IoError*);
using IoErrorPtr = std::unique_ptr<IoError, IoErrorDeleterType>;

/**
 * Basic type for return result which has a return code and error code defined
 * according to different implementations.
 */
template <typename T> struct IoHandleCallResult {
  IoHandleCallResult(T rc, IoErrorPtr err) : rc_(rc), err_(std::move(err)) {}

  IoHandleCallResult(IoHandleCallResult<T>&& result)
      : rc_(result.rc_), err_(std::move(result.err_)) {}

  virtual ~IoHandleCallResult() {}

  T rc_;
  IoErrorPtr err_;
};

using IoHandleCallUintResult = IoHandleCallResult<uint64_t>;

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
  virtual IoHandleCallUintResult close() PURE;

  /**
   * Return true if close() hasn't been called.
   */
  virtual bool isOpen() const PURE;
};

typedef std::unique_ptr<IoHandle> IoHandlePtr;

} // namespace Network
} // namespace Envoy

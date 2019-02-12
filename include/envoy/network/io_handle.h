#pragma once

#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Network {

/**
 * Base class for any I/O error.
 */
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
  IoHandleCallResult(T rc, std::unique_ptr<IoError> err) : rc_(rc), err_(std::move(err)) {}

  IoHandleCallResult(IoHandleCallResult<T>&& result)
      : rc_(result.rc_), err_(std::move(result.err_)) {}

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

  /**
   * Return true if close() hasn't been called.
   */
  virtual bool isOpen() const PURE;
};

typedef std::unique_ptr<IoHandle> IoHandlePtr;

} // namespace Network
} // namespace Envoy

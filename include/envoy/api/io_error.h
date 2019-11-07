#pragma once

#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Api {

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
    // Message too big to send.
    MessageTooBig,
    // Kernel interrupt.
    Interrupt,
    // Requested a nonexistent interface or a non-local source address.
    AddressNotAvailable,
    // Bad file descriptor.
    BadFd,
    // Other error codes cannot be mapped to any one above in getErrorCode().
    UnknownError
  };
  virtual ~IoError() = default;

  virtual IoErrorCode getErrorCode() const PURE;
  virtual std::string getErrorDetails() const PURE;
};

using IoErrorDeleterType = void (*)(IoError*);
using IoErrorPtr = std::unique_ptr<IoError, IoErrorDeleterType>;

/**
 * Basic type for return result which has a return code and error code defined
 * according to different implementations.
 * If the call succeeds, ok() should return true and |rc_| is valid. Otherwise |err_|
 * can be passed into IoError::getErrorCode() to extract the error. In this
 * case, |rc_| is invalid.
 */
template <typename ReturnValue> struct IoCallResult {
  IoCallResult(ReturnValue rc, IoErrorPtr err) : rc_(rc), err_(std::move(err)) {}

  IoCallResult(IoCallResult<ReturnValue>&& result) noexcept
      : rc_(result.rc_), err_(std::move(result.err_)) {}

  virtual ~IoCallResult() = default;

  IoCallResult& operator=(IoCallResult&& result) noexcept {
    rc_ = result.rc_;
    err_ = std::move(result.err_);
    return *this;
  }

  /**
   * @return true if the call succeeds.
   */
  bool ok() const { return err_ == nullptr; }

  // TODO(danzh): rename it to be more meaningful, i.e. return_value_.
  ReturnValue rc_;
  IoErrorPtr err_;
};

using IoCallBoolResult = IoCallResult<bool>;
using IoCallSizeResult = IoCallResult<ssize_t>;
using IoCallUint64Result = IoCallResult<uint64_t>;

inline Api::IoCallUint64Result ioCallUint64ResultNoError() {
  return IoCallUint64Result(0, IoErrorPtr(nullptr, [](IoError*) {}));
}

} // namespace Api
} // namespace Envoy

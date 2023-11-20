#pragma once

#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/common/pure.h"

namespace Envoy {
namespace Api {

class IoError;

using IoErrorDeleterType = void (*)(IoError*);
using IoErrorPtr = std::unique_ptr<IoError, IoErrorDeleterType>;

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
    // An existing connection was forcibly closed by the remote host.
    ConnectionReset,
    // Network is unreachable due to network settings.
    NetworkUnreachable,
    // Invalid arguments passed in.
    InvalidArgument,
    // Other error codes cannot be mapped to any one above in getErrorCode().
    UnknownError
  };
  virtual ~IoError() = default;

  virtual IoErrorCode getErrorCode() const PURE;
  virtual std::string getErrorDetails() const PURE;
  virtual int getSystemErrorCode() const PURE;

  // Wrap an IoError* in unique_ptr with custom deleter that doesn't delete.
  static IoErrorPtr reusedStatic(IoError* err) {
    return {err, [](IoError*) {}};
  }
  // Wrap an IoError* in unique_ptr with custom deleter.
  static IoErrorPtr wrap(IoError* err) {
    return {err, [](IoError* err) { delete err; }};
  }
  // Use this non-error for the success case.
  static IoErrorPtr none() {
    return {nullptr, [](IoError*) {}};
  }
};

/**
 * Basic type for return result which has a return code and error code defined
 * according to different implementations.
 * If the call succeeds, ok() should return true and |return_value_| is valid. Otherwise |err_|
 * can be passed into IoError::getErrorCode() to extract the error. In this
 * case, |return_value_| is invalid.
 */
template <typename ReturnValue> struct IoCallResult {
  IoCallResult(ReturnValue return_value, IoErrorPtr err)
      : return_value_(return_value), err_(std::move(err)) {}

  IoCallResult(IoCallResult<ReturnValue>&& result) noexcept
      : return_value_(std::move(result.return_value_)), err_(std::move(result.err_)) {}

  virtual ~IoCallResult() = default;

  IoCallResult& operator=(IoCallResult&& result) noexcept {
    return_value_ = result.return_value_;
    err_ = std::move(result.err_);
    return *this;
  }

  /**
   * @return true if the call succeeds.
   */
  bool ok() const { return err_ == nullptr; }

  /**
   * This return code is frequent enough that we have a separate function to check.
   * @return true if the system call failed because the socket would block.
   */
  bool wouldBlock() const { return !ok() && err_->getErrorCode() == IoError::IoErrorCode::Again; }

  ReturnValue return_value_;
  IoErrorPtr err_;
};

using IoCallBoolResult = IoCallResult<bool>;
using IoCallSizeResult = IoCallResult<ssize_t>;
using IoCallUint64Result = IoCallResult<uint64_t>;

inline Api::IoCallUint64Result ioCallUint64ResultNoError() {
  return {0, IoErrorPtr(nullptr, [](IoError*) {})};
}

} // namespace Api
} // namespace Envoy

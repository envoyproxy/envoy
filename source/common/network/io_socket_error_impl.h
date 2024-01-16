#pragma once

#include "envoy/api/io_error.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Network {

class IoSocketError : public Api::IoError {
public:
  static Api::IoErrorPtr create(int sys_errno);
  ~IoSocketError() override = default;

  Api::IoError::IoErrorCode getErrorCode() const override;
  std::string getErrorDetails() const override;
  int getSystemErrorCode() const override { return errno_; }

  // IoErrorCode::Again is used frequently. This custom error returns a
  // reusable singleton to avoid repeated allocation.
  static Api::IoErrorPtr getIoSocketEagainError();
  static Api::IoErrorPtr getIoSocketEbadfError();

  // This error is introduced when Envoy create socket for unsupported address. It is either a bug,
  // or this Envoy instance received config which is not yet supported. This should not be fatal
  // error.
  static Api::IoCallUint64Result ioResultSocketInvalidAddress();

private:
  explicit IoSocketError(int sys_errno)
      : errno_(sys_errno), error_code_(errorCodeFromErrno(errno_)) {
    ASSERT(error_code_ != IoErrorCode::Again,
           "Didn't use getIoSocketEagainError() to generate `Again`.");
  }
  explicit IoSocketError(int sys_errno, Api::IoError::IoErrorCode error_code)
      : errno_(sys_errno), error_code_(error_code) {}

  static Api::IoError::IoErrorCode errorCodeFromErrno(int sys_errno);

  static Api::IoErrorPtr getIoSocketInvalidAddressError();

  const int errno_;
  const Api::IoError::IoErrorCode error_code_;
};

} // namespace Network
} // namespace Envoy

#pragma once

#include "envoy/api/io_error.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Network {

class IoSocketError : public Api::IoError {
public:
  explicit IoSocketError(int sys_errno)
      : errno_(sys_errno), error_code_(errorCodeFromErrno(errno_)) {
    ASSERT(error_code_ != IoErrorCode::Again,
           "Didn't use getIoSocketEagainInstance() to generate `Again`.");
  }
  ~IoSocketError() override = default;

  Api::IoError::IoErrorCode getErrorCode() const override;
  std::string getErrorDetails() const override;
  int getSystemErrorCode() const override { return errno_; }

  // IoErrorCode::Again is used frequently. Define it to be a singleton to avoid frequent memory
  // allocation of such instance. If this is used, IoHandleCallResult has to be instantiated with
  // deleter deleteIoError() below to avoid deallocating memory for this error.
  static IoSocketError* getIoSocketEagainInstance();

  static IoSocketError* getIoSocketEbadfInstance();

  // This error is introduced when Envoy create socket for unsupported address. It is either a bug,
  // or this Envoy instance received config which is not yet supported. This should not be fatal
  // error.
  static Api::IoCallUint64Result ioResultSocketInvalidAddress();

  // Deallocate memory only if the error is not Again.
  static void deleteIoError(Api::IoError* err);

private:
  explicit IoSocketError(int sys_errno, Api::IoError::IoErrorCode error_code)
      : errno_(sys_errno), error_code_(error_code) {}

  static Api::IoError::IoErrorCode errorCodeFromErrno(int sys_errno);

  static IoSocketError* getIoSocketInvalidAddressInstance();

  const int errno_;
  const Api::IoError::IoErrorCode error_code_;
};

} // namespace Network
} // namespace Envoy

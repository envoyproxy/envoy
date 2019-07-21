#pragma once

#include "envoy/api/io_error.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Network {

class IoSocketError : public Api::IoError {
public:
  explicit IoSocketError(int sys_errno) : errno_(sys_errno) {}

  ~IoSocketError() override = default;

  Api::IoError::IoErrorCode getErrorCode() const override;
  std::string getErrorDetails() const override;

  // IoErrorCode::Again is used frequently. Define it to be a singleton to avoid frequent memory
  // allocation of such instance. If this is used, IoHandleCallResult has to be instantiated with
  // deleter deleteIoError() below to avoid deallocating memory for this error.
  static IoSocketError* getIoSocketEagainInstance();

  // Deallocate memory only if the error is not Again.
  static void deleteIoError(Api::IoError* err);

private:
  int errno_;
};

} // namespace Network
} // namespace Envoy

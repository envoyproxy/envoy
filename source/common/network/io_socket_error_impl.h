#pragma once

#include "envoy/api/io_error.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Network {

class IoSocketError : public Api::IoError {
public:
  explicit IoSocketError(int sys_errno) : errno_(sys_errno) {}

  ~IoSocketError() override {}

  Api::IoError::IoErrorCode getErrorCode() const override;
  std::string getErrorDetails() const override;

private:
  int errno_;
};

// IoErrorCode::Again is used frequently. Define it to be a singleton to avoid frequent memory
// allocation of such instance. If this is used, IoHandleCallResult has to be instantiated with
// deleter deleteIoError() below to avoid deallocating memory for this error.
inline IoSocketEagain* getIoSocketEagainInstance() {
  static auto* instance = new IoSocketError(EAGAIN);
  return instance;
}

// Deallocate memory only if the error is not Again.
inline void deleteIoError(Api::IoError* err) {
  ASSERT(err != nullptr);
  if (err->getErrorCode() != Api::IoError::IoErrorCode::Again) {
    delete err;
  }
}

} // namespace Network
} // namespace Envoy

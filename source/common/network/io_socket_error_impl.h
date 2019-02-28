#pragma once

#include "common/common/assert.h"
#include "envoy/network/io_handle.h"

namespace Envoy {
namespace Network {

class IoSocketError : public IoError {
public:
  explicit IoSocketError(int sys_errno) : errno_(sys_errno) {}

  ~IoSocketError() override {}

  IoError::IoErrorCode getErrorCode() const override;
  std::string getErrorDetails() const override;

private:
  int errno_;
};

// IoErrorCode::Again is used frequently. Define it to be a singleton to avoid frequent memory
// allocation of such instance. If this is used, IoHandleCallResult has to be instantiated with
// deleter deleteIoError() below to avoid deallocating memory for this error.
class IoSocketEagain : public IoSocketError {
public:
 IoSocketEagain() : IoSocketError(EAGAIN) {};
};

inline IoSocketEagain* getIoSocketEagainInstance() {
  static auto* kInstance = new IoSocketEagain();
  return kInstance;
}

// Deallocate memory only if the error is not IoSocketErrorAgain.
inline void deleteIoError(IoError* err) {
  ASSERT(err != nullptr);
  if (err != getIoSocketEagainInstance()) {
    delete err;
  }
}

} // namespace Network
} // namespace Envoy

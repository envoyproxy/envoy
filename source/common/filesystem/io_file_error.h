#pragma once

#include <string>

#include "envoy/api/io_error.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Filesystem {

class IoFileError : public Api::IoError {
public:
  explicit IoFileError(int sys_errno) : errno_(sys_errno) {}

  ~IoFileError() override {}

private:
  IoErrorCode errorCode() const override;

  std::string errorDetails() const override;

  int errno_;
};

using IoFileErrorPtr = std::unique_ptr<IoFileError, Api::IoErrorDeleterType>;

template <typename T> Api::IoCallResult<T> resultFailure(T result, int sys_errno) {
  return {result, IoFileErrorPtr(new IoFileError(sys_errno), [](Api::IoError* err) {
            ASSERT(err != nullptr);
            delete err;
          })};
}

template <typename T> Api::IoCallResult<T> resultSuccess(T result) {
  return {result, IoFileErrorPtr(nullptr, [](Api::IoError*) { NOT_REACHED_GCOVR_EXCL_LINE; })};
}

} // namespace Filesystem
} // namespace Envoy
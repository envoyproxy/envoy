#pragma once

#include <string>

#include "envoy/filesystem/filesystem.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Filesystem {

class IoFileError : public Api::IoError {
public:
  explicit IoFileError(int sys_errno) : errno_(sys_errno) {}

  ~IoFileError() override = default;

  Api::IoError::IoErrorCode getErrorCode() const override;
  std::string getErrorDetails() const override;

private:
  const int errno_;
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

class FileSharedImpl : public File {
public:
  FileSharedImpl(const FilePathAndType& filepath_and_type)
      : fd_(INVALID_HANDLE), filepath_and_type_(filepath_and_type) {}

  ~FileSharedImpl() override = default;

  bool isOpen() const override;
  std::string path() const override;
  DestinationType destinationType() const override;

protected:
  filesystem_os_id_t fd_;
  const FilePathAndType filepath_and_type_;
};

} // namespace Filesystem
} // namespace Envoy

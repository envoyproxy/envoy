#pragma once

#include <string>

#include "envoy/filesystem/filesystem.h"

#include "common/common/assert.h"

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
  FileSharedImpl(std::string path) : fd_(-1), path_(std::move(path)) {}

  ~FileSharedImpl() override = default;

  // Filesystem::File
  Api::IoCallBoolResult open(FlagSet flag) override;
  Api::IoCallSizeResult write(absl::string_view buffer) override;
  Api::IoCallBoolResult close() override;
  bool isOpen() const override;
  std::string path() const override;

protected:
  virtual void openFile(FlagSet in) PURE;
  virtual ssize_t writeFile(absl::string_view buffer) PURE;
  virtual bool closeFile() PURE;

  int fd_;
  const std::string path_;
};

} // namespace Filesystem
} // namespace Envoy

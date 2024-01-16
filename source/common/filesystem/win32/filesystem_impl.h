#pragma once

#include <cstdint>
#include <string>

#include "source/common/filesystem/file_shared_impl.h"

namespace Envoy {
namespace Filesystem {

class FileImplWin32 : public FileSharedImpl {
public:
  FileImplWin32(const FilePathAndType& file_info) : FileSharedImpl(file_info) {}
  ~FileImplWin32();

protected:
  Api::IoCallBoolResult open(FlagSet flag) override;
  Api::IoCallSizeResult write(absl::string_view buffer) override;
  Api::IoCallBoolResult close() override;
  Api::IoCallSizeResult pread(void* buf, uint64_t count, uint64_t offset) override;
  Api::IoCallSizeResult pwrite(const void* buf, uint64_t count, uint64_t offset) override;
  Api::IoCallResult<FileInfo> info() override;

  struct FlagsAndMode {
    DWORD access_ = 0;
    DWORD creation_ = 0;
  };

  FlagsAndMode translateFlag(FlagSet in);

private:
  friend class FileSystemImplTest;
};

class TmpFileImplWin32 : public FileImplWin32 {
public:
  TmpFileImplWin32(const FilePathAndType& file_info) : FileImplWin32(file_info) {}
  Api::IoCallBoolResult open(FlagSet flag) override;

private:
  std::string tmp_file_path_;
};

template <DWORD std_handle_> struct StdStreamFileImplWin32 : public FileImplWin32 {
  static_assert(std_handle_ == STD_OUTPUT_HANDLE || std_handle_ == STD_ERROR_HANDLE);
  StdStreamFileImplWin32()
      : FileImplWin32(FilePathAndType{destinationTypeFromStdHandle(), filenameFromStdHandle()}) {}
  ~StdStreamFileImplWin32() { fd_ = INVALID_HANDLE; }

  Api::IoCallBoolResult open(FlagSet) {
    fd_ = GetStdHandle(std_handle_);
    if (fd_ == NULL) {
      // If an application does not have associated standard handles,
      // such as a service running on an interactive desktop
      // and has not redirected them, the return value is NULL.
      return resultFailure(false, ERROR_INVALID_HANDLE);
    }
    if (fd_ == INVALID_HANDLE) {
      return resultFailure(false, ::GetLastError());
    }
    return resultSuccess(true);
  }

  Api::IoCallBoolResult close() {
    // If we are writing to the standard output of the process we are
    // not the owners of the handle, we are just using it.
    fd_ = INVALID_HANDLE;
    return resultSuccess(true);
  }

  static constexpr absl::string_view filenameFromStdHandle() {
    if constexpr (std_handle_ == STD_OUTPUT_HANDLE) {
      return "/dev/stdout";
    } else {
      return "/dev/stderr";
    }
  }

  static constexpr DestinationType destinationTypeFromStdHandle() {
    if constexpr (std_handle_ == STD_OUTPUT_HANDLE) {
      return DestinationType::Stdout;
    } else {
      return DestinationType::Stderr;
    }
  }
};

class InstanceImplWin32 : public Instance {
public:
  // Filesystem::Instance
  FilePtr createFile(const FilePathAndType& file_info) override;
  bool fileExists(const std::string& path) override;
  bool directoryExists(const std::string& path) override;
  ssize_t fileSize(const std::string& path) override;
  absl::StatusOr<std::string> fileReadToEnd(const std::string& path) override;
  absl::StatusOr<PathSplitResult> splitPathFromFilename(absl::string_view path) override;
  bool illegalPath(const std::string& path) override;
  Api::IoCallResult<FileInfo> stat(absl::string_view path) override;
  Api::IoCallBoolResult createPath(absl::string_view path) override;
};

using FileImpl = FileImplWin32;
using InstanceImpl = InstanceImplWin32;

} // namespace Filesystem
} // namespace Envoy

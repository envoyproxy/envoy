#pragma once

#include <cstdint>
#include <string>

#include "envoy/api/os_sys_calls.h"

#include "source/common/filesystem/file_shared_impl.h"

namespace Envoy {
namespace Filesystem {

class FileImplPosix : public FileSharedImpl {
public:
  FileImplPosix(const FilePathAndType& file_info) : FileSharedImpl(file_info) {}
  ~FileImplPosix() override;

protected:
  struct FlagsAndMode {
    int flags_ = 0;
    mode_t mode_ = 0;
  };

  Api::IoCallBoolResult open(FlagSet flag) override;
  Api::IoCallSizeResult write(absl::string_view buffer) override;
  Api::IoCallBoolResult close() override;
  Api::IoCallSizeResult pread(void* buf, uint64_t count, uint64_t offset) override;
  Api::IoCallSizeResult pwrite(const void* buf, uint64_t count, uint64_t offset) override;
  Api::IoCallResult<FileInfo> info() override;
  FlagsAndMode translateFlag(FlagSet in);

private:
  friend class FileSystemImplTest;
};

class TmpFileImplPosix : public FileImplPosix {
public:
  TmpFileImplPosix(const FilePathAndType& file_info) : FileImplPosix(file_info) {}
  Api::IoCallBoolResult open(FlagSet flag) override;
  Api::IoCallBoolResult close() override;
  ~TmpFileImplPosix() override;

private:
  // Ideally opening a tmp file opens it with no name; this is a fallback option for
  // when the OS or filesystem doesn't support that. with_unlink is a test-only
  // parameter to allow us to also test-cover the path where unlinking an open file
  // doesn't work.
  Api::IoCallBoolResult openNamedTmpFile(FlagsAndMode flags_and_mode, bool with_unlink = true);
  // This is only set in the case where the file system does not support nameless tmp files.
  std::string tmp_file_path_;
  friend class FileSystemImplTest;
};

class InstanceImplPosix : public Instance {
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

private:
  Api::SysCallStringResult canonicalPath(const std::string& path);
  friend class FileSystemImplTest;
};

using FileImpl = FileImplPosix;
using InstanceImpl = InstanceImplPosix;
} // namespace Filesystem
} // namespace Envoy

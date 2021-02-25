#pragma once

#include <cstdint>
#include <string>

#include "envoy/api/os_sys_calls.h"

#include "common/filesystem/file_shared_impl.h"

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

private:
  FlagsAndMode translateFlag(FlagSet in);
  friend class FileSystemImplTest;
};

struct ConsoleFileImplPosix : public FileImplPosix {
  ConsoleFileImplPosix()
      : FileImplPosix(FilePathAndType{DestinationType::Console, "/dev/console"}) {}
};

struct StdOutFileImplPosix : public FileImplPosix {
  StdOutFileImplPosix() : FileImplPosix(FilePathAndType{DestinationType::Stdout, "/dev/stdout"}) {}
};

struct StdErrFileImplPosix : public FileImplPosix {
  StdErrFileImplPosix() : FileImplPosix(FilePathAndType{DestinationType::Stderr, "/dev/stderr"}) {}
};

class InstanceImplPosix : public Instance {
public:
  // Filesystem::Instance
  FilePtr createFile(const FilePathAndType& file_info) override;
  FilePtr createFile(const std::string& path) override;
  bool fileExists(const std::string& path) override;
  bool directoryExists(const std::string& path) override;
  ssize_t fileSize(const std::string& path) override;
  std::string fileReadToEnd(const std::string& path) override;
  PathSplitResult splitPathFromFilename(absl::string_view path) override;
  bool illegalPath(const std::string& path) override;

private:
  Api::SysCallStringResult canonicalPath(const std::string& path);
  friend class FileSystemImplTest;
};

} // namespace Filesystem
} // namespace Envoy

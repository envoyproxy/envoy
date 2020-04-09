#pragma once

#include <cstdint>
#include <string>

#include "envoy/api/os_sys_calls.h"

#include "common/filesystem/file_shared_impl.h"

namespace Envoy {
namespace Filesystem {

class FileImplPosix : public FileSharedImpl {
public:
  FileImplPosix(const std::string& path) : FileSharedImpl(path) {}
  ~FileImplPosix() override;

protected:
  struct FlagsAndMode {
    int flags_ = 0;
    mode_t mode_ = 0;
  };

  // Filesystem::FileSharedImpl
  FlagsAndMode translateFlag(FlagSet in);
  void openFile(FlagSet flags) override;
  ssize_t writeFile(absl::string_view buffer) override;
  bool closeFile() override;

private:
  friend class FileSystemImplTest;
};

class InstanceImplPosix : public Instance {
public:
  // Filesystem::Instance
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

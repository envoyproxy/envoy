#pragma once

#include <cstdint>
#include <string>

#include "common/filesystem/file_shared_impl.h"

namespace Envoy {
namespace Filesystem {

class FileImplWin32 : public FileSharedImpl {
public:
  FileImplWin32(const std::string& path) : FileSharedImpl(path) {}
  ~FileImplWin32();

protected:
  Api::IoCallBoolResult open(FlagSet flag) override;
  Api::IoCallSizeResult write(absl::string_view buffer) override;
  Api::IoCallBoolResult close() override;

private:
  struct FlagsAndMode {
    DWORD access_ = 0;
    DWORD creation_ = 0;
  };

  FlagsAndMode translateFlag(FlagSet in);
  friend class FileSystemImplTest;
  bool is_std_handle_{false};
};

class InstanceImplWin32 : public Instance {
public:
  // Filesystem::Instance
  FilePtr createFile(const std::string& path) override;
  bool fileExists(const std::string& path) override;
  bool directoryExists(const std::string& path) override;
  ssize_t fileSize(const std::string& path) override;
  std::string fileReadToEnd(const std::string& path) override;
  PathSplitResult splitPathFromFilename(absl::string_view path) override;
  bool illegalPath(const std::string& path) override;
};

} // namespace Filesystem
} // namespace Envoy

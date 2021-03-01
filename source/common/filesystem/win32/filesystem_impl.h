#pragma once

#include <cstdint>
#include <string>

#include "common/filesystem/file_shared_impl.h"

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
  struct FlagsAndMode {
    DWORD access_ = 0;
    DWORD creation_ = 0;
  };

  FlagsAndMode translateFlag(FlagSet in);

private:
  friend class FileSystemImplTest;
};

struct ConsoleFileImplWin32 : public FileImplWin32 {
  ConsoleFileImplWin32() : FileImplWin32(FilePathAndType{DestinationType::Console, "CONOUT$"}) {}

protected:
  Api::IoCallBoolResult open(FlagSet flag) override;
};

struct StdOutFileImplWin32 : public FileImplWin32 {
  StdOutFileImplWin32() : FileImplWin32(FilePathAndType{DestinationType::Stdout, "/dev/stdout"}) {}
  ~StdOutFileImplWin32() { fd_ = INVALID_HANDLE; }

protected:
  Api::IoCallBoolResult open(FlagSet) override;
  Api::IoCallBoolResult close() override;

private:
  static constexpr DWORD std_handle_ = STD_OUTPUT_HANDLE;
};

struct StdErrFileImplWin32 : public FileImplWin32 {
  StdErrFileImplWin32() : FileImplWin32(FilePathAndType{DestinationType::Stderr, "/dev/stderr"}) {}
  ~StdErrFileImplWin32() { fd_ = INVALID_HANDLE; }

protected:
  Api::IoCallBoolResult open(FlagSet) override;
  Api::IoCallBoolResult close() override;

private:
  static constexpr DWORD std_handle_ = STD_ERROR_HANDLE;
};

class InstanceImplWin32 : public Instance {
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
};

} // namespace Filesystem
} // namespace Envoy

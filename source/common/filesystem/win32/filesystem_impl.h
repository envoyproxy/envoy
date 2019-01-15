#pragma once

#include "envoy/filesystem/filesystem.h"

namespace Envoy {
namespace Filesystem {

class InstanceImplWin32 : public Instance {
public:
  // Filesystem::Instance
  bool fileExists(const std::string& path) override;
  bool directoryExists(const std::string& path) override;
  ssize_t fileSize(const std::string& path) override;
  std::string fileReadToEnd(const std::string& path) override;
  bool illegalPath(const std::string& path) override;
  FilePtr createFile(const std::string& path) override;
};

class FileImplWin32 : public File {
public:
  FileImplWin32(const std::string& path);
  ~FileImplWin32();

  // Filesystem::File
  void open() override;
  Api::SysCallSizeResult write(const void* buffer, size_t len) override;
  void close() override;
  bool isOpen() override;

private:
  int fd_;
  const std::string path_;
};

} // namespace Filesystem
} // namespace Envoy

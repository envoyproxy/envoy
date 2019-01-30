#pragma once

#include <cstdint>
#include <string>

#include "envoy/filesystem/filesystem.h"

namespace Envoy {
namespace Filesystem {

class RawFileImpl : public RawFile {
public:
  RawFileImpl(const std::string& path);
  ~RawFileImpl();

  // Filesystem::RawFile
  Api::SysCallBoolResult open() override;
  Api::SysCallSizeResult write(absl::string_view buffer) override;
  Api::SysCallBoolResult close() override;
  bool isOpen() override;
  std::string path() override;

private:
  int fd_;
  const std::string path_;
  friend class RawInstanceImplTest;
};

class RawInstanceImpl : public RawInstance {
public:
  // Filesystem::RawInstance
  RawFilePtr createRawFile(const std::string& path) override;
  bool fileExists(const std::string& path) override;
  bool directoryExists(const std::string& path) override;
  ssize_t fileSize(const std::string& path) override;
  std::string fileReadToEnd(const std::string& path) override;
  Api::SysCallStringResult canonicalPath(const std::string& path) override;
  bool illegalPath(const std::string& path) override;
};

} // namespace Filesystem
} // namespace Envoy

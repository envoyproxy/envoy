#pragma once

#include "envoy/filesystem/filesystem.h"
#include "envoy/server/options.h"

namespace Envoy {
namespace Filesystem {

class WrapInstance : public Instance {
public:
  explicit WrapInstance(Filesystem::Instance& impl);

  // Filesystem::Instance impl
  Filesystem::FilePtr createFile(const std::string& path) override;
  bool fileExists(const std::string& path) override;
  bool directoryExists(const std::string& path) override;
  ssize_t fileSize(const std::string& path) override;
  std::string fileReadToEnd(const std::string& path) override;
  Filesystem::PathSplitResult splitPathFromFilename(absl::string_view path) override;
  bool illegalPath(const std::string& path) override;

private:
  Instance& impl_;
};

class NullInstance : public Instance {
public:
  explicit NullInstance(Instance& impl);

  // Filesystem::Instance impl
  FilePtr createFile(const std::string& path) override;
  bool fileExists(const std::string& path) override;
  bool directoryExists(const std::string& path) override;
  ssize_t fileSize(const std::string& path) override;
  std::string fileReadToEnd(const std::string& path) override;
  Filesystem::PathSplitResult splitPathFromFilename(absl::string_view path) override;
  bool illegalPath(const std::string& path) override;

private:
  std::string makeErrorMessage(const absl::string_view path) const;
  Instance& impl_;
};

class RestrictedInstance : public Instance {
public:
  RestrictedInstance(absl::string_view allowed_prefix, Instance& impl);

  // Filesystem::Instance impl
  Filesystem::FilePtr createFile(const std::string& path) override;
  bool fileExists(const std::string& path) override;
  bool directoryExists(const std::string& path) override;
  ssize_t fileSize(const std::string& path) override;
  std::string fileReadToEnd(const std::string& path) override;
  Filesystem::PathSplitResult splitPathFromFilename(absl::string_view path) override;
  bool illegalPath(const std::string& path) override;

private:
  std::string makeErrorMessage(const absl::string_view path) const;
  const std::string prefix_;
  Instance& impl_;
};

InstancePtr makeValidationFilesystem(const Server::Options& options, Instance& real_file_system);

} // namespace Filesystem
} // namespace Envoy
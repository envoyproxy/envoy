#pragma once

#include "envoy/filesystem/filesystem.h"
#include "envoy/server/options.h"

namespace Envoy {
namespace Filesystem {

/**
 * Validation-only filesystem wrapper that does not allow access to the real file system. Any
 * operations attempted on a NullInstance that would access the on-disk file system will return an
 * error.
 */
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

/**
 * Validation-only filesystem wrapper that allows access to paths on the real file system that
 * begin with the specified prefix. No attempts are made to canonicalize paths, dereference
 * symlinks, or check individual path segments; the only accessible files are those whose paths have
 * the specified prefix. All other operations attempted that would access the on-disk file system
 * will result in an error.
 */
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

/**
 * Create a file system instance backed by the provided real file system with an appropriate level
 * of access determined by the provided options. If no access restrictions need to be imposed,
 * nullptr is returned.
 */
InstancePtr makeValidationFilesystem(const Server::Options& options, Instance& real_file_system);

} // namespace Filesystem
} // namespace Envoy

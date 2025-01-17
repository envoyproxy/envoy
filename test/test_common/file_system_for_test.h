#pragma once

#include "envoy/filesystem/filesystem.h"

#include "source/common/filesystem/file_shared_impl.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {

namespace Filesystem {

struct MemFileInfo;

class MemfileInstanceImpl : public Instance {
public:
  MemfileInstanceImpl();

  FilePtr createFile(const FilePathAndType& file_info) override;

  bool fileExists(const std::string& path) override {
    absl::MutexLock m(&lock_);
    auto it = files_.find(path);
    return (it != files_.end() || file_system_->fileExists(path));
  }

  bool directoryExists(const std::string& path) override {
    return file_system_->directoryExists(path);
  }

  ssize_t fileSize(const std::string& path) override;

  absl::StatusOr<std::string> fileReadToEnd(const std::string& path) override;

  absl::StatusOr<PathSplitResult> splitPathFromFilename(absl::string_view path) override {
    return file_system_->splitPathFromFilename(path);
  }

  bool illegalPath(const std::string& path) override { return file_system_->illegalPath(path); }

  void renameFile(const std::string& old_name, const std::string& new_name);

  Api::IoCallResult<FileInfo> stat(absl::string_view path) override;

  Api::IoCallBoolResult createPath(absl::string_view path) override;

private:
  friend class ScopedUseMemfiles;

  void setUseMemfiles(bool value) {
    absl::MutexLock m(&lock_);
    use_memfiles_ = value;
  }

  bool useMemfiles() {
    absl::MutexLock m(&lock_);
    return use_memfiles_;
  }

  std::unique_ptr<Instance> file_system_;
  absl::Mutex lock_;
  bool use_memfiles_ ABSL_GUARDED_BY(lock_){false};
  absl::flat_hash_map<std::string, std::shared_ptr<MemFileInfo>> files_ ABSL_GUARDED_BY(lock_);
};

MemfileInstanceImpl& fileSystemForTest();

class ScopedUseMemfiles {
public:
  explicit ScopedUseMemfiles(bool use)
      : prior_use_memfiles_(Filesystem::fileSystemForTest().useMemfiles()) {
    Filesystem::fileSystemForTest().setUseMemfiles(use);
  }
  ~ScopedUseMemfiles() { Filesystem::fileSystemForTest().setUseMemfiles(prior_use_memfiles_); }

private:
  const bool prior_use_memfiles_;
};

} // namespace Filesystem

} // namespace Envoy

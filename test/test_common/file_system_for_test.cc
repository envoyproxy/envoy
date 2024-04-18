#include "test/test_common/file_system_for_test.h"

#include "source/common/filesystem/filesystem_impl.h"

#include "test/test_common/test_time.h"

namespace Envoy {

namespace Filesystem {

struct MemFileInfo {
  MemFileInfo(SystemTime timestamp)
      : create_time_(timestamp), access_time_(timestamp), modify_time_(timestamp) {}
  absl::Mutex lock_;
  std::string data_ ABSL_GUARDED_BY(lock_);
  SystemTime create_time_;
  SystemTime access_time_;
  SystemTime modify_time_;
  FileInfo toFileInfo(absl::string_view path) {
    absl::MutexLock lock(&lock_);
    return {
        std::string{fileSystemForTest().splitPathFromFilename(path).value().file_},
        data_.length(),
        FileType::Regular,
        create_time_,
        access_time_,
        modify_time_,
    };
  }
};

class MemfileImpl : public FileSharedImpl {
public:
  MemfileImpl(const FilePathAndType& file_info, std::shared_ptr<MemFileInfo> info)
      : FileSharedImpl(file_info), info_(std::move(info)) {}

  bool isOpen() const override { return open_; }

protected:
  Api::IoCallBoolResult open(FlagSet flag) override {
    ASSERT(!isOpen());
    flags_ = flag;
    open_ = true;
    if (flags_.test(File::Operation::Write) && !flags_.test(File::Operation::Append) &&
        !flags_.test(File::Operation::KeepExistingData)) {
      absl::MutexLock l(&info_->lock_);
      info_->data_.clear();
    }
    return resultSuccess(true);
  }

  Api::IoCallSizeResult write(absl::string_view buffer) override {
    absl::MutexLock l(&info_->lock_);
    info_->data_.append(std::string(buffer));
    const ssize_t size = info_->data_.size();
    return resultSuccess(size);
  }

  Api::IoCallBoolResult close() override {
    ASSERT(isOpen());
    open_ = false;
    return resultSuccess(true);
  }

  Api::IoCallSizeResult pread(void* buf, uint64_t count, uint64_t offset) override;
  Api::IoCallSizeResult pwrite(const void* buf, uint64_t count, uint64_t offset) override;
  Api::IoCallResult<FileInfo> info() override;

private:
  FlagSet flags_;
  std::shared_ptr<MemFileInfo> info_;
  bool open_{false};
};

Api::IoCallSizeResult MemfileImpl::pread(void* buf, uint64_t count, uint64_t offset) {
  absl::MutexLock l(&info_->lock_);
  if (!flags_.test(File::Operation::Read)) {
    return resultFailure<ssize_t>(-1, EBADF);
  }
  if (offset >= info_->data_.size()) {
    count = 0;
    offset = 0;
  } else {
    count = std::max(count, info_->data_.size() - offset);
  }
  memcpy(buf, &info_->data_[offset], count);
  return resultSuccess<ssize_t>(count);
}

Api::IoCallSizeResult MemfileImpl::pwrite(const void* buf, uint64_t count, uint64_t offset) {
  absl::MutexLock l(&info_->lock_);
  if (!flags_.test(File::Operation::Write)) {
    return resultFailure<ssize_t>(-1, EBADF);
  }
  // When writing to an offset beyond end of file, file gets padded to there with zeroes.
  if (offset > info_->data_.size()) {
    std::string pad;
    pad.resize(offset - info_->data_.size());
    info_->data_ += pad;
  }
  std::string before = info_->data_.substr(0, offset);
  std::string after = (offset + count > info_->data_.size())
                          ? ""
                          : info_->data_.substr(info_->data_.size() - offset - count);
  info_->data_ =
      before + std::string{static_cast<const char*>(buf), static_cast<size_t>(count)} + after;
  return resultSuccess<ssize_t>(count);
}

Api::IoCallResult<FileInfo> MemfileImpl::info() { return resultSuccess(info_->toFileInfo(path())); }

Api::IoCallResult<FileInfo> MemfileInstanceImpl::stat(absl::string_view path) {
  {
    absl::MutexLock m(&lock_);
    auto it = files_.find(path);
    if (it != files_.end()) {
      ASSERT(use_memfiles_);
      return resultSuccess(it->second->toFileInfo(path));
    }
  }
  return file_system_->stat(path);
}

Api::IoCallBoolResult MemfileInstanceImpl::createPath(absl::string_view) {
  // Creating an imaginary path is always successful.
  return resultSuccess(true);
}

MemfileInstanceImpl::MemfileInstanceImpl() : file_system_{new InstanceImpl()} {}

MemfileInstanceImpl& fileSystemForTest() {
  static MemfileInstanceImpl* file_system = new MemfileInstanceImpl();
  return *file_system;
}

FilePtr MemfileInstanceImpl::createFile(const FilePathAndType& file_info) {
  const std::string& path = file_info.path_;
  absl::MutexLock m(&lock_);
  if (!use_memfiles_) {
    return file_system_->createFile(file_info);
  }
  Event::GlobalTimeSystem time_source;
  if (file_info.file_type_ == DestinationType::TmpFile) {
    // tmp files ideally should have no filename, so we create an info
    // without adding it to files_.
    return std::make_unique<MemfileImpl>(file_info,
                                         std::make_shared<MemFileInfo>(time_source.systemTime()));
  }
  if (file_system_->fileExists(path)) {
    return file_system_->createFile(file_info);
  }
  std::shared_ptr<MemFileInfo>& info = files_[path];
  if (info == nullptr) {
    info = std::make_shared<MemFileInfo>(time_source.systemTime());
  }
  return std::make_unique<MemfileImpl>(file_info, info);
}

ssize_t MemfileInstanceImpl::fileSize(const std::string& path) {
  {
    absl::MutexLock m(&lock_);
    auto it = files_.find(path);
    if (it != files_.end()) {
      ASSERT(use_memfiles_);
      absl::MutexLock n(&it->second->lock_);
      return it->second->data_.size();
    }
  }
  return file_system_->fileSize(path);
}

absl::StatusOr<std::string> MemfileInstanceImpl::fileReadToEnd(const std::string& path) {
  {
    absl::MutexLock m(&lock_);
    auto it = files_.find(path);
    if (it != files_.end()) {
      absl::MutexLock n(&it->second->lock_);
      ASSERT(use_memfiles_);
      return it->second->data_;
    }
  }
  return file_system_->fileReadToEnd(path);
}

void MemfileInstanceImpl::renameFile(const std::string& old_name, const std::string& new_name) {
  {
    absl::MutexLock m(&lock_);
    // It's easy enough to change the key to the hash set, but most instances of
    // renameFile are to trigger file watches in core code, and those are not
    // mem-file-aware.
    RELEASE_ASSERT(!use_memfiles_,
                   "moving files not supported with memfile. Please call setUseMemfiles(false)");
  }
#ifdef WIN32
  // use MoveFileEx, since ::rename will not overwrite an existing file. See
  // https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/rename-wrename?view=vs-2017
  // Note MoveFileEx cannot overwrite a directory as documented, nor a symlink, apparently.
  const BOOL rc = ::MoveFileEx(old_name.c_str(), new_name.c_str(), MOVEFILE_REPLACE_EXISTING);
  RELEASE_ASSERT(rc != 0, fmt::format("failed to rename file from  {} to {} with error {}",
                                      old_name, new_name, ::GetLastError()));
#else
  const int rc = ::rename(old_name.c_str(), new_name.c_str());
  RELEASE_ASSERT(rc == 0, "failed to rename file");
#endif
}

} // namespace Filesystem

} // namespace Envoy

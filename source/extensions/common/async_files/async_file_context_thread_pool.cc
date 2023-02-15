#include "source/extensions/common/async_files/async_file_context_thread_pool.h"

#include <fcntl.h>

#include <memory>
#include <string>
#include <utility>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_context_base.h"
#include "source/extensions/common/async_files/async_file_manager_thread_pool.h"
#include "source/extensions/common/async_files/status_after_file_error.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

namespace {

template <typename T> class AsyncFileActionThreadPool : public AsyncFileActionWithResult<T> {
public:
  explicit AsyncFileActionThreadPool(AsyncFileHandle handle, std::function<void(T)> on_complete)
      : AsyncFileActionWithResult<T>(on_complete), handle_(std::move(handle)) {}

protected:
  int& fileDescriptor() { return context()->fileDescriptor(); }
  AsyncFileContextThreadPool* context() const {
    return static_cast<AsyncFileContextThreadPool*>(handle_.get());
  }

  Api::OsSysCalls& posix() const {
    return static_cast<AsyncFileManagerThreadPool&>(context()->manager()).posix();
  }

  AsyncFileHandle handle_;
};

class ActionStat : public AsyncFileActionThreadPool<absl::StatusOr<struct stat>> {
public:
  ActionStat(AsyncFileHandle handle, std::function<void(absl::StatusOr<struct stat>)> on_complete)
      : AsyncFileActionThreadPool<absl::StatusOr<struct stat>>(handle, on_complete) {}

  absl::StatusOr<struct stat> executeImpl() override {
    ASSERT(fileDescriptor() != -1);
    struct stat stat_result;
    auto result = posix().fstat(fileDescriptor(), &stat_result);
    if (result.return_value_ != 0) {
      return statusAfterFileError(result);
    }
    return stat_result;
  }
};

class ActionCreateHardLink : public AsyncFileActionThreadPool<absl::Status> {
public:
  ActionCreateHardLink(AsyncFileHandle handle, absl::string_view filename,
                       std::function<void(absl::Status)> on_complete)
      : AsyncFileActionThreadPool<absl::Status>(handle, on_complete), filename_(filename) {}

  absl::Status executeImpl() override {
    ASSERT(fileDescriptor() != -1);
    std::string procfile = absl::StrCat("/proc/self/fd/", fileDescriptor());
    auto result = posix().linkat(fileDescriptor(), procfile.c_str(), AT_FDCWD, filename_.c_str(),
                                 AT_SYMLINK_FOLLOW);
    if (result.return_value_ == -1) {
      return statusAfterFileError(result);
    }
    return absl::OkStatus();
  }

  void onCancelledBeforeCallback(absl::Status result) override {
    if (result.ok()) {
      posix().unlink(filename_.c_str());
    }
  }

private:
  const std::string filename_;
};

class ActionCloseFile : public AsyncFileActionThreadPool<absl::Status> {
public:
  // Here we take a copy of the AsyncFileContext's file descriptor, because the close function
  // sets the AsyncFileContext's file descriptor to -1. This way there will be no race of trying
  // to use the handle again while the close is in flight.
  explicit ActionCloseFile(AsyncFileHandle handle, std::function<void(absl::Status)> on_complete)
      : AsyncFileActionThreadPool<absl::Status>(handle, on_complete),
        file_descriptor_(fileDescriptor()) {}

  absl::Status executeImpl() override {
    auto result = posix().close(file_descriptor_);
    if (result.return_value_ == -1) {
      return statusAfterFileError(result);
    }
    return absl::OkStatus();
  }

private:
  const int file_descriptor_;
};

class ActionReadFile : public AsyncFileActionThreadPool<absl::StatusOr<Buffer::InstancePtr>> {
public:
  ActionReadFile(AsyncFileHandle handle, off_t offset, size_t length,
                 std::function<void(absl::StatusOr<Buffer::InstancePtr>)> on_complete)
      : AsyncFileActionThreadPool<absl::StatusOr<Buffer::InstancePtr>>(handle, on_complete),
        offset_(offset), length_(length) {}

  absl::StatusOr<Buffer::InstancePtr> executeImpl() override {
    ASSERT(fileDescriptor() != -1);
    auto result = std::make_unique<Buffer::OwnedImpl>();
    auto reservation = result->reserveSingleSlice(length_);
    auto bytes_read = posix().pread(fileDescriptor(), reservation.slice().mem_, length_, offset_);
    if (bytes_read.return_value_ == -1) {
      return statusAfterFileError(bytes_read);
    }
    if (static_cast<size_t>(bytes_read.return_value_) != length_) {
      result =
          std::make_unique<Buffer::OwnedImpl>(reservation.slice().mem_, bytes_read.return_value_);
    } else {
      reservation.commit(bytes_read.return_value_);
    }
    return result;
  }

private:
  const off_t offset_;
  const size_t length_;
};

class ActionWriteFile : public AsyncFileActionThreadPool<absl::StatusOr<size_t>> {
public:
  ActionWriteFile(AsyncFileHandle handle, Buffer::Instance& contents, off_t offset,
                  std::function<void(absl::StatusOr<size_t>)> on_complete)
      : AsyncFileActionThreadPool<absl::StatusOr<size_t>>(handle, on_complete), offset_(offset) {
    contents_.move(contents);
  }

  absl::StatusOr<size_t> executeImpl() override {
    ASSERT(fileDescriptor() != -1);
    auto slices = contents_.getRawSlices();
    size_t total_bytes_written = 0;
    for (const auto& slice : slices) {
      size_t slice_bytes_written = 0;
      while (slice_bytes_written < slice.len_) {
        auto bytes_just_written =
            posix().pwrite(fileDescriptor(), static_cast<char*>(slice.mem_) + slice_bytes_written,
                           slice.len_ - slice_bytes_written, offset_ + total_bytes_written);
        if (bytes_just_written.return_value_ == -1) {
          return statusAfterFileError(bytes_just_written);
        }
        slice_bytes_written += bytes_just_written.return_value_;
        total_bytes_written += bytes_just_written.return_value_;
      }
    }
    return total_bytes_written;
  }

private:
  Buffer::OwnedImpl contents_;
  const off_t offset_;
};

class ActionDuplicateFile : public AsyncFileActionThreadPool<absl::StatusOr<AsyncFileHandle>> {
public:
  ActionDuplicateFile(AsyncFileHandle handle,
                      std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete)
      : AsyncFileActionThreadPool<absl::StatusOr<AsyncFileHandle>>(handle, on_complete) {}

  absl::StatusOr<AsyncFileHandle> executeImpl() override {
    ASSERT(fileDescriptor() != -1);
    auto newfd = posix().duplicate(fileDescriptor());
    if (newfd.return_value_ == -1) {
      return statusAfterFileError(newfd);
    }
    return std::make_shared<AsyncFileContextThreadPool>(context()->manager(), newfd.return_value_);
  }

  void onCancelledBeforeCallback(absl::StatusOr<AsyncFileHandle> result) override {
    if (result.ok()) {
      result.value()->close([](absl::Status) {}).IgnoreError();
    }
  }
};

} // namespace

absl::StatusOr<CancelFunction>
AsyncFileContextThreadPool::stat(std::function<void(absl::StatusOr<struct stat>)> on_complete) {
  return checkFileAndEnqueue(std::make_shared<ActionStat>(handle(), std::move(on_complete)));
}

absl::StatusOr<CancelFunction>
AsyncFileContextThreadPool::createHardLink(absl::string_view filename,
                                           std::function<void(absl::Status)> on_complete) {
  return checkFileAndEnqueue(
      std::make_shared<ActionCreateHardLink>(handle(), filename, std::move(on_complete)));
}

absl::Status AsyncFileContextThreadPool::close(std::function<void(absl::Status)> on_complete) {
  auto status =
      checkFileAndEnqueue(std::make_shared<ActionCloseFile>(handle(), std::move(on_complete)))
          .status();
  fileDescriptor() = -1;
  return status;
}

absl::StatusOr<CancelFunction> AsyncFileContextThreadPool::read(
    off_t offset, size_t length,
    std::function<void(absl::StatusOr<Buffer::InstancePtr>)> on_complete) {
  return checkFileAndEnqueue(
      std::make_shared<ActionReadFile>(handle(), offset, length, std::move(on_complete)));
}

absl::StatusOr<CancelFunction>
AsyncFileContextThreadPool::write(Buffer::Instance& contents, off_t offset,
                                  std::function<void(absl::StatusOr<size_t>)> on_complete) {
  return checkFileAndEnqueue(
      std::make_shared<ActionWriteFile>(handle(), contents, offset, std::move(on_complete)));
}

absl::StatusOr<CancelFunction> AsyncFileContextThreadPool::duplicate(
    std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) {
  return checkFileAndEnqueue(
      std::make_shared<ActionDuplicateFile>(handle(), std::move(on_complete)));
}

absl::StatusOr<CancelFunction>
AsyncFileContextThreadPool::checkFileAndEnqueue(std::shared_ptr<AsyncFileAction> action) {
  if (fileDescriptor() == -1) {
    return absl::FailedPreconditionError("file was already closed");
  }
  return enqueue(action);
}

AsyncFileContextThreadPool::AsyncFileContextThreadPool(AsyncFileManager& manager, int fd)
    : AsyncFileContextBase(manager), file_descriptor_(fd) {}

AsyncFileContextThreadPool::~AsyncFileContextThreadPool() { ASSERT(file_descriptor_ == -1); }

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy

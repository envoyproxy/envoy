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

  const PosixFileOperations& posix() const {
    return static_cast<AsyncFileManagerThreadPool&>(context()->manager()).posix();
  }

  AsyncFileHandle handle_;
};

class ActionCreateHardLink : public AsyncFileActionThreadPool<absl::Status> {
public:
  ActionCreateHardLink(AsyncFileHandle handle, absl::string_view filename,
                       std::function<void(absl::Status)> on_complete)
      : AsyncFileActionThreadPool<absl::Status>(handle, on_complete), filename_(filename) {}

  absl::Status executeImpl() override {
    ASSERT(fileDescriptor() != -1);
    std::string procfile = absl::StrCat("/proc/self/fd/", fileDescriptor());
    int result = posix().linkat(fileDescriptor(), procfile.c_str(), AT_FDCWD, filename_.c_str(),
                                AT_SYMLINK_FOLLOW);
    if (result == -1) {
      return statusAfterFileError();
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
  explicit ActionCloseFile(AsyncFileHandle handle, std::function<void(absl::Status)> on_complete)
      : AsyncFileActionThreadPool<absl::Status>(handle, on_complete) {}

  absl::Status executeImpl() override {
    ASSERT(fileDescriptor() != -1);
    if (posix().close(fileDescriptor()) == -1) {
      return statusAfterFileError();
    }
    fileDescriptor() = -1;
    return absl::OkStatus();
  }
};

class ActionReadFile
    : public AsyncFileActionThreadPool<absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>>> {
public:
  ActionReadFile(
      AsyncFileHandle handle, off_t offset, size_t length,
      std::function<void(absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>>)> on_complete)
      : AsyncFileActionThreadPool<absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>>>(
            handle, on_complete),
        offset_(offset), length_(length) {}

  absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> executeImpl() override {
    ASSERT(fileDescriptor() != -1);
    auto result = std::make_unique<Envoy::Buffer::OwnedImpl>();
    auto reservation = result->reserveSingleSlice(length_);
    int bytes_read = posix().pread(fileDescriptor(), reservation.slice().mem_, length_, offset_);
    if (bytes_read == -1) {
      return statusAfterFileError();
    }
    if (static_cast<unsigned int>(bytes_read) != length_) {
      result = std::make_unique<Envoy::Buffer::OwnedImpl>(reservation.slice().mem_, bytes_read);
    } else {
      reservation.commit(bytes_read);
    }
    return result;
  }

private:
  const off_t offset_;
  const size_t length_;
};

class ActionWriteFile : public AsyncFileActionThreadPool<absl::StatusOr<size_t>> {
public:
  ActionWriteFile(AsyncFileHandle handle, Envoy::Buffer::Instance& contents, off_t offset,
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
        ssize_t bytes_just_written =
            posix().pwrite(fileDescriptor(), static_cast<char*>(slice.mem_) + slice_bytes_written,
                           slice.len_ - slice_bytes_written, offset_ + total_bytes_written);
        if (bytes_just_written == -1) {
          return statusAfterFileError();
        }
        slice_bytes_written += bytes_just_written;
        total_bytes_written += bytes_just_written;
      }
    }
    return total_bytes_written;
  }

private:
  Envoy::Buffer::OwnedImpl contents_;
  const off_t offset_;
};

class ActionDuplicateFile : public AsyncFileActionThreadPool<absl::StatusOr<AsyncFileHandle>> {
public:
  ActionDuplicateFile(AsyncFileHandle handle,
                      std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete)
      : AsyncFileActionThreadPool<absl::StatusOr<AsyncFileHandle>>(handle, on_complete) {}

  absl::StatusOr<AsyncFileHandle> executeImpl() override {
    ASSERT(fileDescriptor() != -1);
    int newfd = posix().dup(fileDescriptor());
    if (newfd == -1) {
      return statusAfterFileError();
    }
    return std::make_shared<AsyncFileContextThreadPool>(context()->manager(), newfd);
  }

  void onCancelledBeforeCallback(absl::StatusOr<AsyncFileHandle> result) override {
    if (result.ok()) {
      result.value()->close([](absl::Status) {});
    }
  }
};

} // namespace

std::function<void()>
AsyncFileContextThreadPool::createHardLink(absl::string_view filename,
                                           std::function<void(absl::Status)> on_complete) {
  return enqueue(
      std::make_shared<ActionCreateHardLink>(handle(), filename, std::move(on_complete)));
}

std::function<void()>
AsyncFileContextThreadPool::close(std::function<void(absl::Status)> on_complete) {
  return enqueue(std::make_shared<ActionCloseFile>(handle(), std::move(on_complete)));
}

std::function<void()> AsyncFileContextThreadPool::read(
    off_t offset, size_t length,
    std::function<void(absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>>)> on_complete) {
  return enqueue(
      std::make_shared<ActionReadFile>(handle(), offset, length, std::move(on_complete)));
}

std::function<void()>
AsyncFileContextThreadPool::write(Envoy::Buffer::Instance& contents, off_t offset,
                                  std::function<void(absl::StatusOr<size_t>)> on_complete) {
  return enqueue(
      std::make_shared<ActionWriteFile>(handle(), contents, offset, std::move(on_complete)));
}

std::function<void()> AsyncFileContextThreadPool::duplicate(
    std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) {
  return enqueue(std::make_shared<ActionDuplicateFile>(handle(), std::move(on_complete)));
}

AsyncFileContextThreadPool::AsyncFileContextThreadPool(AsyncFileManager& manager, int fd)
    : AsyncFileContextBase(manager), file_descriptor_(fd) {}

AsyncFileContextThreadPool::~AsyncFileContextThreadPool() { ASSERT(file_descriptor_ == -1); }

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy

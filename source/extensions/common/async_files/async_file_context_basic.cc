#include "source/extensions/common/async_files/async_file_context_basic.h"

#include <fcntl.h>

#include <memory>
#include <string>
#include <utility>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_context_impl.h"
#include "source/extensions/common/async_files/status_after_file_error.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

namespace {
class AsyncFileActionImpl : public AsyncFileAction {
protected:
  int& fileDescriptor(AsyncFileContextImpl* context) {
    return static_cast<AsyncFileContextBasic*>(context)->file_descriptor_;
  }
};

class ActionCreateAnonymousFile : public AsyncFileActionImpl {
public:
  ActionCreateAnonymousFile(std::string path, std::function<void(absl::Status)> on_complete)
      : on_complete_(on_complete), path_(path) {}

  void execute(AsyncFileContextImpl* context) override {
    if (fileDescriptor(context) != -1) {
      result_ = absl::AlreadyExistsError(
          "AsyncFileContext::createAnonymousFile: a file was already opened in this context");
      return;
    }
    bool was_successful_first_call = false;
    static bool supports_O_TMPFILE;
    static std::once_flag once_flag;
    std::call_once(once_flag, [this, &context, &was_successful_first_call]() {
      fileDescriptor(context) = open(path_.c_str(), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);
      was_successful_first_call = supports_O_TMPFILE = (fileDescriptor(context) != -1);
    });
    if (supports_O_TMPFILE) {
      if (was_successful_first_call) {
        // This was the thread doing the very first open(O_TMPFILE), and it worked, so no need to do
        // anything else.
        return;
      }
      // This was any other thread, but O_TMPFILE proved it worked, so we can do it again.
      fileDescriptor(context) = open(path_.c_str(), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);
      if (fileDescriptor(context) == -1) {
        result_ = statusAfterFileError();
      }
      return;
    }
    // If O_TMPFILE didn't work, fall back to creating a named file and unlinking it.
    // Use a fixed-size buffer because we're going to be using C file functions anyway, and it saves
    // a heap allocation.
    char filename[4096];
    static const char file_suffix[] = "/buffer.XXXXXX";
    if (path_.size() + sizeof(file_suffix) > sizeof(filename)) {
      result_ = absl::InvalidArgumentError(
          "AsyncFileContext::createAnonymousFile: pathname too long for tmpfile");
      return;
    }
    snprintf(filename, sizeof(filename), "%s%s", path_.c_str(), file_suffix);
    fileDescriptor(context) = mkstemp(filename);
    if (fileDescriptor(context) == -1) {
      result_ = statusAfterFileError();
      return;
    }
    if (::unlink(filename) != 0) {
      // Most likely the problem here is we can't unlink a file while it's open - since that's a
      // prerequisite of the desired behavior of this function, and we don't want to accidentally
      // fill a disk with named tmp files, if this happens we close the file, unlink it, and report
      // an error.
      result_ = absl::UnimplementedError("AsyncFileContext::createAnonymousFile: not supported for "
                                         "target filesystem (failed to unlink an open file)");
      ::close(fileDescriptor(context));
      ::unlink(filename);
      fileDescriptor(context) = -1;
      return;
    }
  }

  void callback() override { on_complete_(result_); }

private:
  absl::Status result_;
  const std::function<void(absl::Status)> on_complete_;
  const std::string path_;
};

class ActionCreateHardLink : public AsyncFileActionImpl {
public:
  ActionCreateHardLink(std::string filename, std::function<void(absl::Status)> on_complete)
      : on_complete_(on_complete), filename_(filename) {}

  void execute(AsyncFileContextImpl* context) override {
    if (fileDescriptor(context) == -1) {
      result_ = absl::FailedPreconditionError(
          "AsyncFileContext::createHardLink: no file was open in this context");
      return;
    }
    char procfile[64];
    snprintf(procfile, sizeof(procfile), "/proc/self/fd/%d", fileDescriptor(context));
    int result =
        linkat(fileDescriptor(context), procfile, AT_FDCWD, filename_.c_str(), AT_SYMLINK_FOLLOW);
    if (result == -1) {
      result_ = statusAfterFileError();
    }
  }

  void callback() override { on_complete_(result_); }

private:
  absl::Status result_;
  const std::function<void(absl::Status)> on_complete_;
  const std::string filename_;
};

class ActionUnlink : public AsyncFileActionImpl {
public:
  ActionUnlink(std::string filename, std::function<void(absl::Status)> on_complete)
      : on_complete_(on_complete), filename_(filename) {}

  void execute(AsyncFileContextImpl*) override {
    int result = ::unlink(filename_.c_str());
    if (result == -1) {
      result_ = statusAfterFileError();
    }
  }

  void callback() override { on_complete_(result_); }

private:
  absl::Status result_;
  const std::function<void(absl::Status)> on_complete_;
  const std::string filename_;
};

class ActionOpenExistingFile : public AsyncFileActionImpl {
public:
  ActionOpenExistingFile(std::string filename, AsyncFileContext::Mode mode,
                         std::function<void(absl::Status)> on_complete)
      : on_complete_(on_complete), filename_(filename), mode_(mode) {}

  void execute(AsyncFileContextImpl* context) override {
    if (fileDescriptor(context) != -1) {
      result_ = absl::AlreadyExistsError(
          "AsyncFileContext::openExistingFile: a file was already opened in this context");
      return;
    }
    fileDescriptor(context) = open(filename_.c_str(), openFlags());
    if (fileDescriptor(context) == -1) {
      result_ = statusAfterFileError();
    }
  }

  void callback() override { on_complete_(result_); }

private:
  int openFlags() const {
    switch (mode_) {
    case AsyncFileContext::Mode::READONLY:
      return O_RDONLY;
    case AsyncFileContext::Mode::WRITEONLY:
      return O_WRONLY;
    case AsyncFileContext::Mode::READWRITE:
    default:
      return O_RDWR;
    }
  }
  absl::Status result_;
  const std::function<void(absl::Status)> on_complete_;
  const std::string filename_;
  const AsyncFileContext::Mode mode_;
};

class ActionCloseFile : public AsyncFileActionImpl {
public:
  explicit ActionCloseFile(std::function<void(absl::Status)> on_complete)
      : on_complete_(on_complete) {}

  void execute(AsyncFileContextImpl* context) override {
    if (fileDescriptor(context) == -1) {
      result_ = absl::FailedPreconditionError(
          "AsyncFileContext::close: no file was opened in this context");
      return;
    }
    if (::close(fileDescriptor(context)) == -1) {
      result_ = statusAfterFileError();
      return;
    }
    fileDescriptor(context) = -1;
    result_ = absl::OkStatus();
  }

  void callback() override { on_complete_(result_); }

private:
  absl::Status result_;
  const std::function<void(absl::Status)> on_complete_;
};

class ActionReadFile : public AsyncFileActionImpl {
public:
  ActionReadFile(
      off_t offset, size_t length,
      std::function<void(absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>>)> on_complete)
      : offset_(offset), length_(length), on_complete_(on_complete) {}

  void execute(AsyncFileContextImpl* context) override {
    if (fileDescriptor(context) == -1) {
      result_ = absl::FailedPreconditionError(
          "AsyncFileContext::read: no file was opened in this context");
      return;
    }
    result_ = std::make_unique<Envoy::Buffer::OwnedImpl>();
    auto reservation = result_.value()->reserveSingleSlice(length_);
    int bytes_read = pread(fileDescriptor(context), reservation.slice().mem_, length_, offset_);
    if (bytes_read == -1) {
      result_ = statusAfterFileError();
      return;
    }
    if (static_cast<unsigned int>(bytes_read) != length_) {
      result_ = std::make_unique<Envoy::Buffer::OwnedImpl>(reservation.slice().mem_, bytes_read);
    } else {
      reservation.commit(bytes_read);
    }
  }

  void callback() override { on_complete_(std::move(result_)); }

private:
  const off_t offset_;
  const size_t length_;
  absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>> result_;
  const std::function<void(absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>>)> on_complete_;
};

class ActionWriteFile : public AsyncFileActionImpl {
public:
  ActionWriteFile(Envoy::Buffer::Instance& contents, off_t offset, // NOLINT(runtime/references)
                  std::function<void(absl::StatusOr<size_t>)> on_complete)
      : offset_(offset), on_complete_(on_complete) {
    contents_.move(contents);
  }

  void execute(AsyncFileContextImpl* context) override {
    if (fileDescriptor(context) == -1) {
      result_ = absl::FailedPreconditionError(
          "AsyncFileContext::read: no file was opened in this context");
      return;
    }
    auto slices = contents_.getRawSlices();
    ssize_t total_bytes_written = 0;
    for (const auto& slice : slices) {
      ssize_t bytes_written =
          pwrite(fileDescriptor(context), slice.mem_, slice.len_, offset_ + total_bytes_written);
      if (bytes_written == -1) {
        result_ = statusAfterFileError();
        return;
      }
      total_bytes_written += bytes_written;
      if (static_cast<unsigned int>(bytes_written) != slice.len_) {
        result_ = total_bytes_written;
        return;
      }
    }
    result_ = total_bytes_written;
  }

  void callback() override { on_complete_(result_); }

private:
  Envoy::Buffer::OwnedImpl contents_;
  const off_t offset_;
  absl::StatusOr<size_t> result_;
  const std::function<void(absl::StatusOr<size_t>)> on_complete_;
};

} // namespace

void AsyncFileContextBasic::createAnonymousFile(std::string path,
                                                std::function<void(absl::Status)> on_complete) {
  enqueue(std::make_unique<ActionCreateAnonymousFile>(std::move(path), std::move(on_complete)));
}

void AsyncFileContextBasic::createHardLink(std::string filename,
                                           std::function<void(absl::Status)> on_complete) {
  enqueue(std::make_unique<ActionCreateHardLink>(std::move(filename), std::move(on_complete)));
}

void AsyncFileContextBasic::unlink(std::string filename,
                                   std::function<void(absl::Status)> on_complete) {
  enqueue(std::make_unique<ActionUnlink>(std::move(filename), std::move(on_complete)));
}

void AsyncFileContextBasic::openExistingFile(std::string filename, Mode mode,
                                             std::function<void(absl::Status)> on_complete) {
  enqueue(
      std::make_unique<ActionOpenExistingFile>(std::move(filename), mode, std::move(on_complete)));
}

void AsyncFileContextBasic::close(std::function<void(absl::Status)> on_complete) {
  enqueue(std::make_unique<ActionCloseFile>(std::move(on_complete)));
}

void AsyncFileContextBasic::read(
    off_t offset, size_t length,
    std::function<void(absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>>)> on_complete) {
  enqueue(std::make_unique<ActionReadFile>(offset, length, std::move(on_complete)));
}

void AsyncFileContextBasic::write(Envoy::Buffer::Instance& contents, off_t offset,
                                  std::function<void(absl::StatusOr<size_t>)> on_complete) {
  enqueue(std::make_unique<ActionWriteFile>(contents, offset, std::move(on_complete)));
}

void AsyncFileContextBasic::abortClose() {
  if (file_descriptor_ != -1) {
    ActionCloseFile([](absl::Status) {}).execute(this);
  }
}

absl::StatusOr<std::shared_ptr<AsyncFileContext>> AsyncFileContextBasic::duplicate() {
  if (file_descriptor_ == -1) {
    return absl::FailedPreconditionError(
        "AsyncFileContext::duplicate: no file was open in this context");
  }
  int newfd = ::dup(file_descriptor_);
  if (newfd == -1) {
    return statusAfterFileError();
  }
  return std::shared_ptr<AsyncFileContextImpl>(new AsyncFileContextBasic(manager_, newfd));
}

AsyncFileContextBasic::AsyncFileContextBasic(AsyncFileManager* manager, int fd /* = -1 */)
    : AsyncFileContextImpl(manager), file_descriptor_(fd) {}

std::shared_ptr<AsyncFileContextImpl> AsyncFileContextBasic::create(AsyncFileManager* manager) {
  // Uses new because the constructor is private, so make_shared can't use it.
  return std::shared_ptr<AsyncFileContextImpl>(new AsyncFileContextBasic(manager));
}

AsyncFileContextBasic::~AsyncFileContextBasic() { AsyncFileContextBasic::abortClose(); }

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy

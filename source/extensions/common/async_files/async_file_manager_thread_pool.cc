#include "source/extensions/common/async_files/async_file_manager_thread_pool.h"

#include <memory>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_context_base.h"
#include "source/extensions/common/async_files/async_file_context_thread_pool.h"
#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/status_after_file_error.h"

#include "absl/base/thread_annotations.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

// next_action_ is per worker thread; if enqueue is called from a callback
// the action goes directly into next_action_, otherwise it goes into the
// queue and is eventually pulled out into next_action_ by a worker thread.
static thread_local std::shared_ptr<AsyncFileAction> next_action_;

// is_worker_thread_ is set to true for worker threads, and will be false
// for all other threads.
static thread_local bool is_worker_thread_ = false;

AsyncFileManagerThreadPool::AsyncFileManagerThreadPool(const AsyncFileManagerConfig& config)
    : posix_(config.substitute_posix_file_operations == nullptr
                 ? realPosixFileOperations()
                 : *config.substitute_posix_file_operations) {
  unsigned int thread_pool_size = config.thread_pool_size.value();
  if (thread_pool_size == 0) {
    thread_pool_size = std::thread::hardware_concurrency();
  }
  thread_pool_.reserve(thread_pool_size);
  while (thread_pool_.size() < thread_pool_size) {
    thread_pool_.emplace_back([this]() { worker(); });
  }
}

AsyncFileManagerThreadPool::~AsyncFileManagerThreadPool() ABSL_LOCKS_EXCLUDED(queue_mutex_) {
  {
    absl::MutexLock lock(&queue_mutex_);
    terminate_ = true;
  }
  while (!thread_pool_.empty()) {
    thread_pool_.back().join();
    thread_pool_.pop_back();
  }
}

std::string AsyncFileManagerThreadPool::describe() const {
  return absl::StrCat("thread_pool_size = ", thread_pool_.size());
}

std::function<void()> AsyncFileManagerThreadPool::enqueue(std::shared_ptr<AsyncFileAction> action) {
  auto cancelFunc = [action]() { action->cancel(); };
  absl::MutexLock lock(&queue_mutex_);
  if (is_worker_thread_) {
    ASSERT(!next_action_); // only do one file action per callback.
    next_action_ = std::move(action);
    return cancelFunc;
  }
  queue_.push(std::move(action));
  return cancelFunc;
}

void AsyncFileManagerThreadPool::worker() {
  is_worker_thread_ = true;
  while (true) {
    const auto condition = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(queue_mutex_) {
      return !queue_.empty() || terminate_;
    };
    {
      absl::MutexLock lock(&queue_mutex_);
      queue_mutex_.Await(absl::Condition(&condition));
      if (terminate_) {
        return;
      }
      next_action_ = std::move(queue_.front());
      queue_.pop();
    }
    resolveActions();
  }
}

void AsyncFileManagerThreadPool::resolveActions() {
  while (next_action_) {
    // Move the action out of next_action_ so that its callback can enqueue
    // a different next_action_ without self-destructing.
    std::shared_ptr<AsyncFileAction> action = std::move(next_action_);
    action->execute();
  }
}

namespace {

class ActionWithFileResult : public AsyncFileActionWithResult<absl::StatusOr<AsyncFileHandle>> {
public:
  ActionWithFileResult(AsyncFileManagerThreadPool& manager,
                       std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete)
      : AsyncFileActionWithResult(on_complete), manager_(manager) {}

protected:
  void onCancelledBeforeCallback(absl::StatusOr<AsyncFileHandle> result) override {
    if (result.ok()) {
      result.value()->close([](absl::Status) {});
    }
  }
  AsyncFileManagerThreadPool& manager_;
  const PosixFileOperations& posix() { return manager_.posix(); }
};

class ActionCreateAnonymousFile : public ActionWithFileResult {
public:
  ActionCreateAnonymousFile(AsyncFileManagerThreadPool& manager, absl::string_view path,
                            std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete)
      : ActionWithFileResult(manager, on_complete), path_(path) {}

  absl::StatusOr<AsyncFileHandle> executeImpl() override {
    bool was_successful_first_call = false;
    int fd;
    std::call_once(manager_.once_flag_, [this, &was_successful_first_call, &fd]() {
      fd = posix().open(path_.c_str(), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);
      was_successful_first_call = manager_.supports_o_tmpfile_ = (fd != -1);
    });
    if (manager_.supports_o_tmpfile_) {
      if (was_successful_first_call) {
        // This was the thread doing the very first open(O_TMPFILE), and it worked, so no need to do
        // anything else.
        return std::make_shared<AsyncFileContextThreadPool>(manager_, fd);
      }
      // This was any other thread, but O_TMPFILE proved it worked, so we can do it again.
      fd = posix().open(path_.c_str(), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);
      if (fd == -1) {
        return statusAfterFileError();
      }
      return std::make_shared<AsyncFileContextThreadPool>(manager_, fd);
    }
    // If O_TMPFILE didn't work, fall back to creating a named file and unlinking it.
    // Use a fixed-size buffer because we're going to be using C file functions anyway, and it saves
    // a heap allocation.
    char filename[4096];
    static const char file_suffix[] = "/buffer.XXXXXX";
    if (path_.size() + sizeof(file_suffix) > sizeof(filename)) {
      return absl::InvalidArgumentError(
          "AsyncFileManagerThreadPool::createAnonymousFile: pathname too long for tmpfile");
    }
    // Using C-style functions here because `mkstemp` requires a writable
    // char buffer, and we can't give it that with C++ strings.
    snprintf(filename, sizeof(filename), "%s%s", path_.c_str(), file_suffix);
    fd = posix().mkstemp(filename);
    if (fd == -1) {
      return statusAfterFileError();
    }
    if (posix().unlink(filename) != 0) {
      // Most likely the problem here is we can't unlink a file while it's open - since that's a
      // prerequisite of the desired behavior of this function, and we don't want to accidentally
      // fill a disk with named tmp files, if this happens we close the file, unlink it, and report
      // an error.
      posix().close(fd);
      posix().unlink(filename);
      return absl::UnimplementedError(
          "AsyncFileManagerThreadPool::createAnonymousFile: not supported for "
          "target filesystem (failed to unlink an open file)");
    }
    return std::make_shared<AsyncFileContextThreadPool>(manager_, fd);
  }

private:
  const std::string path_;
};

class ActionOpenExistingFile : public ActionWithFileResult {
public:
  ActionOpenExistingFile(AsyncFileManagerThreadPool& manager, absl::string_view filename,
                         AsyncFileManager::Mode mode,
                         std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete)
      : ActionWithFileResult(manager, on_complete), filename_(filename), mode_(mode) {}

  absl::StatusOr<AsyncFileHandle> executeImpl() override {
    int fd = posix().open(filename_.c_str(), openFlags());
    if (fd == -1) {
      return statusAfterFileError();
    }
    return std::make_shared<AsyncFileContextThreadPool>(manager_, fd);
  }

private:
  int openFlags() const {
    switch (mode_) {
    case AsyncFileManager::Mode::ReadOnly:
      return O_RDONLY;
    case AsyncFileManager::Mode::WriteOnly:
      return O_WRONLY;
    case AsyncFileManager::Mode::ReadWrite:
    default:
      return O_RDWR;
    }
  }
  const std::string filename_;
  const AsyncFileManager::Mode mode_;
};

class ActionUnlink : public AsyncFileActionWithResult<absl::Status> {
public:
  ActionUnlink(const PosixFileOperations& posix, absl::string_view filename,
               std::function<void(absl::Status)> on_complete)
      : AsyncFileActionWithResult(on_complete), posix_(posix), filename_(filename) {}

  absl::Status executeImpl() override {
    int result = posix_.unlink(filename_.c_str());
    if (result == -1) {
      return statusAfterFileError();
    }
    return absl::OkStatus();
  }

private:
  const PosixFileOperations& posix_;
  const std::string filename_;
};

} // namespace

std::function<void()> AsyncFileManagerThreadPool::createAnonymousFile(
    absl::string_view path, std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) {
  return enqueue(std::make_shared<ActionCreateAnonymousFile>(*this, path, on_complete));
}

std::function<void()> AsyncFileManagerThreadPool::openExistingFile(
    absl::string_view filename, Mode mode,
    std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) {
  return enqueue(std::make_shared<ActionOpenExistingFile>(*this, filename, mode, on_complete));
}

std::function<void()>
AsyncFileManagerThreadPool::unlink(absl::string_view filename,
                                   std::function<void(absl::Status)> on_complete) {
  return enqueue(std::make_shared<ActionUnlink>(posix(), filename, on_complete));
}

class AsyncFileManagerThreadPoolFactory : public AsyncFileManagerFactory {
  bool shouldUseThisFactory(const AsyncFileManagerConfig& config) const override {
    return config.thread_pool_size.has_value();
  }
  std::unique_ptr<AsyncFileManager> create(const AsyncFileManagerConfig& config) const override {
    return std::make_unique<AsyncFileManagerThreadPool>(config);
  }
};
static AsyncFileManagerThreadPoolFactory registered;

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy

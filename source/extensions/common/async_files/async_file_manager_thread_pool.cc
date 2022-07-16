#include "source/extensions/common/async_files/async_file_manager_thread_pool.h"

#include <memory>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_context_thread_pool.h"
#include "source/extensions/common/async_files/status_after_file_error.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

namespace {
// ThreadNextAction is per worker thread; if enqueue is called from a callback
// the action goes directly into ThreadNextAction, otherwise it goes into the
// queue and is eventually pulled out into ThreadNextAction by a worker thread.
thread_local std::shared_ptr<AsyncFileAction> ThreadNextAction;

// ThreadIsWorker is set to true for worker threads, and will be false
// for all other threads.
thread_local bool ThreadIsWorker = false;
} // namespace

AsyncFileManagerThreadPool::AsyncFileManagerThreadPool(
    const envoy::extensions::common::async_files::v3::AsyncFileManagerConfig& config,
    Api::OsSysCalls& posix)
    : posix_(posix) {
  if (!posix.supportsAllPosixFileOperations()) {
    throw EnvoyException("AsyncFileManagerThreadPool not supported");
  }
  unsigned int thread_pool_size = config.thread_pool().thread_count();
  if (thread_pool_size == 0) {
    thread_pool_size = std::thread::hardware_concurrency();
  }
  ENVOY_LOG(info, fmt::format("AsyncFileManagerThreadPool created with id '{}', with {} threads",
                              config.id(), thread_pool_size));
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
  auto cancel_func = [action]() { action->cancel(); };
  // If an action is being enqueued from within a callback, we don't have to actually queue it,
  // we can just set it as the thread's next action - this acts to chain the actions without
  // yielding to another file.
  if (ThreadIsWorker) {
    ASSERT(!ThreadNextAction); // only do one file action per callback.
    ThreadNextAction = std::move(action);
    return cancel_func;
  }
  absl::MutexLock lock(&queue_mutex_);
  queue_.push(std::move(action));
  return cancel_func;
}

void AsyncFileManagerThreadPool::worker() {
  ThreadIsWorker = true;
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
      ThreadNextAction = std::move(queue_.front());
      queue_.pop();
    }
    resolveActions();
  }
}

void AsyncFileManagerThreadPool::resolveActions() {
  while (ThreadNextAction) {
    // Move the action out of ThreadNextAction so that its callback can enqueue
    // a different ThreadNextAction without self-destructing.
    std::shared_ptr<AsyncFileAction> action = std::move(ThreadNextAction);
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
      result.value()->close([](absl::Status) {}).IgnoreError();
    }
  }
  AsyncFileManagerThreadPool& manager_;
  Api::OsSysCalls& posix() { return manager_.posix(); }
};

class ActionCreateAnonymousFile : public ActionWithFileResult {
public:
  ActionCreateAnonymousFile(AsyncFileManagerThreadPool& manager, absl::string_view path,
                            std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete)
      : ActionWithFileResult(manager, on_complete), path_(path) {}

  absl::StatusOr<AsyncFileHandle> executeImpl() override {
    Api::SysCallIntResult open_result;
#ifdef O_TMPFILE
    bool was_successful_first_call = false;
    std::call_once(manager_.once_flag_, [this, &was_successful_first_call, &open_result]() {
      open_result = posix().open(path_.c_str(), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);
      was_successful_first_call = manager_.supports_o_tmpfile_ = (open_result.return_value_ != -1);
    });
    if (manager_.supports_o_tmpfile_) {
      if (was_successful_first_call) {
        // This was the thread doing the very first open(O_TMPFILE), and it worked, so no need to do
        // anything else.
        return std::make_shared<AsyncFileContextThreadPool>(manager_, open_result.return_value_);
      }
      // This was any other thread, but O_TMPFILE proved it worked, so we can do it again.
      open_result = posix().open(path_.c_str(), O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);
      if (open_result.return_value_ == -1) {
        return statusAfterFileError(open_result);
      }
      return std::make_shared<AsyncFileContextThreadPool>(manager_, open_result.return_value_);
    }
#endif // O_TMPFILE
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
    open_result = posix().mkstemp(filename);
    if (open_result.return_value_ == -1) {
      return statusAfterFileError(open_result);
    }
    if (posix().unlink(filename).return_value_ != 0) {
      // Most likely the problem here is we can't unlink a file while it's open - since that's a
      // prerequisite of the desired behavior of this function, and we don't want to accidentally
      // fill a disk with named tmp files, if this happens we close the file, unlink it, and report
      // an error.
      posix().close(open_result.return_value_);
      posix().unlink(filename);
      return absl::UnimplementedError(
          "AsyncFileManagerThreadPool::createAnonymousFile: not supported for "
          "target filesystem (failed to unlink an open file)");
    }
    return std::make_shared<AsyncFileContextThreadPool>(manager_, open_result.return_value_);
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
    auto open_result = posix().open(filename_.c_str(), openFlags());
    if (open_result.return_value_ == -1) {
      return statusAfterFileError(open_result);
    }
    return std::make_shared<AsyncFileContextThreadPool>(manager_, open_result.return_value_);
  }

private:
  int openFlags() const {
    switch (mode_) {
    case AsyncFileManager::Mode::ReadOnly:
      return O_RDONLY;
    case AsyncFileManager::Mode::WriteOnly:
      return O_WRONLY;
    case AsyncFileManager::Mode::ReadWrite:
      return O_RDWR;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
  const std::string filename_;
  const AsyncFileManager::Mode mode_;
};

class ActionUnlink : public AsyncFileActionWithResult<absl::Status> {
public:
  ActionUnlink(Api::OsSysCalls& posix, absl::string_view filename,
               std::function<void(absl::Status)> on_complete)
      : AsyncFileActionWithResult(on_complete), posix_(posix), filename_(filename) {}

  absl::Status executeImpl() override {
    Api::SysCallIntResult unlink_result = posix_.unlink(filename_.c_str());
    if (unlink_result.return_value_ == -1) {
      return statusAfterFileError(unlink_result);
    }
    return absl::OkStatus();
  }

private:
  Api::OsSysCalls& posix_;
  const std::string filename_;
};

} // namespace

CancelFunction AsyncFileManagerThreadPool::createAnonymousFile(
    absl::string_view path, std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) {
  return enqueue(std::make_shared<ActionCreateAnonymousFile>(*this, path, on_complete));
}

CancelFunction AsyncFileManagerThreadPool::openExistingFile(
    absl::string_view filename, Mode mode,
    std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) {
  return enqueue(std::make_shared<ActionOpenExistingFile>(*this, filename, mode, on_complete));
}

CancelFunction AsyncFileManagerThreadPool::unlink(absl::string_view filename,
                                                  std::function<void(absl::Status)> on_complete) {
  return enqueue(std::make_shared<ActionUnlink>(posix(), filename, on_complete));
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy

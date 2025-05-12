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
  // This destructor will be blocked by this loop until all queued file actions are complete.
  while (!thread_pool_.empty()) {
    thread_pool_.back().join();
    thread_pool_.pop_back();
  }
}

std::string AsyncFileManagerThreadPool::describe() const {
  return absl::StrCat("thread_pool_size = ", thread_pool_.size());
}

void AsyncFileManagerThreadPool::waitForIdle() {
  const auto condition = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(queue_mutex_) {
    return active_workers_ == 0 && queue_.empty() && cleanup_queue_.empty();
  };
  absl::MutexLock lock(&queue_mutex_);
  queue_mutex_.Await(absl::Condition(&condition));
}

absl::AnyInvocable<void()>
AsyncFileManagerThreadPool::enqueue(Event::Dispatcher* dispatcher,
                                    std::unique_ptr<AsyncFileAction> action) {
  QueuedAction entry{std::move(action), dispatcher};
  auto cancel_func = [dispatcher, state = entry.state_]() {
    ASSERT(dispatcher == nullptr || dispatcher->isThreadSafe());
    state->store(QueuedAction::State::Cancelled);
  };
  absl::MutexLock lock(&queue_mutex_);
  queue_.push(std::move(entry));
  return cancel_func;
}

void AsyncFileManagerThreadPool::postCancelledActionForCleanup(
    std::unique_ptr<AsyncFileAction> action) {
  absl::MutexLock lock(&queue_mutex_);
  cleanup_queue_.push(std::move(action));
}

void AsyncFileManagerThreadPool::executeAction(QueuedAction&& queued_action) {
  using State = QueuedAction::State;
  State expected = State::Queued;
  std::shared_ptr<std::atomic<State>> state = std::move(queued_action.state_);
  std::unique_ptr<AsyncFileAction> action = std::move(queued_action.action_);
  if (!state->compare_exchange_strong(expected, State::Executing)) {
    ASSERT(expected == State::Cancelled);
    if (action->executesEvenIfCancelled()) {
      action->execute();
    }
    return;
  }
  action->execute();
  expected = State::Executing;
  if (!state->compare_exchange_strong(expected, State::InCallback)) {
    ASSERT(expected == State::Cancelled);
    action->onCancelledBeforeCallback();
    return;
  }
  if (queued_action.dispatcher_ == nullptr) {
    // No need to bother arranging the callback, because a dispatcher was not provided.
    return;
  }
  // If it is necessary to explicitly undo an action on cancel then the lambda will need a
  // pointer to this manager that is guaranteed to outlive the lambda, in order to be able
  // to perform that cancel operation on a thread belonging to the file manager.
  // So capture a shared_ptr if necessary, but, to avoid unnecessary shared_ptr wrangling,
  // leave it empty if the action doesn't have an associated cancel operation.
  std::shared_ptr<AsyncFileManagerThreadPool> manager;
  if (action->hasActionIfCancelledBeforeCallback()) {
    manager = shared_from_this();
  }
  queued_action.dispatcher_->post([manager = std::move(manager), action = std::move(action),
                                   state = std::move(state)]() mutable {
    // This callback runs on the caller's thread.
    State expected = State::InCallback;
    if (state->compare_exchange_strong(expected, State::Done)) {
      // Action was not cancelled; run the captured callback on the caller's thread.
      action->onComplete();
      return;
    }
    ASSERT(expected == State::Cancelled);
    if (manager == nullptr) {
      // Action had a "do nothing" cancellation so we don't need to post a cleanup action.
      return;
    }
    // If an action with side-effects was cancelled after being posted, its
    // side-effects need to be undone as the caller can no longer receive the
    // returned context. That undo action will need to be done on one of the
    // file manager's threads, as it is file related, so post it to the thread pool.
    manager->postCancelledActionForCleanup(std::move(action));
  });
}

void AsyncFileManagerThreadPool::worker() {
  const auto condition = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(queue_mutex_) {
    return !queue_.empty() || !cleanup_queue_.empty() || terminate_;
  };
  {
    absl::MutexLock lock(&queue_mutex_);
    active_workers_++;
  }
  while (true) {
    QueuedAction action;
    std::unique_ptr<AsyncFileAction> cleanup_action;
    {
      absl::MutexLock lock(&queue_mutex_);
      active_workers_--;
      queue_mutex_.Await(absl::Condition(&condition));
      if (terminate_ && queue_.empty() && cleanup_queue_.empty()) {
        return;
      }
      active_workers_++;
      if (!queue_.empty()) {
        action = std::move(queue_.front());
        queue_.pop();
      }
      if (!cleanup_queue_.empty()) {
        cleanup_action = std::move(cleanup_queue_.front());
        cleanup_queue_.pop();
      }
    }
    if (action.action_ != nullptr) {
      executeAction(std::move(action));
      action.action_ = nullptr;
    }
    if (cleanup_action != nullptr) {
      std::move(cleanup_action)->onCancelledBeforeCallback();
      cleanup_action = nullptr;
    }
  }
}

namespace {

class ActionWithFileResult : public AsyncFileActionWithResult<absl::StatusOr<AsyncFileHandle>> {
public:
  ActionWithFileResult(AsyncFileManagerThreadPool& manager,
                       absl::AnyInvocable<void(absl::StatusOr<AsyncFileHandle>)> on_complete)
      : AsyncFileActionWithResult(std::move(on_complete)), manager_(manager) {}

protected:
  void onCancelledBeforeCallback() override {
    if (result_.value().ok()) {
      result_.value().value()->close(nullptr, [](absl::Status) {}).IgnoreError();
    }
  }
  bool hasActionIfCancelledBeforeCallback() const override { return true; }

  AsyncFileManagerThreadPool& manager_;
  Api::OsSysCalls& posix() { return manager_.posix(); }
};

class ActionCreateAnonymousFile : public ActionWithFileResult {
public:
  ActionCreateAnonymousFile(AsyncFileManagerThreadPool& manager, absl::string_view path,
                            absl::AnyInvocable<void(absl::StatusOr<AsyncFileHandle>)> on_complete)
      : ActionWithFileResult(manager, std::move(on_complete)), path_(path) {}

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
                         absl::AnyInvocable<void(absl::StatusOr<AsyncFileHandle>)> on_complete)
      : ActionWithFileResult(manager, std::move(on_complete)), filename_(filename), mode_(mode) {}

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

class ActionStat : public AsyncFileActionWithResult<absl::StatusOr<struct stat>> {
public:
  ActionStat(Api::OsSysCalls& posix, absl::string_view filename,
             absl::AnyInvocable<void(absl::StatusOr<struct stat>)> on_complete)
      : AsyncFileActionWithResult(std::move(on_complete)), posix_(posix), filename_(filename) {}

  absl::StatusOr<struct stat> executeImpl() override {
    struct stat ret;
    Api::SysCallIntResult stat_result = posix_.stat(filename_.c_str(), &ret);
    if (stat_result.return_value_ == -1) {
      return statusAfterFileError(stat_result);
    }
    return ret;
  }

private:
  Api::OsSysCalls& posix_;
  const std::string filename_;
};

class ActionUnlink : public AsyncFileActionWithResult<absl::Status> {
public:
  ActionUnlink(Api::OsSysCalls& posix, absl::string_view filename,
               absl::AnyInvocable<void(absl::Status)> on_complete)
      : AsyncFileActionWithResult(std::move(on_complete)), posix_(posix), filename_(filename) {}

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
    Event::Dispatcher* dispatcher, absl::string_view path,
    absl::AnyInvocable<void(absl::StatusOr<AsyncFileHandle>)> on_complete) {
  return enqueue(dispatcher,
                 std::make_unique<ActionCreateAnonymousFile>(*this, path, std::move(on_complete)));
}

CancelFunction AsyncFileManagerThreadPool::openExistingFile(
    Event::Dispatcher* dispatcher, absl::string_view filename, Mode mode,
    absl::AnyInvocable<void(absl::StatusOr<AsyncFileHandle>)> on_complete) {
  return enqueue(dispatcher, std::make_unique<ActionOpenExistingFile>(*this, filename, mode,
                                                                      std::move(on_complete)));
}

CancelFunction AsyncFileManagerThreadPool::stat(
    Event::Dispatcher* dispatcher, absl::string_view filename,
    absl::AnyInvocable<void(absl::StatusOr<struct stat>)> on_complete) {
  return enqueue(dispatcher,
                 std::make_unique<ActionStat>(posix(), filename, std::move(on_complete)));
}

CancelFunction
AsyncFileManagerThreadPool::unlink(Event::Dispatcher* dispatcher, absl::string_view filename,
                                   absl::AnyInvocable<void(absl::Status)> on_complete) {
  return enqueue(dispatcher,
                 std::make_unique<ActionUnlink>(posix(), filename, std::move(on_complete)));
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy

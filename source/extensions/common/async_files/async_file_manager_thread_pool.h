#pragma once

#include <memory>
#include <queue>
#include <string>
#include <thread>

#include "envoy/extensions/common/async_files/v3/async_file_manager.pb.h"

#include "source/extensions/common/async_files/async_file_handle.h"
#include "source/extensions/common/async_files/async_file_manager.h"

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

// An AsyncFileManager which performs file operations in a thread pool.
// The operations are passed in through a queue, and performed in the order they are
// received, except when operations are chained, in which case the thread that
// performed the previous action in the chain immediately performs the newly chained
// action.
class AsyncFileManagerThreadPool : public AsyncFileManager,
                                   public std::enable_shared_from_this<AsyncFileManagerThreadPool>,
                                   protected Logger::Loggable<Logger::Id::main> {
public:
  explicit AsyncFileManagerThreadPool(
      const envoy::extensions::common::async_files::v3::AsyncFileManagerConfig& config,
      Api::OsSysCalls& posix);
  ~AsyncFileManagerThreadPool() ABSL_LOCKS_EXCLUDED(queue_mutex_) override;
  CancelFunction createAnonymousFile(
      Event::Dispatcher* dispatcher, absl::string_view path,
      absl::AnyInvocable<void(absl::StatusOr<AsyncFileHandle>)> on_complete) override;
  CancelFunction
  openExistingFile(Event::Dispatcher* dispatcher, absl::string_view filename, Mode mode,
                   absl::AnyInvocable<void(absl::StatusOr<AsyncFileHandle>)> on_complete) override;
  CancelFunction stat(Event::Dispatcher* dispatcher, absl::string_view filename,
                      absl::AnyInvocable<void(absl::StatusOr<struct stat>)> on_complete) override;
  CancelFunction unlink(Event::Dispatcher* dispatcher, absl::string_view filename,
                        absl::AnyInvocable<void(absl::Status)> on_complete) override;
  std::string describe() const override;
  void waitForIdle() override;
  Api::OsSysCalls& posix() const { return posix_; }

#ifdef O_TMPFILE
  // The first time we try to open an anonymous file, these values are used to capture whether
  // opening with O_TMPFILE works. If it does not, the first open is retried using 'mkstemp',
  // and all subsequent tries are redirected down that path. If it works, the first try is
  // used, and all subsequent tries use the same mechanism.
  // This is per-manager rather than static in part to facilitate testing, but also because
  // if there are multiple managers they may be operating on different file-systems with
  // different capabilities.
  std::once_flag once_flag_;
  bool supports_o_tmpfile_;
#endif // O_TMPFILE

private:
  absl::AnyInvocable<void()> enqueue(Event::Dispatcher* dispatcher,
                                     std::unique_ptr<AsyncFileAction> action)
      ABSL_LOCKS_EXCLUDED(queue_mutex_) override;
  void postCancelledActionForCleanup(std::unique_ptr<AsyncFileAction> action)
      ABSL_LOCKS_EXCLUDED(queue_mutex_) override;
  void worker() ABSL_LOCKS_EXCLUDED(queue_mutex_);

  absl::Mutex queue_mutex_;
  void executeAction(QueuedAction&& action);
  std::queue<QueuedAction> queue_ ABSL_GUARDED_BY(queue_mutex_);
  int active_workers_ ABSL_GUARDED_BY(queue_mutex_) = 0;
  std::queue<std::unique_ptr<AsyncFileAction>> cleanup_queue_ ABSL_GUARDED_BY(queue_mutex_);
  bool terminate_ ABSL_GUARDED_BY(queue_mutex_) = false;

  std::vector<std::thread> thread_pool_;
  Api::OsSysCalls& posix_;
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy

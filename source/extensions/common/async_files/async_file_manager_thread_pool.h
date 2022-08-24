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
                                   protected Logger::Loggable<Logger::Id::main> {
public:
  explicit AsyncFileManagerThreadPool(
      const envoy::extensions::common::async_files::v3::AsyncFileManagerConfig& config,
      Api::OsSysCalls& posix);
  ~AsyncFileManagerThreadPool() ABSL_LOCKS_EXCLUDED(queue_mutex_) override;
  CancelFunction
  createAnonymousFile(absl::string_view path,
                      std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) override;
  CancelFunction
  openExistingFile(absl::string_view filename, Mode mode,
                   std::function<void(absl::StatusOr<AsyncFileHandle>)> on_complete) override;
  CancelFunction unlink(absl::string_view filename,
                        std::function<void(absl::Status)> on_complete) override;
  std::string describe() const override;
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
  std::function<void()> enqueue(std::shared_ptr<AsyncFileAction> action)
      ABSL_LOCKS_EXCLUDED(queue_mutex_) override;
  void worker() ABSL_LOCKS_EXCLUDED(queue_mutex_);
  void resolveActions();

  absl::Mutex queue_mutex_;
  std::queue<std::shared_ptr<AsyncFileAction>> queue_ ABSL_GUARDED_BY(queue_mutex_);
  bool terminate_ ABSL_GUARDED_BY(queue_mutex_) = false;

  std::vector<std::thread> thread_pool_;
  Api::OsSysCalls& posix_;
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy

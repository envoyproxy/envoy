#pragma once

#include <memory>

#include "source/extensions/common/async_files/async_file_handle.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

class AsyncFileManager;

// The base implementation of an AsyncFileHandle that uses the manager thread pool - includes
// all the parts that interact with the manager's thread pool.
// See AsyncFileContextBasic for the implementation of the actions on top of this base.
class AsyncFileContextImpl : public AsyncFileContext {
public:
  void abort() ABSL_LOCKS_EXCLUDED(action_mutex_) override;

  void whenReady(std::function<void(absl::Status)> on_complete) override;

protected:
  // Queue up an action.
  // Will assert if an action is already queued for this handle.
  void enqueue(std::unique_ptr<AsyncFileAction> action) ABSL_LOCKS_EXCLUDED(action_mutex_);

  // Closes the file outside of the queue, if open. Used when aborting.
  virtual void abortClose() = 0;

  // Called by AsyncFileManager when the context reaches the front of the thread queue.
  // Performs the queued action; adding to the queue during any on_complete callback
  // will skip the thread queue and also perform the additional action in the current thread.
  void resolve() ABSL_LOCKS_EXCLUDED(action_mutex_);

  // Called by AsyncFileManager during enqueue. Returns true if the context needs to be
  // pushed onto the thread queue, false if it is already acting on the thread queue.
  // Asserts if it is not in a correct state to push an action.
  bool setAction(std::unique_ptr<AsyncFileAction> action) ABSL_LOCKS_EXCLUDED(action_mutex_);

  explicit AsyncFileContextImpl(AsyncFileManager* manager);

protected:
  AsyncFileManager* manager_;

private:
  absl::Mutex action_mutex_;
  std::unique_ptr<AsyncFileAction> next_action_ ABSL_GUARDED_BY(action_mutex_);
  bool aborting_ ABSL_GUARDED_BY(action_mutex_) = false;
  bool in_callback_ ABSL_GUARDED_BY(action_mutex_) = false;
  bool in_queue_ ABSL_GUARDED_BY(action_mutex_) = false;

  friend class AsyncFileActionImpl;
  friend class AsyncFileHandleTest;
  friend class AsyncFileManagerImpl;
  friend class AsyncFileManagerTest;
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy

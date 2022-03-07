#include "source/extensions/common/async_files/async_file_context_impl.h"

#include <memory>
#include <utility>

#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_manager.h"

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

namespace {
class ActionWhenReady : public AsyncFileAction {
public:
  explicit ActionWhenReady(std::function<void(absl::Status)> on_complete)
      : on_complete_(on_complete) {}

  void execute(AsyncFileContextImpl*) override {}

  void callback() override { on_complete_(absl::OkStatus()); }

private:
  const std::function<void(absl::Status)> on_complete_;
};
} // namespace

AsyncFileContextImpl::AsyncFileContextImpl(AsyncFileManager* manager) : manager_(manager) {}

void AsyncFileContextImpl::abort() {
  bool needs_queue;
  {
    absl::MutexLock lock(&action_mutex_);
    auto condition = [this]()
                         ABSL_EXCLUSIVE_LOCKS_REQUIRED(action_mutex_) { return !in_callback_; };
    aborting_ = true;
    action_mutex_.Await(absl::Condition(&condition));
    // We need the guarded value both inside and outside the locked scope, so we copy it into a
    // local.
    needs_queue = !in_queue_;
    if (needs_queue) {
      in_queue_ = true;
    }
  }
  if (needs_queue) {
    manager_->enqueue(std::static_pointer_cast<AsyncFileContextImpl>(shared_from_this()));
  }
}

void AsyncFileContextImpl::enqueue(std::unique_ptr<AsyncFileAction> action) {
  bool action_result = setAction(std::move(action));
  if (action_result) { // true means we need to queue it.
    manager_->enqueue(std::static_pointer_cast<AsyncFileContextImpl>(shared_from_this()));
  }
}

void AsyncFileContextImpl::resolve() {
  std::unique_ptr<AsyncFileAction> action;
  // Keep executing actions until there is no next_action_.
  while (true) {
    {
      absl::MutexLock lock(&action_mutex_);
      if (!next_action_ || aborting_) {
        in_queue_ = false;
        if (aborting_) {
          next_action_.reset();
          break;
        }
        return;
      }
      action = std::move(next_action_);
    }
    action->execute(this);
    {
      absl::MutexLock lock(&action_mutex_);
      if (aborting_) {
        next_action_.reset();
        break;
      }
      in_callback_ = true;
    }
    action->callback();
    {
      absl::MutexLock lock(&action_mutex_);
      in_callback_ = false;
    }
  }
  // We only get here when aborting - doing this outside the
  // loop means the lock has been released, which we want
  // whenever actions are being executed since they may not
  // complete promptly.
  abortClose();
}

void AsyncFileContextImpl::whenReady(std::function<void(absl::Status)> on_complete) {
  enqueue(std::make_unique<ActionWhenReady>(std::move(on_complete)));
}

bool AsyncFileContextImpl::setAction(std::unique_ptr<AsyncFileAction> action) {
  absl::MutexLock lock(&action_mutex_);
  // If you're triggering this assert, you're trying to chain actions like
  // context->createAnonymousFile(...);
  // context->write(...);
  //
  // Instead, you should be doing something like
  // context->createAnonymousFile(..., [this](absl::Status status) {
  //   if (status.ok()) {
  //     context->write(...);
  //   }
  // });
  ASSERT(!next_action_);
  next_action_ = std::move(action);
  if (in_queue_) {
    return false;
  }
  return in_queue_ = true;
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy

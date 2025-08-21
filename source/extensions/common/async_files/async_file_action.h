#pragma once

#include <atomic>
#include <functional>

#include "envoy/common/pure.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

// A CancelFunction attempts to stop an action in flight.
// * If the action already occurred, the CancelFunction does nothing.
// * If the action is already calling the callback, CancelFunction blocks until the callback
//   completes.
// * If the action is already executing, CancelFunction causes the removal of any resource-consuming
//   return value (e.g. file handles), and prevents the callback.
// * If the action is still just queued, CancelFunction prevents its execution.
using CancelFunction = absl::AnyInvocable<void()>;

// Actions to be passed to asyncFileManager->enqueue.
class AsyncFileAction {
public:
  virtual ~AsyncFileAction() = default;

  // Performs the action represented by this instance, and captures the
  // result.
  virtual void execute() PURE;

  // Calls the captured callback with the captured result.
  virtual void onComplete() PURE;

  // Performs any action to undo side-effects of the execution if the callback
  // has not yet been called (e.g. closing a file that was just opened, removing
  // a hard-link that was just created).
  // Not necessary for things that don't make persistent resources,
  // e.g. cancelling a write does not have to undo the write.
  virtual void onCancelledBeforeCallback() {}
  virtual bool hasActionIfCancelledBeforeCallback() const { return false; }
  virtual bool executesEvenIfCancelled() const { return false; }
};

// All concrete AsyncFileActions are a subclass of AsyncFileActionWithResult.
// The template allows for different on_complete callback signatures appropriate
// to each specific action.
//
// on_complete callbacks run in the AsyncFileManager's thread pool, and therefore:
// 1. Should avoid using variables that may be out of scope by the time the callback is called.
// 2. May need to lock-guard variables that can be changed in other threads.
// 3. Must not block significantly or do significant work - if anything time-consuming is required
// the result should be passed to another thread for handling.
template <typename T> class AsyncFileActionWithResult : public AsyncFileAction {
public:
  explicit AsyncFileActionWithResult(absl::AnyInvocable<void(T)> on_complete)
      : on_complete_(std::move(on_complete)) {}

  void execute() final { result_ = executeImpl(); }
  void onComplete() final { std::move(on_complete_)(std::move(result_.value())); }

protected:
  absl::optional<T> result_;
  // Implementation of the actual action.
  virtual T executeImpl() PURE;

private:
  absl::AnyInvocable<void(T)> on_complete_;
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy

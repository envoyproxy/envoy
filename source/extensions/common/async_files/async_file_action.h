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
using CancelFunction = std::function<void()>;

// Actions to be passed to asyncFileManager->enqueue.
class AsyncFileAction {
public:
  virtual ~AsyncFileAction() = default;

  // Cancel the action, as much as possible.
  //
  // If the action has not been started, it will become a no-op.
  //
  // If the action has started, onCancelledBeforeCallback will be called,
  // and the callback will not.
  //
  // If the callback is already being called, cancel will block until the
  // callback has completed.
  //
  // If the action is already complete, cancel does nothing.
  void cancel();

  // Performs the action represented by this instance, and calls the callback
  // on completion or on error.
  virtual void execute() PURE;

protected:
  enum class State { Queued, Executing, InCallback, Done, Cancelled };
  std::atomic<State> state_{State::Queued};
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
  explicit AsyncFileActionWithResult(std::function<void(T)> on_complete)
      : on_complete_(on_complete) {}

  void execute() final {
    State expected = State::Queued;
    if (!state_.compare_exchange_strong(expected, State::Executing)) {
      ASSERT(expected == State::Cancelled);
      return;
    }
    expected = State::Executing;
    T result = executeImpl();
    if (!state_.compare_exchange_strong(expected, State::InCallback)) {
      ASSERT(expected == State::Cancelled);
      onCancelledBeforeCallback(std::move(result));
      return;
    }
    on_complete_(std::move(result));
    state_.store(State::Done);
  }

protected:
  // Performs any action to undo side-effects of the execution if the callback
  // has not yet been called (e.g. closing a file that was just opened).
  // Not necessary for things that don't make persistent resources,
  // e.g. cancelling a write does not have to undo the write.
  virtual void onCancelledBeforeCallback(T){};

  // Implementation of the actual action.
  virtual T executeImpl() PURE;

private:
  std::function<void(T)> on_complete_;
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy

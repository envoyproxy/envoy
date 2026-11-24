#pragma once

#include <optional>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/schedulable_cb.h"

#include "source/common/coroutine/leaf_awaitable.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Reuses a single lazily-allocated Event::SchedulableCallback (which binds its callback once at
// creation and exposes no setCallback()) so each schedule*() call can invoke a different `fn`.
class ReusableSchedulableCallback {
public:
  explicit ReusableSchedulableCallback(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  void scheduleCurrentIteration(absl::AnyInvocable<void()> fn);
  void scheduleNextIteration(absl::AnyInvocable<void()> fn);
  void cancel();

private:
  Event::SchedulableCallback& ensureCallback();

  Event::Dispatcher& dispatcher_;
  absl::AnyInvocable<void()> fn_;
  Event::SchedulableCallbackPtr cb_;
};

// Parks the awaiting coroutine while FilterChainBridge::writePaused() is true; resumed
// asynchronously via `resume_cb` on the current iteration so resumption never interacts with the
// filter chain reentrantly on the watermark callback stack.
class FilterChainWritable : public Coroutine::LeafAwaitable<absl::Status> {
public:
  FilterChainWritable(FilterChainBridge& bridge, ReusableSchedulableCallback& resume_cb)
      : bridge_(bridge), resume_cb_(resume_cb) {}

protected:
  std::optional<absl::Status> tryImmediate() override;
  void onStart() override;
  void onCancel() override;

private:
  void arm();

  FilterChainBridge& bridge_;
  ReusableSchedulableCallback& resume_cb_;
};

// Consumes one chunk of the bridge's per-iteration inject budget immediately when available, or
// yields to the next event-loop iteration using `budget_cb` before refilling and consuming one
// unit.
class InjectionBudgetAvailable : public Coroutine::LeafAwaitable<absl::Status> {
public:
  InjectionBudgetAvailable(FilterChainBridge& bridge, ReusableSchedulableCallback& budget_cb)
      : bridge_(bridge), budget_cb_(budget_cb) {}

protected:
  std::optional<absl::Status> tryImmediate() override;
  void onStart() override;
  void onCancel() override;

private:
  FilterChainBridge& bridge_;
  ReusableSchedulableCallback& budget_cb_;
};

// Injects a single chunk into the filter chain via `bridge`, checking cancellation both before
// (via LeafAwaitable::await_ready()) and after the synchronous `injectData()` call so an on-stack
// local reply or stream reset that cancels the coroutine resolves with CancelledError rather than
// returning OkStatus().
class InjectChunk : public Coroutine::LeafAwaitable<absl::Status> {
public:
  InjectChunk(FilterChainBridge& bridge, Buffer::Instance& chunk)
      : bridge_(bridge), chunk_(chunk) {}

protected:
  std::optional<absl::Status> tryImmediate() override;
  void onStart() override {}
  void onCancel() override {}

private:
  FilterChainBridge& bridge_;
  Buffer::Instance& chunk_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

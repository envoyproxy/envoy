#include "source/extensions/filters/http/ai_protocol_manager/chain_writer.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/schedulable_cb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/status_macros.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

// Zero-suspension cancellation probe used after synchronous bridge_.injectData() calls so a
// re-entrant stream reset or local reply that cancelled the CoroutineContext unwinds immediately
// without touching ChainWriter or FilterChainBridge members.
class CheckCancelled : public Coroutine::LeafAwaitable<absl::Status> {
protected:
  std::optional<absl::Status> tryImmediate() override { return absl::OkStatus(); }
  void onStart() override {}
  void onCancel() override {}
};

// Consumes one chunk of the bridge's per-iteration inject budget immediately when available, or
// yields to the next event-loop iteration using `budget_cb` before refilling and consuming one
// unit.
class InjectionBudgetAvailable : public Coroutine::LeafAwaitable<absl::Status> {
public:
  InjectionBudgetAvailable(FilterChainBridge& bridge, ReusableSchedulableCallback& budget_cb)
      : bridge_(bridge), budget_cb_(budget_cb) {}

protected:
  std::optional<absl::Status> tryImmediate() override {
    if (bridge_.tryConsumeInjectBudget()) {
      return absl::OkStatus();
    }
    return std::nullopt;
  }

  void onStart() override {
    budget_cb_.scheduleNextIteration([this]() {
      bridge_.resetInjectBudget();
      const bool consumed = bridge_.tryConsumeInjectBudget();
      ASSERT(consumed);
      complete(absl::OkStatus());
    });
  }

  void onCancel() override { budget_cb_.cancel(); }

private:
  FilterChainBridge& bridge_;
  ReusableSchedulableCallback& budget_cb_;
};

// Parks the coroutine while FilterChainBridge::writePaused() is true; resumed asynchronously via
// `resume_cb` on the current iteration so resumption never injects reentrantly on the watermark
// callback stack.
class FilterChainWritable : public Coroutine::LeafAwaitable<absl::Status> {
public:
  FilterChainWritable(FilterChainBridge& bridge, ReusableSchedulableCallback& resume_cb)
      : bridge_(bridge), resume_cb_(resume_cb) {}

protected:
  std::optional<absl::Status> tryImmediate() override {
    if (!bridge_.writePaused()) {
      return absl::OkStatus();
    }
    return std::nullopt;
  }

  void onStart() override {
    ASSERT(bridge_.writePaused());
    arm();
  }

  void onCancel() override {
    bridge_.clearWriteResumeCallback();
    resume_cb_.cancel();
  }

private:
  void arm() {
    bridge_.setWriteResumeCallback([this]() {
      resume_cb_.scheduleCurrentIteration([this]() {
        if (bridge_.writePaused()) {
          arm();
          return;
        }
        complete(absl::OkStatus());
      });
    });
  }

  FilterChainBridge& bridge_;
  ReusableSchedulableCallback& resume_cb_;
};

} // namespace

Event::SchedulableCallback& ReusableSchedulableCallback::ensureCallback() {
  if (cb_ == nullptr) {
    cb_ = dispatcher_.createSchedulableCallback([this]() {
      absl::AnyInvocable<void()> fn = std::move(fn_);
      fn_ = nullptr;
      if (fn != nullptr) {
        fn();
      }
    });
  }
  return *cb_;
}

void ReusableSchedulableCallback::scheduleCurrentIteration(absl::AnyInvocable<void()> fn) {
  ASSERT(fn_ == nullptr);
  Event::SchedulableCallback& cb = ensureCallback();
  ASSERT(!cb.enabled());
  fn_ = std::move(fn);
  cb.scheduleCallbackCurrentIteration();
}

void ReusableSchedulableCallback::scheduleNextIteration(absl::AnyInvocable<void()> fn) {
  ASSERT(fn_ == nullptr);
  Event::SchedulableCallback& cb = ensureCallback();
  ASSERT(!cb.enabled());
  fn_ = std::move(fn);
  cb.scheduleCallbackNextIteration();
}

void ReusableSchedulableCallback::cancel() {
  fn_ = nullptr;
  if (cb_ != nullptr) {
    cb_->cancel();
  }
}

Coroutine::Task<absl::Status> ChainWriter::inject(Buffer::Instance& data) {
  if (data.length() == 0) {
    co_return absl::OkStatus();
  }

  Buffer::OwnedImpl owned;
  owned.move(data);

  // `inject()` can be entered synchronously from inside `decodeData()` / `encodeData()` (e.g. on an
  // in-memory payload or when `AsyncQueue::tryPush()` wakes a parked response sink), where calling
  // `inject*DataToFilterChain` on-stack trips `Http::FilterManager`'s `FilterCallState` assertion.
  CO_RETURN_IF_ERROR(co_await Coroutine::yield());

  while (owned.length() > 0) {
    CO_RETURN_IF_ERROR(co_await FilterChainWritable(bridge_, write_resume_cb_));
    CO_RETURN_IF_ERROR(co_await InjectionBudgetAvailable(bridge_, budget_cb_));

    const uint64_t to_inject = std::min(WriteChunkSize, static_cast<uint64_t>(owned.length()));
    Buffer::OwnedImpl chunk;
    chunk.move(owned, to_inject);
    bridge_.injectData(chunk);
    CO_RETURN_IF_ERROR(co_await CheckCancelled());
  }

  co_return absl::OkStatus();
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

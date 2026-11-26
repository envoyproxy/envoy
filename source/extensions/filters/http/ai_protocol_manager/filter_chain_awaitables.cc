#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_awaitables.h"

#include <optional>
#include <utility>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

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

std::optional<absl::Status> FilterChainWritable::tryImmediate() {
  if (!bridge_.writePaused()) {
    return absl::OkStatus();
  }
  return std::nullopt;
}

void FilterChainWritable::onStart() {
  ASSERT(bridge_.writePaused());
  arm();
}

void FilterChainWritable::onCancel() {
  bridge_.clearWriteResumeCallback();
  resume_cb_.cancel();
}

void FilterChainWritable::arm() {
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

std::optional<absl::Status> InjectionBudgetAvailable::tryImmediate() {
  if (bridge_.tryConsumeInjectBudget()) {
    return absl::OkStatus();
  }
  return std::nullopt;
}

void InjectionBudgetAvailable::onStart() {
  budget_cb_.scheduleNextIteration([this]() {
    bridge_.resetInjectBudget();
    const bool consumed = bridge_.tryConsumeInjectBudget();
    ASSERT(consumed);
    complete(absl::OkStatus());
  });
}

void InjectionBudgetAvailable::onCancel() { budget_cb_.cancel(); }

std::optional<absl::Status> InjectChunk::tryImmediate() {
  bridge_.injectData(chunk_);
  if (context().cancellation()->cancelled()) {
    return absl::CancelledError("coroutine cancelled");
  }
  return absl::OkStatus();
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

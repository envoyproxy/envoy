#include "source/common/event/evwatch_observer_manager_impl.h"

#include <algorithm>
#include <chrono>

namespace Envoy {
namespace Event {

namespace {

class IterationGuard {
public:
  explicit IterationGuard(uint32_t& depth) : depth_(depth) { ++depth_; }
  ~IterationGuard() { --depth_; }

private:
  uint32_t& depth_;
};

} // namespace

EvwatchObserverManagerImpl::EvwatchObserverManagerImpl(event_base& libevent,
                                                       TimeSource& time_source)
    : libevent_(libevent), time_source_(time_source) {}

EvwatchObserverManagerImpl::~EvwatchObserverManagerImpl() {
  if (prepare_evwatch_ != nullptr) {
    evwatch_free(prepare_evwatch_);
  }
  if (check_evwatch_ != nullptr) {
    evwatch_free(check_evwatch_);
  }
  IterationGuard guard(iteration_depth_);
  for (size_t i = 0; i < observers_.size(); ++i) {
    if (observers_[i].has_value()) {
      Evwatch::Observer& obs = observers_[i].value();
      observers_[i].reset();
      obs.onClose();
    }
  }
  observers_.clear();
}

void EvwatchObserverManagerImpl::onPrepare(evwatch*, const evwatch_prepare_cb_info* info,
                                           void* arg) {
  auto* self = static_cast<EvwatchObserverManagerImpl*>(arg);
  if (self->observers_.empty()) {
    return;
  }
  timeval timeout;
  const bool timeout_set = evwatch_prepare_get_timeout(info, &timeout);
  const std::optional<MonotonicTime::duration> timeout_duration =
      timeout_set ? std::make_optional(std::chrono::seconds(timeout.tv_sec) +
                                       std::chrono::microseconds(timeout.tv_usec))
                  : std::nullopt;

  const MonotonicTime prepare_time = self->time_source_.monotonicTime();

  {
    IterationGuard guard(self->iteration_depth_);
    const size_t size_at_start = self->observers_.size();
    for (size_t i = 0; i < size_at_start; ++i) {
      if (self->observers_[i].has_value()) {
        self->observers_[i]->onPrepare(prepare_time, timeout_duration);
      }
    }
  }
  self->cleanupNulledObservers();
}

void EvwatchObserverManagerImpl::onCheck(evwatch*, const evwatch_check_cb_info*, void* arg) {
  auto* self = static_cast<EvwatchObserverManagerImpl*>(arg);
  if (self->observers_.empty()) {
    return;
  }
  const MonotonicTime check_time = self->time_source_.monotonicTime();

  {
    IterationGuard guard(self->iteration_depth_);
    const size_t size_at_start = self->observers_.size();
    for (size_t i = 0; i < size_at_start; ++i) {
      if (self->observers_[i].has_value()) {
        self->observers_[i]->onCheck(check_time);
      }
    }
  }
  self->cleanupNulledObservers();
}

void EvwatchObserverManagerImpl::registerObserver(Evwatch::Observer& observer) {
  auto it = std::find_if(observers_.begin(), observers_.end(),
                         [&observer](const auto& entry) { return entry.ptr() == &observer; });
  if (it != observers_.end()) {
    return;
  }
  if (!hooks_registered_) {
    hooks_registered_ = true;
    prepare_evwatch_ = evwatch_prepare_new(&libevent_, &onPrepare, this);
    check_evwatch_ = evwatch_check_new(&libevent_, &onCheck, this);
  }
  observers_.push_back(observer);
}

void EvwatchObserverManagerImpl::unregisterObserver(Evwatch::Observer& observer) {
  auto it = std::find_if(observers_.begin(), observers_.end(),
                         [&observer](const auto& entry) { return entry.ptr() == &observer; });
  if (it != observers_.end()) {
    if (iteration_depth_ > 0) {
      it->reset();
      has_nulled_observers_ = true;
    } else {
      observers_.erase(it);
    }
    observer.onClose();
  }
}

void EvwatchObserverManagerImpl::cleanupNulledObservers() {
  if (!has_nulled_observers_ || iteration_depth_ > 0) {
    return;
  }
  observers_.erase(std::remove_if(observers_.begin(), observers_.end(),
                                  [](const auto& entry) { return !entry.has_value(); }),
                   observers_.end());
  has_nulled_observers_ = false;
}

} // namespace Event
} // namespace Envoy

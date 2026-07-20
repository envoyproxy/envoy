#include "source/common/event/evwatch_observer_manager_impl.h"

#include <algorithm>
#include <chrono>

namespace Envoy {
namespace Event {

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
  for (Evwatch::Observer& observer : observers_) {
    observer.onClose();
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

  for (Evwatch::Observer& observer : self->observers_) {
    observer.onPrepare(prepare_time, timeout_duration);
  }
}

void EvwatchObserverManagerImpl::onCheck(evwatch*, const evwatch_check_cb_info*, void* arg) {
  auto* self = static_cast<EvwatchObserverManagerImpl*>(arg);
  if (self->observers_.empty()) {
    return;
  }
  const MonotonicTime check_time = self->time_source_.monotonicTime();

  for (Evwatch::Observer& observer : self->observers_) {
    observer.onCheck(check_time);
  }
}

void EvwatchObserverManagerImpl::registerObserver(Evwatch::Observer& observer) {
  auto it = std::find_if(observers_.begin(), observers_.end(),
                         [&observer](const auto& entry) { return &entry.get() == &observer; });
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
                         [&observer](const auto& entry) { return &entry.get() == &observer; });
  if (it != observers_.end()) {
    observers_.erase(it);
    observer.onClose();
  }
}

} // namespace Event
} // namespace Envoy

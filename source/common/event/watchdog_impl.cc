
#include "common/common/assert.h"
#include "common/event/watchdog_impl.h"

#include "envoy/event/dispatcher.h"

namespace Event {

void WatchDogImpl::startWatchdog(Event::Dispatcher& dispatcher) {
  timer_ = dispatcher.createTimer([this]() -> void {
    this->touch();
    timer_->enableTimer(timer_interval_);
  });
  timer_->enableTimer(timer_interval_);
}

} // Event

#include "server/watchdog_impl.h"

#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Server {

void WatchDogImpl::startWatchdog(Event::Dispatcher& dispatcher) {
  timer_ = dispatcher.createTimer([this]() -> void {
    this->touch();
    timer_->enableTimer(timer_interval_);
  });
  timer_->enableTimer(timer_interval_);
}

} // namespace Server
} // namespace Envoy

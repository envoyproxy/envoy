#include "common/event/user_space_file_event_impl.h"

#include <cstdint>

#include "common/common/assert.h"

namespace Envoy {
namespace Event {

void DefaultEventListener::onEventEnabled(uint32_t enabled_events) {
  enabled_events_ = enabled_events;
  // Clear ephemeral events to align with FileEventImpl::setEnable().
  ephermal_events_ = 0;
}

void DefaultEventListener::onEventActivated(uint32_t activated_events) {
  // Event owner should not activate any event which is disabled.
  // Also see onEventEnabled which clears ephemeral events.
  // The overall prevents callback on disabled events.
  ASSERT((ephermal_events_ & ~enabled_events_) == 0);
  ephermal_events_ |= activated_events;
}

void UserSpaceFileEventImpl::activate(uint32_t events) {
  // Only supported event types are set.
  ASSERT((events & (FileReadyType::Read | FileReadyType::Write | FileReadyType::Closed)) == events);
  event_listener_.onEventActivated(events);
  if (!schedulable_.enabled()) {
    schedulable_.scheduleCallbackNextIteration();
  }
}

void UserSpaceFileEventImpl::setEnabled(uint32_t events) {
  // Only supported event types are set.
  ASSERT((events & (FileReadyType::Read | FileReadyType::Write | FileReadyType::Closed)) == events);
  event_listener_.onEventEnabled(events);
  bool was_enabled = schedulable_.enabled();
  if (!was_enabled) {
    schedulable_.scheduleCallbackNextIteration();
  }
  ENVOY_LOG(trace, "User space file event {} set events {}. Will {} reschedule.",
            static_cast<void*>(this), events, was_enabled ? "not " : "");
}

UserSpaceFileEventImpl::UserSpaceFileEventImpl(Event::FileReadyCb cb, uint32_t events,
                                               SchedulableCallback& schedulable_cb)
    : schedulable_(schedulable_cb), cb_([this, cb]() {
        auto all_events = getEventListener().triggeredEvents();
        auto ephemeral_events = getEventListener().getAndClearEphemeralEvents();
        ENVOY_LOG(trace,
                  "User space event {} invokes callbacks on allevents = {}, ephermal events = {}",
                  static_cast<void*>(this), all_events, ephemeral_events);
        cb(all_events | ephemeral_events);
      }) {
  event_listener_.onEventEnabled(events);
}

std::unique_ptr<UserSpaceFileEventImpl> UserSpaceFileEventFactory::createUserSpaceFileEventImpl(
    Event::Dispatcher&, Event::FileReadyCb cb, Event::FileTriggerType trigger_type, uint32_t events,
    SchedulableCallback& scheduable_cb) {
  ASSERT(trigger_type == Event::FileTriggerType::Edge);
  return std::unique_ptr<UserSpaceFileEventImpl>(
      new UserSpaceFileEventImpl(cb, events, scheduable_cb));
}

} // namespace Event
} // namespace Envoy
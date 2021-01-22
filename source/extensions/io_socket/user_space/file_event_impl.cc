#include "extensions/io_socket/user_space/file_event_impl.h"

#include <cstdint>

#include "common/common/assert.h"

#include "extensions/io_socket/user_space/io_handle.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace UserSpace {

FileEventImpl::FileEventImpl(Event::Dispatcher& dispatcher, Event::FileReadyCb cb, uint32_t events,
                             IoHandle& io_source)
    : schedulable_(dispatcher.createSchedulableCallback([this, cb]() {
        auto ephemeral_events = event_listener_.getAndClearEphemeralEvents();
        ENVOY_LOG(trace, "User space event {} invokes callbacks on events = {}",
                  static_cast<void*>(this), ephemeral_events);
        cb(ephemeral_events);
      })),
      io_source_(io_source) {
  setEnabled(events);
}

void FileEventImpl::EventListener::clearEphemeralEvents() {
  // Clear ephemeral events to align with FileEventImpl::setEnabled().
  ephemeral_events_ = 0;
}

void FileEventImpl::EventListener::onEventActivated(uint32_t activated_events) {
  ephemeral_events_ |= activated_events;
}

void FileEventImpl::EventListener::setEnabledEvents(uint32_t enabled_events) {
  enabled_events_ = enabled_events;
}

void FileEventImpl::activate(uint32_t events) {
  // Only supported event types are set.
  ASSERT((events & (Event::FileReadyType::Read | Event::FileReadyType::Write |
                    Event::FileReadyType::Closed)) == events);
  event_listener_.onEventActivated(events);
  schedulable_->scheduleCallbackNextIteration();
}

void FileEventImpl::setEnabled(uint32_t events) {
  // Only supported event types are set.
  ASSERT((events & (Event::FileReadyType::Read | Event::FileReadyType::Write |
                    Event::FileReadyType::Closed)) == events);
  event_listener_.clearEphemeralEvents();
  event_listener_.setEnabledEvents(events);
  bool was_enabled = schedulable_->enabled();
  // Recalculate activated events.
  uint32_t events_to_notify = 0;
  if ((events & Event::FileReadyType::Read) && io_source_.isReadable()) {
    events_to_notify |= Event::FileReadyType::Read;
  }
  if ((events & Event::FileReadyType::Write) && io_source_.isPeerWritable()) {
    events_to_notify |= Event::FileReadyType::Write;
  }
  if ((events & Event::FileReadyType::Closed) && io_source_.isPeerShutDownWrite()) {
    events_to_notify |= Event::FileReadyType::Closed;
  }
  if (events_to_notify != 0) {
    activate(events_to_notify);
  } else {
    schedulable_->cancel();
  }
  ENVOY_LOG(
      trace,
      "User space file event {} set enabled events {} and events {} is active. Will {} reschedule.",
      static_cast<void*>(this), events, was_enabled ? "not " : "");
}

void FileEventImpl::activateIfEnabled(uint32_t events) {
  ASSERT((events & (Event::FileReadyType::Read | Event::FileReadyType::Write |
                    Event::FileReadyType::Closed)) == events);
  // filtered out disabled events.
  uint32_t filter_enabled = events & event_listener_.getEnabledEvents();
  if (filter_enabled == 0) {
    return;
  }
  activate(filter_enabled);
}
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
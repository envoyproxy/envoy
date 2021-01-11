#include "extensions/io_socket/buffered_io_socket/user_space_file_event_impl.h"

#include <cstdint>

#include "common/common/assert.h"

#include "extensions/io_socket/buffered_io_socket/peer_buffer.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace BufferedIoSocket {

UserSpaceFileEventImpl::UserSpaceFileEventImpl(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                                               uint32_t events, UserspaceIoHandle& io_source)
    : schedulable_(dispatcher.createSchedulableCallback([this, cb]() {
        auto ephemeral_events = event_listener_.getAndClearEphemeralEvents();
        ENVOY_LOG(trace, "User space event {} invokes callbacks on events = {}",
                  static_cast<void*>(this), ephemeral_events);
        cb(ephemeral_events);
      })),
      io_source_(io_source) {
  setEnabled(events);
}

void EventListenerImpl::clearEphemeralEvents() {
  // Clear ephemeral events to align with FileEventImpl::setEnable().
  ephemeral_events_ = 0;
}

void EventListenerImpl::onEventActivated(uint32_t activated_events) {
  ephemeral_events_ |= activated_events;
}

void UserSpaceFileEventImpl::activate(uint32_t events) {
  // Only supported event types are set.
  ASSERT((events & (Event::FileReadyType::Read | Event::FileReadyType::Write |
                    Event::FileReadyType::Closed)) == events);
  event_listener_.onEventActivated(events);
  schedulable_->scheduleCallbackNextIteration();
}

void UserSpaceFileEventImpl::setEnabled(uint32_t events) {
  // Only supported event types are set.
  ASSERT((events & (Event::FileReadyType::Read | Event::FileReadyType::Write |
                    Event::FileReadyType::Closed)) == events);
  event_listener_.clearEphemeralEvents();
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
} // namespace BufferedIoSocket
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
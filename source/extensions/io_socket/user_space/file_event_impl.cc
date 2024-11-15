#include "source/extensions/io_socket/user_space/file_event_impl.h"

#include "source/common/common/assert.h"
#include "source/extensions/io_socket/user_space/io_handle.h"

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
        THROW_IF_NOT_OK(cb(ephemeral_events));
      })),
      io_source_(io_source) {
  setEnabled(events);
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
  // Align with Event::FileEventImpl. Clear pending events on updates to the fd event mask to avoid
  // delivering events that are no longer relevant.
  event_listener_.clearEphemeralEvents();
  event_listener_.setEnabledEvents(events);
  bool was_enabled = schedulable_->enabled();
  // Recalculate activated events.
  uint32_t events_to_notify = 0;
  if ((events & Event::FileReadyType::Read) && (io_source_.isReadable() ||
                                                // Notify Read event when end-of-stream is received.
                                                io_source_.isPeerShutDownWrite())) {
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
      "User space file event {} set enabled events {} and events {} is active. Will {}reschedule.",
      static_cast<void*>(this), events, events_to_notify, was_enabled ? "not " : "");
}

void FileEventImpl::activateIfEnabled(uint32_t events) {
  ASSERT((events & (Event::FileReadyType::Read | Event::FileReadyType::Write |
                    Event::FileReadyType::Closed)) == events);
  // Filter out disabled events.
  uint32_t filtered_events = events & event_listener_.getEnabledEvents();
  if (filtered_events == 0) {
    return;
  }
  activate(filtered_events);
}
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy

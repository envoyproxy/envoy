#include "common/event/file_event_impl.h"

#include <cstdint>

#include "common/common/assert.h"
#include "common/event/dispatcher_impl.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

FileEventImpl::FileEventImpl(DispatcherImpl& dispatcher, os_fd_t fd, FileReadyCb cb,
                             FileTriggerType trigger, uint32_t events)
    : cb_(cb), fd_(fd), trigger_(trigger) {
#ifdef WIN32
  RELEASE_ASSERT(trigger_ == FileTriggerType::Level,
                 "libevent does not support edge triggers on Windows");
#endif
  assignEvents(events, &dispatcher.base());
  event_add(&raw_event_, nullptr);
}

void FileEventImpl::activate(uint32_t events) {
  int libevent_events = 0;
  if (events & FileReadyType::Read) {
    libevent_events |= EV_READ;
  }

  if (events & FileReadyType::Write) {
    libevent_events |= EV_WRITE;
  }

  if (events & FileReadyType::Closed) {
    libevent_events |= EV_CLOSED;
  }

  ASSERT(libevent_events);
  event_active(&raw_event_, libevent_events, 0);
}

void FileEventImpl::assignEvents(uint32_t events, event_base* base) {
  ASSERT(base != nullptr);
  event_assign(
      &raw_event_, base, fd_,
      EV_PERSIST | (trigger_ == FileTriggerType::Level ? 0 : EV_ET) |
          (events & FileReadyType::Read ? EV_READ : 0) |
          (events & FileReadyType::Write ? EV_WRITE : 0) |
          (events & FileReadyType::Closed ? EV_CLOSED : 0),
      [](evutil_socket_t, short what, void* arg) -> void {
        auto* event = static_cast<FileEventImpl*>(arg);
        uint32_t events = 0;
        if (what & EV_READ) {
          events |= FileReadyType::Read;
        }

        if (what & EV_WRITE) {
          events |= FileReadyType::Write;
        }

        if (what & EV_CLOSED) {
          events |= FileReadyType::Closed;
        }

        ASSERT(events);
        event->cb_(events);
      },
      this);
}

void FileEventImpl::setEnabled(uint32_t events) {
  auto* base = event_get_base(&raw_event_);
  event_del(&raw_event_);
  assignEvents(events, base);
  event_add(&raw_event_, nullptr);
}

} // namespace Event
} // namespace Envoy

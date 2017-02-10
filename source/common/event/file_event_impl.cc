#include "dispatcher_impl.h"
#include "file_event_impl.h"

#include "event2/event.h"

#include "common/common/assert.h"

namespace Event {

FileEventImpl::FileEventImpl(DispatcherImpl& dispatcher, int fd, FileReadyCb cb,
                             FileTriggerType trigger)
    : cb_(cb), base_(&dispatcher.base()), fd_(fd), trigger_(trigger) {
  assignEvents(FileReadyType::Read | FileReadyType::Write);
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

  ASSERT(libevent_events);
  event_active(&raw_event_, libevent_events, 0);
}

void FileEventImpl::assignEvents(uint32_t events) {
  event_assign(&raw_event_, base_, fd_,
               EV_PERSIST | (trigger_ == FileTriggerType::Level ? 0 : EV_ET) |
                   (events & FileReadyType::Read ? EV_READ : 0) |
                   (events & FileReadyType::Write ? EV_WRITE : 0),
               [](evutil_socket_t, short what, void* arg) -> void {
                 FileEventImpl* event = static_cast<FileEventImpl*>(arg);
                 uint32_t events = 0;
                 if (what & EV_READ) {
                   events |= FileReadyType::Read;
                 }

                 if (what & EV_WRITE) {
                   events |= FileReadyType::Write;
                 }

                 ASSERT(events);
                 event->cb_(events);
               },
               this);
}

void FileEventImpl::setEnabled(uint32_t events) {
  event_del(&raw_event_);
  assignEvents(events);
  event_add(&raw_event_, nullptr);
}

} // Event

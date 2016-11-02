#include "dispatcher_impl.h"
#include "file_event_impl.h"

#include "event2/event.h"

#include "common/common/assert.h"

namespace Event {

FileEventImpl::FileEventImpl(DispatcherImpl& dispatcher, int fd, FileReadyCb cb) : cb_(cb) {
  event_assign(&raw_event_, &dispatcher.base(), fd, EV_PERSIST | EV_ET | EV_READ | EV_WRITE,
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

} // Event

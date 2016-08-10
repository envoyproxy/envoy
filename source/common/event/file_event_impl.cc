#include "dispatcher_impl.h"
#include "file_event_impl.h"

#include "event2/event.h"

namespace Event {

FileEventImpl::FileEventImpl(DispatcherImpl& dispatcher, int fd, FileReadyCb read_cb,
                             FileReadyCb write_cb)
    : read_cb_(read_cb), write_cb_(write_cb) {
  short what = EV_PERSIST | EV_ET;
  if (read_cb) {
    what |= EV_READ;
  }

  if (write_cb) {
    what |= EV_WRITE;
  }

  event_assign(&raw_event_, &dispatcher.base(), fd,
               what, [](evutil_socket_t, short what, void* arg) -> void {
                 FileEventImpl* event = static_cast<FileEventImpl*>(arg);
                 if (what & EV_READ) {
                   event->read_cb_();
                 }

                 if (what & EV_WRITE) {
                   event->write_cb_();
                 }
               }, this);

  event_add(&raw_event_, nullptr);
}

} // Event

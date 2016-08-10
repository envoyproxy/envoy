#pragma once

#include "event_impl_base.h"

#include "envoy/event/file_event.h"

namespace Event {

/**
 * Implementation of FileEvent for libevent that uses edge triggered persistent events and assumes
 * the user will read/write until EAGAIN is returned from the file.
 */
class FileEventImpl : public FileEvent, ImplBase {
public:
  FileEventImpl(DispatcherImpl& dispatcher, int fd, FileReadyCb read_cb, FileReadyCb write_cb);

private:
  FileReadyCb read_cb_;
  FileReadyCb write_cb_;
};

} // Event

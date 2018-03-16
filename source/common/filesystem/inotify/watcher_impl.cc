#include <sys/inotify.h>

#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/filesystem/watcher_impl.h"

namespace Envoy {
namespace Filesystem {

WatcherImpl::WatcherImpl(Event::Dispatcher& dispatcher)
    : inotify_fd_(inotify_init1(IN_NONBLOCK)),
      inotify_event_(dispatcher.createFileEvent(inotify_fd_,
                                                [this](uint32_t events) -> void {
                                                  ASSERT(events == Event::FileReadyType::Read);
                                                  UNREFERENCED_PARAMETER(events);
                                                  onInotifyEvent();
                                                },
                                                Event::FileTriggerType::Edge,
                                                Event::FileReadyType::Read)) {}

WatcherImpl::~WatcherImpl() { close(inotify_fd_); }

void WatcherImpl::addWatch(const std::string& path, uint32_t events, OnChangedCb callback) {
  // Because of general inotify pain, we always watch the directory that the file lives in,
  // and then synthetically raise per file events.
  size_t last_slash = path.rfind('/');
  if (last_slash == std::string::npos) {
    throw EnvoyException(fmt::format("invalid watch path {}", path));
  }

  std::string directory = last_slash != 0 ? path.substr(0, last_slash) : "/";
  std::string file = StringUtil::subspan(path, last_slash + 1, path.size());

  int watch_fd = inotify_add_watch(inotify_fd_, directory.c_str(), IN_ALL_EVENTS);
  if (watch_fd == -1) {
    throw EnvoyException(
        fmt::format("unable to add filesystem watch for file {}: {}", path, strerror(errno)));
  }

  ENVOY_LOG(debug, "added watch for directory: '{}' file: '{}' fd: {}", directory, file, watch_fd);
  callback_map_[watch_fd].watches_.push_back({file, events, callback});
}

void WatcherImpl::onInotifyEvent() {
  while (true) {
    uint8_t buffer[sizeof(inotify_event) + NAME_MAX + 1];
    ssize_t rc = read(inotify_fd_, &buffer, sizeof(buffer));
    if (rc == -1 && errno == EAGAIN) {
      return;
    }
    RELEASE_ASSERT(rc >= 0);

    const size_t event_count = rc;
    size_t index = 0;
    while (index < event_count) {
      inotify_event* file_event = reinterpret_cast<inotify_event*>(&buffer[index]);
      ASSERT(callback_map_.count(file_event->wd) == 1);

      std::string file;
      if (file_event->len > 0) {
        file.assign(file_event->name);
      }

      ENVOY_LOG(debug, "notification: fd: {} mask: {:x} file: {}", file_event->wd, file_event->mask,
                file);

      uint32_t events = 0;
      if (file_event->mask & IN_MOVED_TO) {
        events |= Events::MovedTo;
      }

      for (FileWatch& watch : callback_map_[file_event->wd].watches_) {
        if (watch.file_ == file && (watch.events_ & events)) {
          ENVOY_LOG(debug, "matched callback: file: {}", file);
          watch.cb_(events);
        }
      }

      index += sizeof(inotify_event) + file_event->len;
    }
  }
}

} // namespace Filesystem
} // namespace Envoy

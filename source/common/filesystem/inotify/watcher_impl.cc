#include <sys/inotify.h>

#include <cstdint>
#include <string>

#include "envoy/api/api.h"
#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/filesystem/watcher_impl.h"

namespace Envoy {
namespace Filesystem {

WatcherImpl::WatcherImpl(Event::Dispatcher& dispatcher, Filesystem::Instance& file_system)
    : file_system_(file_system) {
  inotify_fd_ = inotify_init1(IN_NONBLOCK);
  RELEASE_ASSERT(inotify_fd_ >= 0,
                 "Consider increasing value of user.max_inotify_watches via sysctl");
  inotify_event_ = dispatcher.createFileEvent(
      inotify_fd_,
      [this](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Read);
        return onInotifyEvent();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
}

WatcherImpl::~WatcherImpl() { close(inotify_fd_); }

absl::Status WatcherImpl::addWatch(absl::string_view path, uint32_t events, OnChangedCb callback) {
  // Because of general inotify pain, we always watch the directory that the file lives in,
  // and then synthetically raise per file events.
  auto result_or_error = file_system_.splitPathFromFilename(path);
  RETURN_IF_NOT_OK_REF(result_or_error.status());
  const PathSplitResult result = result_or_error.value();

  const uint32_t watch_mask = IN_MODIFY | IN_MOVED_TO;
  int watch_fd = inotify_add_watch(inotify_fd_, std::string(result.directory_).c_str(), watch_mask);
  if (watch_fd == -1) {
    return absl::InvalidArgumentError(
        fmt::format("unable to add filesystem watch for file {}: {}", path, errorDetails(errno)));
  }

  ENVOY_LOG(debug, "added watch for directory: '{}' file: '{}' fd: {}", result.directory_,
            result.file_, watch_fd);
  callback_map_[watch_fd].watches_.push_back({std::string(result.file_), events, callback});
  return absl::OkStatus();
}

absl::Status WatcherImpl::onInotifyEvent() {
  while (true) {
    // The buffer needs to be suitably aligned to store the first inotify_event structure.
    // If there are multiple events returned by the read call, the kernel is responsible for
    // properly aligning subsequent inotify_event structures (per `man inotify`).
    alignas(inotify_event) uint8_t buffer[sizeof(inotify_event) + NAME_MAX + 1];
    ssize_t rc = read(inotify_fd_, &buffer, sizeof(buffer));
    if (rc == -1 && errno == EAGAIN) {
      return absl::OkStatus();
    }
    RELEASE_ASSERT(rc >= 0, "");

    const size_t event_count = rc;
    size_t index = 0;
    while (index < event_count) {
      auto* file_event = reinterpret_cast<inotify_event*>(&buffer[index]);
      ASSERT(callback_map_.count(file_event->wd) == 1);

      std::string file;
      if (file_event->len > 0) {
        file.assign(file_event->name);
      }

      ENVOY_LOG(debug, "notification: fd: {} mask: {:x} file: {}", file_event->wd, file_event->mask,
                file);

      uint32_t events = 0;
      if (file_event->mask & IN_MODIFY) {
        events |= Events::Modified;
      }
      if (file_event->mask & IN_MOVED_TO) {
        events |= Events::MovedTo;
      }

      for (FileWatch& watch : callback_map_[file_event->wd].watches_) {
        if (watch.events_ & events) {
          if (watch.file_ == file) {
            ENVOY_LOG(debug, "matched callback: file: {}", file);
            RETURN_IF_NOT_OK(watch.cb_(events));
          } else if (watch.file_.empty()) {
            ENVOY_LOG(debug, "matched callback: directory: {}", file);
            RETURN_IF_NOT_OK(watch.cb_(events));
          }
        }
      }

      index += sizeof(inotify_event) + file_event->len;
    }
  }
  return absl::OkStatus();
}

} // namespace Filesystem
} // namespace Envoy

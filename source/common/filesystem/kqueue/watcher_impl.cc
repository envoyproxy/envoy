#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/types.h>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/filesystem/watcher_impl.h"

#include "event2/event.h"

namespace Envoy {
namespace Filesystem {

WatcherImpl::WatcherImpl(Event::Dispatcher& dispatcher, Filesystem::Instance& file_system)
    : file_system_(file_system), queue_(kqueue()),
      kqueue_event_(dispatcher.createFileEvent(
          queue_,
          [this](uint32_t events) {
            if (events & Event::FileReadyType::Read) {
              return onKqueueEvent();
            }
            return absl::OkStatus();
          },
          Event::FileTriggerType::Edge, Event::FileReadyType::Read)) {}

WatcherImpl::~WatcherImpl() {
  close(queue_);
  watches_.clear();
}

absl::Status WatcherImpl::addWatch(absl::string_view path, uint32_t events,
                                   Watcher::OnChangedCb cb) {
  absl::StatusOr<FileWatchPtr> watch_or_error = addWatch(path, events, cb, false);
  RETURN_IF_NOT_OK_REF(watch_or_error.status());
  if (watch_or_error.value() == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat("invalid watch path ", path));
  }
  return absl::OkStatus();
}

absl::StatusOr<WatcherImpl::FileWatchPtr> WatcherImpl::addWatch(absl::string_view path,
                                                                uint32_t events,
                                                                Watcher::OnChangedCb cb,
                                                                bool path_must_exist) {
  bool watching_dir = false;
  std::string pathname(path);
  int watch_fd = open(pathname.c_str(), O_SYMLINK);
  if (watch_fd == -1) {
    if (path_must_exist) {
      return nullptr;
    }

    const auto result_or_error = file_system_.splitPathFromFilename(path);
    RETURN_IF_NOT_OK_REF(result_or_error.status());
    watch_fd = open(std::string(result_or_error.value().directory_).c_str(), 0);
    if (watch_fd == -1) {
      return nullptr;
    }

    watching_dir = true;
  }

  FileWatchPtr watch(new FileWatch());
  watch->fd_ = watch_fd;
  watch->file_ = pathname;
  watch->events_ = events;
  watch->callback_ = cb;
  watch->watching_dir_ = watching_dir;

  u_int flags = NOTE_DELETE | NOTE_RENAME | NOTE_WRITE;

  struct kevent event;
  EV_SET(&event, watch_fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, flags, 0,
         reinterpret_cast<void*>(watch_fd));

  if (kevent(queue_, &event, 1, nullptr, 0, nullptr) == -1 || event.flags & EV_ERROR) {
    return absl::InvalidArgumentError(
        fmt::format("unable to add filesystem watch for file {}: {}", path, errorDetails(errno)));
  }

  ENVOY_LOG(debug, "added watch for file: '{}' fd: {}", path, watch_fd);

  watches_[watch_fd] = watch;

  return watch;
}

void WatcherImpl::removeWatch(FileWatchPtr& watch) {
  // Removing the map entry closes the fd, which will automatically
  // unregister the kqueue event.
  int fd = watch->fd_;
  watches_.erase(fd);
}

absl::Status WatcherImpl::onKqueueEvent() {
  struct kevent event = {};
  timespec nullts = {0, 0};

  while (true) {
    uint32_t events = 0;
    int nevents = kevent(queue_, nullptr, 0, &event, 1, &nullts);
    if (nevents < 1 || event.udata == nullptr) {
      return absl::OkStatus();
    }

    int watch_fd = reinterpret_cast<std::intptr_t>(event.udata);

    FileWatchPtr file = watches_[watch_fd];
    ASSERT(file != nullptr);
    ASSERT(watch_fd == file->fd_);

    absl::StatusOr<PathSplitResult> pathname_or_error =
        file_system_.splitPathFromFilename(file->file_);
    RETURN_IF_NOT_OK_REF(pathname_or_error.status());
    PathSplitResult& pathname = pathname_or_error.value();

    if (file->watching_dir_) {
      if (event.fflags & NOTE_DELETE) {
        // directory was deleted
        removeWatch(file);
        return absl::OkStatus();
      }

      if (event.fflags & NOTE_WRITE) {
        // directory was written -- check if the file we're actually watching appeared
        auto file_or_error = addWatch(file->file_, file->events_, file->callback_, true);
        RETURN_IF_NOT_OK_REF(file_or_error.status());
        FileWatchPtr new_file = file_or_error.value();
        if (new_file != nullptr) {
          removeWatch(file);
          file = new_file;

          events |= Events::MovedTo;
        }
      }
    } else if (pathname.file_.empty()) {
      if (event.fflags & NOTE_WRITE) {
        events |= Events::MovedTo;
      }
    } else {
      // kqueue doesn't seem to work well with NOTE_RENAME and O_SYMLINK, so instead if we
      // get a NOTE_DELETE on the symlink we check if there is another file with the same
      // name we assume a NOTE_RENAME and re-attach another event to the new file.
      if (event.fflags & NOTE_DELETE) {
        removeWatch(file);

        auto file_or_error = addWatch(file->file_, file->events_, file->callback_, true);
        RETURN_IF_NOT_OK_REF(file_or_error.status());
        FileWatchPtr new_file = file_or_error.value();
        if (new_file == nullptr) {
          return absl::OkStatus();
        }

        event.fflags |= NOTE_RENAME;
        file = new_file;
      }

      if (event.fflags & NOTE_RENAME) {
        events |= Events::MovedTo;
      }
      if (event.fflags & NOTE_WRITE) {
        events |= Events::Modified;
      }
    }

    ENVOY_LOG(debug, "notification: fd: {} flags: {:x} file: {}", file->fd_, event.fflags,
              file->file_);

    if (events & file->events_) {
      ENVOY_LOG(debug, "matched callback: file: {}", file->file_);
      RETURN_IF_NOT_OK(file->callback_(events));
    }
  }
  return absl::OkStatus();
}

} // namespace Filesystem
} // namespace Envoy

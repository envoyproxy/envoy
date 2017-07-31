#include "common/filesystem/watcher_impl.h"

#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/types.h>

#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"

#include "common/common/assert.h"
#include "common/common/utility.h"

#include "event2/event.h"

namespace Envoy {
namespace Filesystem {

WatcherImpl::WatcherImpl(Event::Dispatcher& dispatcher)
    : queue_(kqueue()),
      kqueue_event_(dispatcher.createFileEvent(queue_,
                                               [this](uint32_t events) -> void {
                                                 if (events & Event::FileReadyType::Read) {
                                                   onKqueueEvent();
                                                 }
                                               },
                                               Event::FileTriggerType::Edge,
                                               Event::FileReadyType::Read)) {}

WatcherImpl::~WatcherImpl() {
  close(queue_);

  for (const auto& entry : watches_) {
    close(entry.first);
  }
  watches_.clear();
}

void WatcherImpl::addWatch(const std::string& path, uint32_t events, Watcher::OnChangedCb cb) {
  FileWatchPtr watch = addWatch_(path, events, cb, false);
  if (watch == nullptr) {
    throw EnvoyException(fmt::format("invalid watch path {}", path));
  }
}

WatcherImpl::FileWatchPtr WatcherImpl::addWatch_(const std::string& path, uint32_t events,
                                                 Watcher::OnChangedCb cb, bool pathMustExist) {
  bool watchingDir = false;
  int watch_fd = open(path.c_str(), O_SYMLINK);
  if (watch_fd == -1) {
    if (pathMustExist) {
      return nullptr;
    }

    size_t last_slash = path.rfind('/');
    if (last_slash == std::string::npos) {
      throw EnvoyException(fmt::format("invalid watch path {}", path));
    }

    std::string directory = path.substr(0, last_slash);
    watch_fd = open(directory.c_str(), 0);
    if (watch_fd == -1) {
      return nullptr;
    }

    watchingDir = true;
  }

  FileWatchPtr watch(new FileWatch());
  watch->fd_ = watch_fd;
  watch->file_ = path;
  watch->events_ = events;
  watch->callback_ = cb;
  watch->watchingDir_ = watchingDir;

  int flags = NOTE_DELETE | NOTE_RENAME;
  if (watchingDir) {
    flags = NOTE_DELETE | NOTE_WRITE;
  }

  struct kevent event;
  EV_SET(&event, watch_fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, flags, 0,
         reinterpret_cast<void*>(watch_fd));

  if (kevent(queue_, &event, 1, NULL, 0, NULL) == -1) {
    throw EnvoyException(
        fmt::format("unable to add filesystem watch for file {}: {}", path, strerror(errno)));
  }

  if (event.flags & EV_ERROR) {
    throw EnvoyException(
        fmt::format("unable to add filesystem watch for file {}: {}", path, strerror(event.data)));
  }

  log_debug("added watch for file: '{}' fd: {}", path, watch_fd);

  watches_[watch_fd] = watch;

  return watch;
}

void WatcherImpl::removeWatch_(FileWatchPtr& watch) {
  int fd = watch->fd_;
  close(fd);
  watches_.erase(fd);
}

void WatcherImpl::onKqueueEvent() {
  struct kevent event = {};
  timespec nullts = {0, 0};

  while (true) {
    uint32_t events = 0;
    int nevents = kevent(queue_, NULL, 0, &event, 1, &nullts);
    if (nevents < 1 || event.udata == nullptr) {
      return;
    }

    int watch_fd = reinterpret_cast<std::intptr_t>(event.udata);

    FileWatchPtr file = watches_[watch_fd];
    if (file == nullptr) {
      continue;
    }
    ASSERT(watch_fd == file->fd_);

    if (file->watchingDir_) {
      if (event.fflags & NOTE_DELETE) {
        // directory was deleted
        removeWatch_(file);
        return;
      }

      if (event.fflags & NOTE_WRITE) {
        // directory was written -- check if the file we're actually watching appeared
        FileWatchPtr newFile = addWatch_(file->file_, file->events_, file->callback_, true);
        if (newFile != nullptr) {
          removeWatch_(file);
          file = newFile;

          events |= Events::MovedTo;
        }
      }
    } else {
      // kqueue doesn't seem to work well with NOTE_RENAME and O_SYMLINK, so instead if we
      // get a NOTE_DELETE on the symlink we check if there is another file with the same
      // name we assume a NOTE_RENAME and re-attach another event to the new file.
      if (event.fflags & NOTE_DELETE) {
        removeWatch_(file);

        FileWatchPtr newFile = addWatch_(file->file_, file->events_, file->callback_, true);
        if (newFile == nullptr) {
          return;
        }

        event.fflags |= NOTE_RENAME;
        file = newFile;
      }

      if (event.fflags & NOTE_RENAME) {
        events |= Events::MovedTo;
      }
    }

    if (events & file->events_) {
      log_debug("matched callback: file: {}", file->file_);
      file->callback_(events);
    }

    log_debug("notification: fd: {} flags: {:x} file: {}", file->fd_, event.fflags, file->file_);
  }
}

} // namespace Filesystem
} // namespace Envoy

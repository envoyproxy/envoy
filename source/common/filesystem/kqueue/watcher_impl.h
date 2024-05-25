#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/watcher.h"

#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Filesystem {

/**
 * Implementation of Watcher that uses kqueue. If the file being watched doesn't exist, we watch
 * the directory, and then try to add a file watch each time there's a write event to the
 * directory.
 */
class WatcherImpl : public Watcher, Logger::Loggable<Logger::Id::file> {
public:
  WatcherImpl(Event::Dispatcher& dispatcher, Filesystem::Instance& file_system);
  ~WatcherImpl();

  // Filesystem::Watcher
  absl::Status addWatch(absl::string_view path, uint32_t events, OnChangedCb cb) override;

private:
  struct FileWatch : LinkedObject<FileWatch> {
    ~FileWatch() { close(fd_); }

    int fd_;
    uint32_t events_;
    std::string file_;
    OnChangedCb callback_;
    bool watching_dir_;
  };

  using FileWatchPtr = std::shared_ptr<FileWatch>;

  absl::Status onKqueueEvent();
  absl::StatusOr<FileWatchPtr> addWatch(absl::string_view path, uint32_t events,
                                        Watcher::OnChangedCb cb, bool pathMustExist);
  void removeWatch(FileWatchPtr& watch);

  Filesystem::Instance& file_system_;
  int queue_;
  absl::node_hash_map<int, FileWatchPtr> watches_;
  Event::FileEventPtr kqueue_event_;
};

} // namespace Filesystem
} // namespace Envoy

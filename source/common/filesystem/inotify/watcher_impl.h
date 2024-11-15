#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/watcher.h"

#include "source/common/common/logger.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Filesystem {

/**
 * Implementation of Watcher that uses inotify. inotify is an awful API. In order to make this work
 * in a somewhat sane way we always watch the directory that owns the thing being watched, and then
 * filter for events that are relevant to the thing being watched.
 */
class WatcherImpl : public Watcher, Logger::Loggable<Logger::Id::file> {
public:
  WatcherImpl(Event::Dispatcher& dispatcher, Filesystem::Instance& file_system);
  ~WatcherImpl() override;

  // Filesystem::Watcher
  absl::Status addWatch(absl::string_view path, uint32_t events, OnChangedCb cb) override;

private:
  struct FileWatch {
    std::string file_;
    uint32_t events_;
    OnChangedCb cb_;
  };

  struct DirectoryWatch {
    std::list<FileWatch> watches_;
  };

  absl::Status onInotifyEvent();

  Filesystem::Instance& file_system_;
  int inotify_fd_;
  Event::FileEventPtr inotify_event_;
  absl::node_hash_map<int, DirectoryWatch> callback_map_;
};

} // namespace Filesystem
} // namespace Envoy

#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/filesystem.h"

#include "common/common/linked_object.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Filesystem {

/**
 * Implementation of Watcher that uses kqueue.
 */
class WatcherImpl : public Watcher, Logger::Loggable<Logger::Id::file> {
public:
  WatcherImpl(Event::Dispatcher& dispatcher);
  ~WatcherImpl();

  // Filesystem::Watcher
  void addWatch(const std::string& path, uint32_t events, OnChangedCb cb) override;

private:
  struct FileWatch : LinkedObject<FileWatch> {
    int fd_;
    uint32_t events_;
    std::string file_;
    OnChangedCb callback_;
    bool watchingDir_;
  };

  typedef std::shared_ptr<FileWatch> FileWatchPtr;

  void onKqueueEvent();
  FileWatchPtr addWatch_(const std::string& path, uint32_t events, Watcher::OnChangedCb cb,
                         bool pathMustExist);
  void removeWatch_(FileWatchPtr &watch);

  int queue_;
  std::unordered_map<int, FileWatchPtr> watches_;
  Event::FileEventPtr kqueue_event_;
};

} // namespace Filesystem
} // namespace Envoy

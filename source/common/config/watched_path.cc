#include "common/config/watched_path.h"

namespace Envoy {
namespace Config {

namespace {

uint32_t watchedPathEventToWatcherEvent(envoy::config::core::v3::WatchedPath::Event event) {
  switch (event) {
  case envoy::config::core::v3::WatchedPath::MOVED_TO:
    return Filesystem::Watcher::Events::MovedTo;
  case envoy::config::core::v3::WatchedPath::MODIFIED:
    return Filesystem::Watcher::Events::Modified;

  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace

WatchedPath::WatchedPath(const envoy::config::core::v3::WatchedPath& config,
                         Event::Dispatcher& dispatcher) {
  watcher_ = dispatcher.createFilesystemWatcher();
  watcher_->addWatch(config.path(), watchedPathEventToWatcherEvent(config.event()),
                     [this](uint32_t) { cb_(); });
}

} // namespace Config
} // namespace Envoy

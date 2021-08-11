#include "source/common/config/watched_directory.h"

namespace Envoy {
namespace Config {

WatchedDirectory::WatchedDirectory(const envoy::config::core::v3::WatchedDirectory& config,
                                   Event::Dispatcher& dispatcher) {
  watcher_ = dispatcher.createFilesystemWatcher();
  watcher_->addWatch(absl::StrCat(config.path(), "/"), Filesystem::Watcher::Events::MovedTo,
                     [this](uint32_t) { cb_(); });
}

} // namespace Config
} // namespace Envoy

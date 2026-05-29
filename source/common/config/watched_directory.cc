#include "source/common/config/watched_directory.h"

namespace Envoy {
namespace Config {

absl::StatusOr<std::unique_ptr<WatchedDirectory>>
WatchedDirectory::create(const envoy::config::core::v3::WatchedDirectory& config,
                         Event::Dispatcher& dispatcher) {
  absl::Status creation_status = absl::OkStatus();
  auto ret =
      std::unique_ptr<WatchedDirectory>(new WatchedDirectory(config, dispatcher, creation_status));
  RETURN_IF_NOT_OK_REF(creation_status);
  return ret;
}

WatchedDirectory::WatchedDirectory(const envoy::config::core::v3::WatchedDirectory& config,
                                   Event::Dispatcher& dispatcher, absl::Status& creation_status) {
  watcher_ = dispatcher.createFilesystemWatcher();
  SET_AND_RETURN_IF_NOT_OK(watcher_->addWatch(absl::StrCat(config.path(), "/"),
                                              Filesystem::Watcher::Events::MovedTo,
                                              [this](uint32_t) {
                                                // Check if callback is set before invoking to avoid
                                                // crash if watch triggers before setCallback().
                                                if (cb_) {
                                                  return cb_();
                                                }
                                                return absl::OkStatus();
                                              }),
                           creation_status);
}

} // namespace Config
} // namespace Envoy

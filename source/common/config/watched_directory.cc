#include "common/config/watched_directory.h"

namespace Envoy {
namespace Config {

WatchedDirectory::WatchedDirectory(const envoy::config::core::v3::WatchedDirectory& config,
                                   Event::Dispatcher& dispatcher, Filesystem::Instance& filesystem)
    : filesystem_(filesystem),
      literal_path_(filesystem.joinPath(config.root(), config.subdirectory())) {
  resolvePath();
  watcher_ = dispatcher.createFilesystemWatcher();
  watcher_->addWatch(absl::StrCat(config.root(), "/"), Filesystem::Watcher::Events::MovedTo,
                     [this](uint32_t) {
                       resolvePath();
                       cb_();
                     });
}

void WatchedDirectory::resolveDataSourcePath(envoy::config::core::v3::DataSource& data_source) {
  if (data_source.filename().empty() || resolved_path_.empty()) {
    return;
  }
  try {
    const auto result = filesystem_.splitPathFromFilename(data_source.filename());
    if (result.directory_ == literal_path_) {
      data_source.set_filename(filesystem_.joinPath(resolved_path_, result.file_));
    }
  } catch (EnvoyException& e) {
    ENVOY_LOG_MISC(warn, "Unable to split path {}: {}", data_source.filename(), e.what());
  }
}

void WatchedDirectory::resolvePath() {
  try {
    resolved_path_ = filesystem_.readSymlink(literal_path_);
  } catch (EnvoyException& e) {
    resolved_path_ = "";
    ENVOY_LOG_MISC(warn, "Unable to resolve path {}: {}", literal_path_, e.what());
  }
}

} // namespace Config
} // namespace Envoy

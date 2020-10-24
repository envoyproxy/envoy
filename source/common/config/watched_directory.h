#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/filesystem/watcher.h"

namespace Envoy {
namespace Config {

// Implement the common functionality of
// envoy::config::core::v3::WatchedDirectory.
class WatchedDirectory {
public:
  using Callback = std::function<void()>;

  WatchedDirectory(const envoy::config::core::v3::WatchedDirectory& config,
                   Event::Dispatcher& dispatcher, Filesystem::Instance& filesystem);

  void setCallback(Callback cb) { cb_ = cb; }
  // Update the path in a DataSource if its directory matches the watched
  // directory.
  void resolveDataSourcePath(envoy::config::core::v3::DataSource& data_source);

private:
  void resolvePath();

  Filesystem::Instance& filesystem_;
  // The root/subdirectory path from the WatchedDirectory.
  const std::string literal_path_;
  // The resolved path after a move event in the root directory.
  std::string resolved_path_;
  std::unique_ptr<Filesystem::Watcher> watcher_;
  Callback cb_;
};

using WatchedDirectoryPtr = std::unique_ptr<WatchedDirectory>;

} // namespace Config
} // namespace Envoy

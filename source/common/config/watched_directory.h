#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/watcher.h"

namespace Envoy {
namespace Config {

// Implement the common functionality of envoy::config::core::v3::WatchedDirectory.
class WatchedDirectory {
public:
  static absl::StatusOr<std::unique_ptr<WatchedDirectory>>
  create(const envoy::config::core::v3::WatchedDirectory& config, Event::Dispatcher& dispatcher);

  using Callback = std::function<absl::Status()>;

  void setCallback(Callback cb) { cb_ = cb; }

protected:
  WatchedDirectory(const envoy::config::core::v3::WatchedDirectory& config,
                   Event::Dispatcher& dispatcher, absl::Status& creation_status);

private:
  std::unique_ptr<Filesystem::Watcher> watcher_;
  Callback cb_;
};

using WatchedDirectoryPtr = std::unique_ptr<WatchedDirectory>;

} // namespace Config
} // namespace Envoy

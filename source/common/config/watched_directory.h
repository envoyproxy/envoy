#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/watcher.h"

namespace Envoy {
namespace Config {

// Implement the common functionality of envoy::config::core::v3::WatchedDirectory.
class WatchedDirectory {
public:
  using Callback = std::function<absl::Status()>;

  WatchedDirectory(const envoy::config::core::v3::WatchedDirectory& config,
                   Event::Dispatcher& dispatcher);

  void setCallback(Callback cb) { cb_ = cb; }

private:
  std::unique_ptr<Filesystem::Watcher> watcher_;
  Callback cb_;
};

using WatchedDirectoryPtr = std::unique_ptr<WatchedDirectory>;

} // namespace Config
} // namespace Envoy

#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/watcher.h"

namespace Envoy {
namespace Config {

// Implement the common functionality of envoy::config::core::v3::WatchedPath.
class WatchedPath {
public:
  using Callback = std::function<void()>;

  WatchedPath(const envoy::config::core::v3::WatchedPath& config, Event::Dispatcher& dispatcher);

  void setCallback(Callback cb) { cb_ = cb; }

private:
  std::unique_ptr<Filesystem::Watcher> watcher_;
  Callback cb_;
};

using WatchedPathPtr = std::unique_ptr<WatchedPath>;

} // namespace Config
} // namespace Envoy

#pragma once

#include "envoy/api/api.h"
#include "envoy/api/v2/core/base.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/filesystem.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Config {

/**
 * Filesystem inotify implementation of the API Subscription interface. This allows the API to be
 * consumed on filesystem changes to files containing the JSON canonical representation of
 * lists of xDS resources.
 */
class FilesystemSubscriptionImpl : public Config::Subscription,
                                   Logger::Loggable<Logger::Id::config> {
public:
  FilesystemSubscriptionImpl(Event::Dispatcher& dispatcher, absl::string_view path,
                             SubscriptionCallbacks& callbacks, SubscriptionStats stats,
                             Api::Api& api);

  // Config::Subscription
  // We report all discovered resources in the watched file, so the resource names arguments are
  // unused, and updateResources is a no-op (other than updating a stat).
  void start(const std::set<std::string>&) override;
  void updateResources(const std::set<std::string>&) override;

private:
  void refresh();

  bool started_{};
  const std::string path_;
  std::unique_ptr<Filesystem::Watcher> watcher_;
  SubscriptionCallbacks& callbacks_;
  SubscriptionStats stats_;
  Api::Api& api_;
};

} // namespace Config
} // namespace Envoy

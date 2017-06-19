#pragma once

#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/filesystem.h"

#include "common/common/macros.h"
#include "common/config/utility.h"
#include "common/filesystem/filesystem_impl.h"

#include "api/base.pb.h"
#include "google/protobuf/util/json_util.h"

namespace Envoy {
namespace Filesystem {

/**
 * Filesystem inotify implementation of the API Subscription interface. This allows the API to be
 * consumed on filesystem changes to files containing the JSON canonical representation of
 * lists of ResourceType.
 */
template <class ResourceType> class SubscriptionImpl : public Config::Subscription<ResourceType> {
public:
  SubscriptionImpl(Event::Dispatcher& dispatcher, const std::string& path)
      : path_(path), watcher_(dispatcher.createFilesystemWatcher()) {}

  // Config::Subscription
  void start(const std::vector<std::string>& resources,
             Config::SubscriptionCallbacks<ResourceType>& callbacks) override {
    // We report all discovered resources in the watched file.
    UNREFERENCED_PARAMETER(resources);
    callbacks_ = &callbacks;
    watcher_->addWatch(path_, Watcher::Events::MovedTo, [this](uint32_t events) {
      UNREFERENCED_PARAMETER(events);
      refresh();
    });
  }

  void updateResources(const std::vector<std::string>& resources) override {
    // We report all discovered resources in the watched file.
    UNREFERENCED_PARAMETER(resources);
  }

private:
  void refresh() {
    try {
      const std::string json = Filesystem::fileReadToEnd(path_);
      envoy::api::v2::DiscoveryResponse message;
      const auto status = google::protobuf::util::JsonStringToMessage(json, &message);
      if (status != google::protobuf::util::Status::OK) {
        // TODO(htuch): Track stats and log failures.
        return;
      }
      const auto typed_resources = Config::Utility::getTypedResources<ResourceType>(message);
      if (callbacks_->onConfigUpdate(typed_resources)) {
        // TODO(htuch): Add some notion of current version for every API in stats/admin.
      } else {
        // TODO(htuch): Track stats and log failures.
      }
    } catch (const EnvoyException& ex) {
      // TODO(htuch): Track stats and log failures.
    }
  }

  const std::string path_;
  std::unique_ptr<Watcher> watcher_;
  Config::SubscriptionCallbacks<ResourceType>* callbacks_{};
};

} // namespace Filesystem
} // namespace Envoy

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
namespace Config {

/**
 * Filesystem inotify implementation of the API Subscription interface. This allows the API to be
 * consumed on filesystem changes to files containing the JSON canonical representation of
 * lists of ResourceType.
 */
template <class ResourceType>
class FilesystemSubscriptionImpl : public Config::Subscription<ResourceType> {
public:
  FilesystemSubscriptionImpl(Event::Dispatcher& dispatcher, const std::string& path)
      : path_(path), watcher_(dispatcher.createFilesystemWatcher()) {}

  // Config::Subscription
  void start(const std::vector<std::string>& resources,
             Config::SubscriptionCallbacks<ResourceType>& callbacks) override {
    // We report all discovered resources in the watched file.
    UNREFERENCED_PARAMETER(resources);
    callbacks_ = &callbacks;
    watcher_->addWatch(path_, Filesystem::Watcher::Events::MovedTo, [this](uint32_t events) {
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
        callbacks_->onConfigUpdateFailed(nullptr);
        return;
      }
      const auto typed_resources = Config::Utility::getTypedResources<ResourceType>(message);
      callbacks_->onConfigUpdate(typed_resources);
      // TODO(htuch): Add some notion of current version for every API in stats/admin.
    } catch (const EnvoyException& e) {
      // TODO(htuch): Track stats and log failures.
      callbacks_->onConfigUpdateFailed(&e);
    }
  }

  const std::string path_;
  std::unique_ptr<Filesystem::Watcher> watcher_;
  Config::SubscriptionCallbacks<ResourceType>* callbacks_{};
};

} // namespace Config
} // namespace Envoy

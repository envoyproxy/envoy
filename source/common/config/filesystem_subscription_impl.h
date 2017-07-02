#pragma once

#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/filesystem.h"

#include "common/common/logger.h"
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
class FilesystemSubscriptionImpl : public Config::Subscription<ResourceType>,
                                   Logger::Loggable<Logger::Id::config> {
public:
  FilesystemSubscriptionImpl(Event::Dispatcher& dispatcher, const std::string& path,
                             SubscriptionStats stats)
      : path_(path), watcher_(dispatcher.createFilesystemWatcher()), stats_(stats) {}

  // Config::Subscription
  void start(const std::vector<std::string>& resources,
             Config::SubscriptionCallbacks<ResourceType>& callbacks) override {
    // We report all discovered resources in the watched file.
    UNREFERENCED_PARAMETER(resources);
    callbacks_ = &callbacks;
    watcher_->addWatch(path_, Filesystem::Watcher::Events::MovedTo, [this](uint32_t events) {
      UNREFERENCED_PARAMETER(events);
      refresh(false);
    });
    // Attempt to read, gracefully fail if the file isn't there yet.
    refresh(true);
  }

  void updateResources(const std::vector<std::string>& resources) override {
    // We report all discovered resources in the watched file.
    UNREFERENCED_PARAMETER(resources);
  }

private:
  void refresh(bool ignore_read_failure) {
    ENVOY_LOG(debug, "Filesystem config refresh for {}", path_);
    stats_.update_attempt_.inc();
    try {
      const std::string json = Filesystem::fileReadToEnd(path_);
      envoy::api::v2::DiscoveryResponse message;
      const auto status = google::protobuf::util::JsonStringToMessage(json, &message);
      if (status != google::protobuf::util::Status::OK) {
        callbacks_->onConfigUpdateFailed(nullptr);
        ENVOY_LOG(warn, "Filesystem config JSON conversion error: {}", status.ToString());
        stats_.update_failure_.inc();
        return;
      }
      const auto typed_resources = Config::Utility::getTypedResources<ResourceType>(message);
      try {
        callbacks_->onConfigUpdate(typed_resources);
        stats_.update_success_.inc();
        // TODO(htuch): Add some notion of current version for every API in stats/admin.
      } catch (const EnvoyException& e) {
        ENVOY_LOG(warn, "Filesystem config update rejected: {}", e.what());
        stats_.update_rejected_.inc();
        callbacks_->onConfigUpdateFailed(&e);
      }
    } catch (const EnvoyException& e) {
      if (!ignore_read_failure) {
        ENVOY_LOG(warn, "Filesystem config update failure: {}", e.what());
        stats_.update_failure_.inc();
        callbacks_->onConfigUpdateFailed(&e);
      }
    }
  }

  const std::string path_;
  std::unique_ptr<Filesystem::Watcher> watcher_;
  SubscriptionCallbacks<ResourceType>* callbacks_{};
  SubscriptionStats stats_;
};

} // namespace Config
} // namespace Envoy

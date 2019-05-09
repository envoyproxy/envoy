#include "common/config/filesystem_subscription_impl.h"

#include "common/common/macros.h"
#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

FilesystemSubscriptionImpl::FilesystemSubscriptionImpl(Event::Dispatcher& dispatcher,
                                                       absl::string_view path,
                                                       SubscriptionStats stats, Api::Api& api)
    : path_(path), watcher_(dispatcher.createFilesystemWatcher()), stats_(stats), api_(api) {
  watcher_->addWatch(path_, Filesystem::Watcher::Events::MovedTo, [this](uint32_t events) {
    UNREFERENCED_PARAMETER(events);
    if (started_) {
      refresh();
    }
  });
}

// Config::Subscription
void FilesystemSubscriptionImpl::start(const std::set<std::string>& resources,
                                       Config::SubscriptionCallbacks& callbacks) {
  // We report all discovered resources in the watched file.
  UNREFERENCED_PARAMETER(resources);
  callbacks_ = &callbacks;
  started_ = true;
  // Attempt to read in case there is a file there already.
  refresh();
}

void FilesystemSubscriptionImpl::updateResources(const std::set<std::string>& resources) {
  // We report all discovered resources in the watched file.
  UNREFERENCED_PARAMETER(resources);
  // Bump stats for consistence behavior with other xDS.
  stats_.update_attempt_.inc();
}

void FilesystemSubscriptionImpl::refresh() {
  ENVOY_LOG(debug, "Filesystem config refresh for {}", path_);
  stats_.update_attempt_.inc();
  bool config_update_available = false;
  try {
    envoy::api::v2::DiscoveryResponse message;
    MessageUtil::loadFromFile(path_, message, api_);
    config_update_available = true;
    callbacks_->onConfigUpdate(message.resources(), message.version_info());
    stats_.version_.set(HashUtil::xxHash64(message.version_info()));
    stats_.update_success_.inc();
    ENVOY_LOG(debug, "Filesystem config update accepted for {}: {}", path_, message.DebugString());
  } catch (const EnvoyException& e) {
    if (config_update_available) {
      ENVOY_LOG(warn, "Filesystem config update rejected: {}", e.what());
      stats_.update_rejected_.inc();
    } else {
      ENVOY_LOG(warn, "Filesystem config update failure: {}", e.what());
      stats_.update_failure_.inc();
    }
    callbacks_->onConfigUpdateFailed(&e);
  }
}

} // namespace Config
} // namespace Envoy

#include "common/config/filesystem_subscription_impl.h"

#include "common/common/macros.h"
#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

FilesystemSubscriptionImpl::FilesystemSubscriptionImpl(
    Event::Dispatcher& dispatcher, absl::string_view path, SubscriptionCallbacks& callbacks,
    SubscriptionStats stats, ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api)
    : path_(path), watcher_(dispatcher.createFilesystemWatcher()), callbacks_(callbacks),
      stats_(stats), api_(api), validation_visitor_(validation_visitor) {
  watcher_->addWatch(path_, Filesystem::Watcher::Events::MovedTo, [this](uint32_t) {
    if (started_) {
      refresh();
    }
  });
}

// Config::Subscription
void FilesystemSubscriptionImpl::start(const std::set<std::string>&) {
  started_ = true;
  // Attempt to read in case there is a file there already.
  refresh();
}

void FilesystemSubscriptionImpl::updateResources(const std::set<std::string>&) {
  // Bump stats for consistence behavior with other xDS.
  stats_.update_attempt_.inc();
}

void FilesystemSubscriptionImpl::refresh() {
  ENVOY_LOG(debug, "Filesystem config refresh for {}", path_);
  stats_.update_attempt_.inc();
  bool config_update_available = false;
  envoy::api::v2::DiscoveryResponse message;
  try {
    MessageUtil::loadFromFile(path_, message, validation_visitor_, api_);
    config_update_available = true;
    callbacks_.onConfigUpdate(message.resources(), message.version_info());
    stats_.version_.set(HashUtil::xxHash64(message.version_info()));
    stats_.update_success_.inc();
    ENVOY_LOG(debug, "Filesystem config update accepted for {}: {}", path_, message.DebugString());
  } catch (const EnvoyException& e) {
    if (config_update_available) {
      ENVOY_LOG(warn, "Filesystem config update rejected: {}", e.what());
      ENVOY_LOG(debug, "Failed configuration:\n{}", message.DebugString());
      stats_.update_rejected_.inc();
      callbacks_.onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, &e);
    } else {
      ENVOY_LOG(warn, "Filesystem config update failure: {}", e.what());
      stats_.update_failure_.inc();
      // ConnectionFailure is not a meaningful error code for file system but it has been chosen so
      // that the behaviour is uniform across all subscription types.
      callbacks_.onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure,
                                      &e);
    }
  }
}

} // namespace Config
} // namespace Envoy

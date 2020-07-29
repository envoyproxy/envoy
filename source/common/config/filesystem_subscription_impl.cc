#include "common/config/filesystem_subscription_impl.h"

#include "envoy/service/discovery/v3/discovery.pb.h"

#include "common/common/macros.h"
#include "common/common/utility.h"
#include "common/config/decoded_resource_impl.h"
#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

FilesystemSubscriptionImpl::FilesystemSubscriptionImpl(
    Event::Dispatcher& dispatcher, absl::string_view path, SubscriptionCallbacks& callbacks,
    OpaqueResourceDecoder& resource_decoder, SubscriptionStats stats,
    ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api)
    : path_(path), watcher_(dispatcher.createFilesystemWatcher()), callbacks_(callbacks),
      resource_decoder_(resource_decoder), stats_(stats), api_(api),
      validation_visitor_(validation_visitor) {
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

void FilesystemSubscriptionImpl::updateResourceInterest(const std::set<std::string>&) {
  // Bump stats for consistent behavior with other xDS.
  stats_.update_attempt_.inc();
}

void FilesystemSubscriptionImpl::configRejected(const EnvoyException& e,
                                                const std::string& message) {
  ENVOY_LOG(warn, "Filesystem config update rejected: {}", e.what());
  ENVOY_LOG(debug, "Failed configuration:\n{}", message);
  stats_.update_rejected_.inc();
  callbacks_.onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, &e);
}

void FilesystemSubscriptionImpl::refresh() {
  ENVOY_LOG(debug, "Filesystem config refresh for {}", path_);
  stats_.update_attempt_.inc();
  bool config_update_available = false;
  envoy::service::discovery::v3::DiscoveryResponse message;
  try {
    MessageUtil::loadFromFile(path_, message, validation_visitor_, api_);
    config_update_available = true;
    const auto decoded_resources =
        DecodedResourcesWrapper(resource_decoder_, message.resources(), message.version_info());
    callbacks_.onConfigUpdate(decoded_resources.refvec_, message.version_info());
    stats_.update_time_.set(DateUtil::nowToMilliseconds(api_.timeSource()));
    stats_.version_.set(HashUtil::xxHash64(message.version_info()));
    stats_.version_text_.set(message.version_info());
    stats_.update_success_.inc();
    ENVOY_LOG(debug, "Filesystem config update accepted for {}: {}", path_, message.DebugString());
  } catch (const ProtobufMessage::UnknownProtoFieldException& e) {
    configRejected(e, message.DebugString());
  } catch (const EnvoyException& e) {
    if (config_update_available) {
      configRejected(e, message.DebugString());
    } else {
      ENVOY_LOG(warn, "Filesystem config update failure: {}", e.what());
      stats_.update_failure_.inc();
      // This could happen due to filesystem issues or a bad configuration (e.g. proto validation).
      // Since the latter is more likely, for now we will treat it as rejection.
      callbacks_.onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, &e);
    }
  }
}

} // namespace Config
} // namespace Envoy

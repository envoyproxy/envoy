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
void FilesystemSubscriptionImpl::start(const std::set<std::string>&, const bool) {
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

FilesystemCollectionSubscriptionImpl::FilesystemCollectionSubscriptionImpl(
    Event::Dispatcher& dispatcher, absl::string_view path, SubscriptionCallbacks& callbacks,
    OpaqueResourceDecoder& resource_decoder, SubscriptionStats stats,
    ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api)
    : FilesystemSubscriptionImpl(dispatcher, path, callbacks, resource_decoder, stats,
                                 validation_visitor, api) {}

void FilesystemCollectionSubscriptionImpl::refresh() {
  ENVOY_LOG(debug, "Filesystem collection config refresh for {}", path_);
  stats_.update_attempt_.inc();
  bool config_update_available = false;
  envoy::service::discovery::v3::Resource resource_message;
  try {
    MessageUtil::loadFromFile(path_, resource_message, validation_visitor_, api_);
    // Dynamically load the collection message.
    const std::string collection_type =
        std::string(TypeUtil::typeUrlToDescriptorFullName(resource_message.resource().type_url()));
    const Protobuf::Descriptor* collection_descriptor =
        Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(collection_type);
    if (collection_descriptor == nullptr) {
      throw EnvoyException(fmt::format("Unknown collection type {}", collection_type));
    }
    Protobuf::DynamicMessageFactory dmf;
    ProtobufTypes::MessagePtr collection_message;
    collection_message.reset(dmf.GetPrototype(collection_descriptor)->New());
    MessageUtil::unpackTo(resource_message.resource(), *collection_message);
    const auto* collection_entries_field_descriptor = collection_descriptor->field(0);
    // Verify collection message type structure.
    if (collection_entries_field_descriptor == nullptr ||
        collection_entries_field_descriptor->type() != Protobuf::FieldDescriptor::TYPE_MESSAGE ||
        collection_entries_field_descriptor->message_type()->full_name() !=
            "udpa.core.v1.CollectionEntry" ||
        !collection_entries_field_descriptor->is_repeated()) {
      throw EnvoyException(fmt::format("Invalid structure for collection type {} in {}",
                                       collection_type, resource_message.DebugString()));
    }
    const auto* reflection = collection_message->GetReflection();
    const uint32_t num_entries =
        reflection->FieldSize(*collection_message, collection_entries_field_descriptor);
    DecodedResourcesWrapper decoded_resources;
    for (uint32_t i = 0; i < num_entries; ++i) {
      udpa::core::v1::CollectionEntry collection_entry;
      collection_entry.MergeFrom(reflection->GetRepeatedMessage(
          *collection_message, collection_entries_field_descriptor, i));
      // TODO(htuch): implement indirect collection entries.
      if (collection_entry.has_inline_entry()) {
        decoded_resources.push_back(std::make_unique<DecodedResourceImpl>(
            resource_decoder_, collection_entry.inline_entry()));
      }
    }
    config_update_available = true;
    callbacks_.onConfigUpdate(decoded_resources.refvec_, resource_message.version());
    stats_.update_time_.set(DateUtil::nowToMilliseconds(api_.timeSource()));
    stats_.version_.set(HashUtil::xxHash64(resource_message.version()));
    stats_.version_text_.set(resource_message.version());
    stats_.update_success_.inc();
    ENVOY_LOG(debug, "Filesystem config update accepted for {}: {}", path_,
              resource_message.DebugString());
  } catch (const ProtobufMessage::UnknownProtoFieldException& e) {
    ENVOY_LOG_MISC(debug, "HTD foo");
    configRejected(e, resource_message.DebugString());
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "HTD bar");
    if (config_update_available) {
      configRejected(e, resource_message.DebugString());
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

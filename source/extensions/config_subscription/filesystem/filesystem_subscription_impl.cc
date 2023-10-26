#include "source/extensions/config_subscription/filesystem/filesystem_subscription_impl.h"

#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/macros.h"
#include "source/common/common/utility.h"
#include "source/common/config/decoded_resource_impl.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Config {

envoy::config::core::v3::PathConfigSource makePathConfigSource(const std::string& path) {
  envoy::config::core::v3::PathConfigSource path_config_source;
  path_config_source.set_path(path);
  return path_config_source;
}

FilesystemSubscriptionImpl::FilesystemSubscriptionImpl(
    Event::Dispatcher& dispatcher,
    const envoy::config::core::v3::PathConfigSource& path_config_source,
    SubscriptionCallbacks& callbacks, OpaqueResourceDecoderSharedPtr resource_decoder,
    SubscriptionStats stats, ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api)
    : path_(path_config_source.path()), callbacks_(callbacks), resource_decoder_(resource_decoder),
      stats_(stats), api_(api), validation_visitor_(validation_visitor) {
  if (!path_config_source.has_watched_directory()) {
    file_watcher_ = dispatcher.createFilesystemWatcher();
    file_watcher_->addWatch(path_, Filesystem::Watcher::Events::MovedTo, [this](uint32_t) {
      if (started_) {
        refresh();
      }
    });
  } else {
    directory_watcher_ =
        std::make_unique<WatchedDirectory>(path_config_source.watched_directory(), dispatcher);
    directory_watcher_->setCallback([this]() {
      if (started_) {
        refresh();
      }
    });
  }
}

// Config::Subscription
void FilesystemSubscriptionImpl::start(const absl::flat_hash_set<std::string>&) {
  started_ = true;
  // Attempt to read in case there is a file there already.
  refresh();
}

void FilesystemSubscriptionImpl::updateResourceInterest(const absl::flat_hash_set<std::string>&) {
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

std::string FilesystemSubscriptionImpl::refreshInternal(ProtobufTypes::MessagePtr* config_update) {
  auto owned_message = std::make_unique<envoy::service::discovery::v3::DiscoveryResponse>();
  auto& message = *owned_message;
  MessageUtil::loadFromFile(path_, message, validation_visitor_, api_);
  *config_update = std::move(owned_message);
  const auto decoded_resources =
      DecodedResourcesWrapper(*resource_decoder_, message.resources(), message.version_info());
  THROW_IF_NOT_OK(callbacks_.onConfigUpdate(decoded_resources.refvec_, message.version_info()));
  return message.version_info();
}

void FilesystemSubscriptionImpl::refresh() {
  ENVOY_LOG(debug, "Filesystem config refresh for {}", path_);
  stats_.update_attempt_.inc();
  ProtobufTypes::MessagePtr config_update;
  TRY_ASSERT_MAIN_THREAD {
    const std::string version = refreshInternal(&config_update);
    stats_.update_time_.set(DateUtil::nowToMilliseconds(api_.timeSource()));
    stats_.version_.set(HashUtil::xxHash64(version));
    stats_.version_text_.set(version);
    stats_.update_success_.inc();
    ENVOY_LOG(debug, "Filesystem config update accepted for {}: {}", path_,
              config_update->DebugString());
  }
  END_TRY
  catch (const ProtobufMessage::UnknownProtoFieldException& e) {
    configRejected(e, config_update == nullptr ? "" : config_update->DebugString());
  }
  catch (const EnvoyException& e) {
    if (config_update != nullptr) {
      configRejected(e, config_update->DebugString());
    } else {
      ENVOY_LOG(warn, "Filesystem config update failure: in {}, {}", path_, e.what());
      stats_.update_failure_.inc();
      // This could happen due to filesystem issues or a bad configuration (e.g. proto validation).
      // Since the latter is more likely, for now we will treat it as rejection.
      callbacks_.onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::UpdateRejected, &e);
    }
  }
}

FilesystemCollectionSubscriptionImpl::FilesystemCollectionSubscriptionImpl(
    Event::Dispatcher& dispatcher,
    const envoy::config::core::v3::PathConfigSource& path_config_source,
    SubscriptionCallbacks& callbacks, OpaqueResourceDecoderSharedPtr resource_decoder,
    SubscriptionStats stats, ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api)
    : FilesystemSubscriptionImpl(dispatcher, path_config_source, callbacks, resource_decoder, stats,
                                 validation_visitor, api) {}

std::string
FilesystemCollectionSubscriptionImpl::refreshInternal(ProtobufTypes::MessagePtr* config_update) {
  auto owned_resource_message = std::make_unique<envoy::service::discovery::v3::Resource>();
  auto& resource_message = *owned_resource_message;
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
          "xds.core.v3.CollectionEntry" ||
      !collection_entries_field_descriptor->is_repeated()) {
    throw EnvoyException(fmt::format("Invalid structure for collection type {} in {}",
                                     collection_type, resource_message.DebugString()));
  }
  const auto* reflection = collection_message->GetReflection();
  const uint32_t num_entries =
      reflection->FieldSize(*collection_message, collection_entries_field_descriptor);
  DecodedResourcesWrapper decoded_resources;
  for (uint32_t i = 0; i < num_entries; ++i) {
    xds::core::v3::CollectionEntry collection_entry;
    collection_entry.MergeFrom(reflection->GetRepeatedMessage(
        *collection_message, collection_entries_field_descriptor, i));
    // TODO(htuch): implement indirect collection entries.
    if (collection_entry.has_inline_entry()) {
      decoded_resources.pushBack(std::make_unique<DecodedResourceImpl>(
          *resource_decoder_, collection_entry.inline_entry()));
    }
  }
  *config_update = std::move(owned_resource_message);
  THROW_IF_NOT_OK(callbacks_.onConfigUpdate(decoded_resources.refvec_, resource_message.version()));
  return resource_message.version();
}

REGISTER_FACTORY(FilesystemSubscriptionFactory, ConfigSubscriptionFactory);
REGISTER_FACTORY(FilesystemCollectionSubscriptionFactory, ConfigSubscriptionFactory);

} // namespace Config
} // namespace Envoy

#include "contrib/config/source/kv_store_xds_delegate.h"

#include "envoy/registry/registry.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/logger.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "contrib/envoy/extensions/config/v3alpha/kv_store_xds_delegate_config.pb.h"
#include "contrib/envoy/extensions/config/v3alpha/kv_store_xds_delegate_config.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Config {
namespace {

using ::Envoy::Config::XdsSourceId;
using ::envoy::extensions::config::v3alpha::KeyValueStoreXdsDelegateConfig;

// Constructs the key for the resource, to be used in the KeyValueStore.
std::string constructKey(const XdsSourceId& source_id, const std::string& resource_name) {
  static constexpr char DELIMITER[] = "+";
  return absl::StrCat(source_id.toKey(), DELIMITER, resource_name);
}

} // namespace

KeyValueStoreXdsDelegate::KeyValueStoreXdsDelegate(KeyValueStorePtr&& xds_config_store)
    : xds_config_store_(std::move(xds_config_store)) {}

std::vector<envoy::service::discovery::v3::Resource>
KeyValueStoreXdsDelegate::getResources(const XdsSourceId& source_id,
                                       const std::vector<std::string>& resource_names) const {
  if (resource_names.empty() || (resource_names.size() == 1 && resource_names[0] == "*")) {
    // Empty names or one entry with "*" means wildcard.
    return getAllResources(source_id);
  }

  std::vector<envoy::service::discovery::v3::Resource> resources;
  for (const std::string& resource_name : resource_names) {
    if (auto existing_resource = xds_config_store_->get(constructKey(source_id, resource_name))) {
      envoy::service::discovery::v3::Resource r;
      r.ParseFromString(std::string(*existing_resource));
      resources.push_back(std::move(r));
    }
  }
  return resources;
}

std::vector<envoy::service::discovery::v3::Resource>
KeyValueStoreXdsDelegate::getAllResources(const XdsSourceId& source_id) const {
  std::vector<envoy::service::discovery::v3::Resource> resources;
  // TODO(abeyad): This is slow as we are iterating over all entries in the KV store; the
  // expectation is we won't be iterating over too many values. But still, try to find a better way.
  xds_config_store_->iterate(
      [&resources, &source_id](const std::string& key, const std::string& value) {
        // TODO(abeyad): Don't include TTL'ed resources.
        if (absl::StartsWith(key, source_id.toKey())) {
          // The source id is a prefix of the key, so it should be included in the list of returned
          // resources.
          envoy::service::discovery::v3::Resource r;
          r.ParseFromString(value);
          resources.push_back(std::move(r));
        }
        return KeyValueStore::Iterate::Continue;
      });
  return resources;
}

void KeyValueStoreXdsDelegate::onConfigUpdated(
    const XdsSourceId& source_id, const std::vector<Envoy::Config::DecodedResourceRef>& resources) {
  for (const auto& resource_ref : resources) {
    const auto& decoded_resource = resource_ref.get();
    if (decoded_resource.hasResource()) {
      envoy::service::discovery::v3::Resource r;
      // TODO(abeyad): Support dynamic parameter constraints.
      r.set_name(decoded_resource.name());
      r.set_version(decoded_resource.version());
      r.mutable_resource()->PackFrom(decoded_resource.resource());
      if (decoded_resource.ttl()) {
        r.mutable_ttl()->CopyFrom(Protobuf::util::TimeUtil::MillisecondsToDuration(
            decoded_resource.ttl().value().count()));
      }
      // TODO(abeyad): Set TTL parameter, if it exists.
      xds_config_store_->addOrUpdate(constructKey(source_id, r.name()), r.SerializeAsString(),
                                     absl::nullopt);
    } else {
      ENVOY_LOG_MISC(warn,
                     "KeyValueStore xDS delegate didn't persist xDS update {}: missing resource",
                     decoded_resource.name());
    }
  }
}

Envoy::ProtobufTypes::MessagePtr KeyValueStoreXdsDelegateFactory::createEmptyConfigProto() {
  return std::make_unique<KeyValueStoreXdsDelegateConfig>();
}

std::string KeyValueStoreXdsDelegateFactory::name() const {
  return "envoy.xds_delegates.kv_store";
};

Envoy::Config::XdsResourcesDelegatePtr KeyValueStoreXdsDelegateFactory::createXdsResourcesDelegate(
    const ProtobufWkt::Any& config, ProtobufMessage::ValidationVisitor& validation_visitor,
    Api::Api& api, Event::Dispatcher& dispatcher) {
  const auto& validator_config =
      Envoy::MessageUtil::anyConvertAndValidate<KeyValueStoreXdsDelegateConfig>(config,
                                                                                validation_visitor);
  auto& kv_store_factory = Envoy::Config::Utility::getAndCheckFactory<Envoy::KeyValueStoreFactory>(
      validator_config.key_value_store_config().config());
  KeyValueStorePtr xds_config_store = kv_store_factory.createStore(
      validator_config.key_value_store_config(), validation_visitor, dispatcher, api.fileSystem());
  return std::make_unique<KeyValueStoreXdsDelegate>(std::move(xds_config_store));
}

REGISTER_FACTORY(KeyValueStoreXdsDelegateFactory, Envoy::Config::XdsResourcesDelegateFactory);

} // namespace Config
} // namespace Extensions
} // namespace Envoy

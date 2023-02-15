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

XdsKeyValueStoreStats KeyValueStoreXdsDelegate::generateStats(Stats::Scope& scope) {
  return {ALL_XDS_KV_STORE_STATS(POOL_COUNTER(scope))};
}

KeyValueStoreXdsDelegate::KeyValueStoreXdsDelegate(KeyValueStorePtr&& xds_config_store,
                                                   Stats::Scope& root_scope)
    : xds_config_store_(std::move(xds_config_store)),
      scope_(root_scope.createScope("xds.kv_store.")), stats_(generateStats(*scope_)) {}

std::vector<envoy::service::discovery::v3::Resource> KeyValueStoreXdsDelegate::getResources(
    const XdsSourceId& source_id, const absl::flat_hash_set<std::string>& resource_names) const {
  std::vector<envoy::service::discovery::v3::Resource> resources;
  if (resource_names.empty() ||
      (resource_names.size() == 1 && resource_names.contains(Envoy::Config::Wildcard))) {
    // Empty names or one entry with "*" means wildcard.
    resources = getAllResources(source_id);
  } else {
    for (const std::string& resource_name : resource_names) {
      const std::string resource_key = constructKey(source_id, resource_name);
      if (const auto existing_resource = xds_config_store_->get(resource_key)) {
        envoy::service::discovery::v3::Resource r;
        if (r.ParseFromString(std::string(*existing_resource))) {
          resources.push_back(std::move(r));
        } else {
          // Resource failed to parse; this shouldn't happen unless fields get removed from the
          // proto. Since the serialized resource in the KV store is no longer parseable into an
          // xDS resource, we'll remove it from the store and not use it in xDS processing.
          xds_config_store_->remove(resource_key);
          stats_.parse_failed_.inc();
        }
      } else {
        stats_.resource_missing_.inc();
      }
    }
  }

  if (resources.empty()) {
    stats_.resources_not_found_.inc();
  } else {
    stats_.load_success_.inc();
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
      absl::optional<std::chrono::seconds> ttl = absl::nullopt;
      if (decoded_resource.ttl().has_value()) {
        r.mutable_ttl()->CopyFrom(
            Protobuf::util::TimeUtil::MillisecondsToDuration(decoded_resource.ttl()->count()));
        ttl = std::chrono::duration_cast<std::chrono::seconds>(decoded_resource.ttl().value());
      }
      std::string serialized_resource;
      if (r.SerializeToString(&serialized_resource)) {
        xds_config_store_->addOrUpdate(constructKey(source_id, r.name()),
                                       std::move(serialized_resource), ttl);
      } else {
        stats_.serialization_failed_.inc();
        ENVOY_LOG_MISC(
            warn,
            "KeyValueStore xDS delegate didn't persist xDS update {}: resource serialiation failed",
            decoded_resource.name());
      }
    } else {
      ENVOY_LOG_MISC(warn,
                     "KeyValueStore xDS delegate didn't persist xDS update {}: missing resource",
                     decoded_resource.name());
    }
  }
}

void KeyValueStoreXdsDelegate::onResourceLoadFailed(
    const XdsSourceId& source_id, const std::string& resource_name,
    const absl::optional<EnvoyException>& exception) {
  // The resource failed to load, so remove it from the store.
  xds_config_store_->remove(constructKey(source_id, resource_name));
  stats_.xds_load_failed_.inc();
  ENVOY_LOG_MISC(warn, "Failed to load locally-persisted xDS resource(s) for {}, {}: {}",
                 source_id.toKey(), resource_name,
                 (exception.has_value() ? exception->what() : "no exception"));
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
  return std::make_unique<KeyValueStoreXdsDelegate>(std::move(xds_config_store), api.rootScope());
}

REGISTER_FACTORY(KeyValueStoreXdsDelegateFactory, Envoy::Config::XdsResourcesDelegateFactory);

} // namespace Config
} // namespace Extensions
} // namespace Envoy

#pragma once

#include "envoy/common/key_value_store.h"
#include "envoy/config/xds_resources_delegate.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace Config {

// All KV store xDS delegate stats. @see stats_macros.h.
#define ALL_XDS_KV_STORE_STATS(COUNTER)                                                            \
  /* Number of times a resource failed to serialize for persistence in the KV store. */            \
  COUNTER(serialization_failed)                                                                    \
  /* Number of times a resource failed to load due to parsing/validation issues. */                \
  COUNTER(xds_load_failed)                                                                         \
  /* Number of times resources were loaded successfully from the KV store. */                      \
  COUNTER(load_success)                                                                            \
  /* Number of times no resources were found for a load attempt from the KV store. */              \
  COUNTER(resources_not_found)                                                                     \
  /* Number of times a persisted resource failed to parse into a xDS proto. */                     \
  COUNTER(parse_failed)                                                                            \
  /* Number of times a resource was requested but not found from the KV store. */                  \
  COUNTER(resource_missing)

// Struct definition for all KV store xDS delegate stats. @see stats_macros.h
struct XdsKeyValueStoreStats {
  ALL_XDS_KV_STORE_STATS(GENERATE_COUNTER_STRUCT);
};

// An implementation of the XdsResourcesDelegate interface that saves and retrieves xDS resources
// to/from the configured KeyValueStore implementation.
//
// The configured KeyValueStore should not do any storage or network I/O on the main thread in their
// addOrUpdate() and get() implementations. Doing so would cause the main thread to block, since
// the delegate is invoked on the main Envoy thread.
//
// The handling of wildcard resources is designed for use with O(100) resources or fewer, so it's
// not currently advised to use this feature for large and complicated configurations.
class KeyValueStoreXdsDelegate : public Envoy::Config::XdsResourcesDelegate {
public:
  KeyValueStoreXdsDelegate(KeyValueStorePtr&& xds_config_store, Stats::Scope& root_scope);

  std::vector<envoy::service::discovery::v3::Resource>
  getResources(const Envoy::Config::XdsSourceId& source_id,
               const absl::flat_hash_set<std::string>& resource_names) const override;

  void onConfigUpdated(const Envoy::Config::XdsSourceId& source_id,
                       const std::vector<Envoy::Config::DecodedResourceRef>& resources) override;

  void onResourceLoadFailed(const Envoy::Config::XdsSourceId& source_id,
                            const std::string& resource_name,
                            const absl::optional<EnvoyException>& exception) override;

private:
  // Gets all the resources present in the KeyValueStore for the given source_id. This is the
  // equivalent of wildcard xDS requests.
  std::vector<envoy::service::discovery::v3::Resource>
  getAllResources(const Envoy::Config::XdsSourceId& source_id) const;

  static XdsKeyValueStoreStats generateStats(Stats::Scope& scope);

  KeyValueStorePtr xds_config_store_;
  Stats::ScopeSharedPtr scope_;
  XdsKeyValueStoreStats stats_;
};

// A factory for creating instances of KeyValueStoreXdsDelegate from the typed_config field of a
// TypedExtensionConfig protocol buffer message.
class KeyValueStoreXdsDelegateFactory : public Envoy::Config::XdsResourcesDelegateFactory {
public:
  KeyValueStoreXdsDelegateFactory() = default;

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;

  Envoy::Config::XdsResourcesDelegatePtr
  createXdsResourcesDelegate(const ProtobufWkt::Any& config,
                             ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api,
                             Event::Dispatcher& dispatcher) override;
};

} // namespace Config
} // namespace Extensions
} // namespace Envoy

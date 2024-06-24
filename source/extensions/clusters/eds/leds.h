#pragma once

#include <memory>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/local_info/local_info.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/config/subscription_base.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * All per-cluster LEDS stats. @see stats_macros.h
 * These will be added to the subscription stats.
 */
#define ALL_LEDS_STATS(COUNTER) COUNTER(update_empty)

/**
 * Struct definition for all per-cluster LEDS stats. @see stats_macros.h
 */
struct LedsStats {
  ALL_LEDS_STATS(GENERATE_COUNTER_STRUCT)
};

/*
 * A single subscription for all LEDS resources of a specific SourceConfig that
 * fetches updates from a Locality Endpoint Discovery Service.
 * Multiple subscriptions with the same LEDS collection name can use a single
 * subscription.
 */
class LedsSubscription
    : private Envoy::Config::SubscriptionBase<envoy::config::endpoint::v3::LbEndpoint>,
      private Logger::Loggable<Logger::Id::upstream> {
public:
  using UpdateCb = std::function<void()>;
  using LbEndpointsMap = absl::flat_hash_map<std::string, envoy::config::endpoint::v3::LbEndpoint>;

  LedsSubscription(const envoy::config::endpoint::v3::LedsClusterLocalityConfig& leds_config,
                   const std::string& cluster_name,
                   Server::Configuration::TransportSocketFactoryContext& factory_context,
                   Stats::Scope& stats_scope, const UpdateCb& callback);

  // Returns the map between registered LEDS resource names and their endpoints data.
  const LbEndpointsMap& getEndpointsMap() const { return endpoints_map_; }

  // Returns true iff the endpoints were updated.
  bool isUpdated() const { return initial_update_attempt_complete_; }

private:
  // Config::SubscriptionCallbacks
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>&,
                              const std::string&) override {
    return absl::OkStatus();
  }
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string&) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;

  const LocalInfo::LocalInfo& local_info_;
  const std::string cluster_name_;
  // LEDS stats scope must outlive the subscription.
  Stats::ScopeSharedPtr stats_scope_;
  LedsStats stats_;
  // A map between a LEDS resource name to the LbEndpoint data.
  LbEndpointsMap endpoints_map_;
  // A callback function activated after an update is received (either successful or
  // unsuccessful).
  const UpdateCb callback_;
  // Once the endpoints of the locality are updated, it is considered active.
  bool initial_update_attempt_complete_{false};
  Config::SubscriptionPtr subscription_;
};

using LedsSubscriptionPtr = std::unique_ptr<LedsSubscription>;

} // namespace Upstream
} // namespace Envoy

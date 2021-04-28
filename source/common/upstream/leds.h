#pragma once

#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.validate.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/local_info/local_info.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/locality.h"

#include "common/config/subscription_base.h"
#include "common/config/xds_resource.h"
#include "common/upstream/cluster_factory_impl.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/clusters/well_known_names.h"

namespace Envoy {
namespace Upstream {

/**
 * LedsConfigHash and LedsConfigEqualTo are used to allow the LedsClusterLocalityConfig proto to be
 * used as a flat_hash_map key.
 */
struct LedsConfigHash {
  size_t
  operator()(const envoy::config::endpoint::v3::LedsClusterLocalityConfig& health_check) const {
    return MessageUtil::hash(health_check);
  }
};

struct LedsConfigEqualTo {
  bool operator()(const envoy::config::endpoint::v3::LedsClusterLocalityConfig& lhs,
                  const envoy::config::endpoint::v3::LedsClusterLocalityConfig& rhs) const {
    return Protobuf::util::MessageDifferencer::Equals(lhs, rhs);
  }
};

/*
 * This is a single subscription for all LEDS resources of a specific SourceConfig that
 * fetches updates from a Locality Endpoint DiscoveryService.
 * On top of this subscription there can be multiple registered leds collection names.
 */
class LedsSubscription
    : private Envoy::Config::SubscriptionBase<envoy::config::endpoint::v3::LbEndpoint>,
      private Logger::Loggable<Logger::Id::upstream> {
public:
  using UpdateCb = std::function<void()>;

  LedsSubscription(const envoy::config::endpoint::v3::LedsClusterLocalityConfig& leds_config,
                   const std::string& cluster_name,
                   Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
                   /*Stats::Scope& stats_scope,*/ UpdateCb callback);

  // Fetch all the endpoints in the locality subscription.
  const std::list<envoy::config::endpoint::v3::LbEndpoint>& getEndpoints() const {
    return endpoints_;
  }

  // Returns true iff the endpoints were updated.
  bool isActive() const { return active_; }

private:
  // Config::SubscriptionCallbacks
  void onConfigUpdate(const std::vector<Config::DecodedResourceRef>&, const std::string&) override {
    // TODO(adisuissa): Complete this once LEDS should be used in SotW mode.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string&) override;
  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;

  bool validateUpdateSize(int num_resources);

  Config::SubscriptionPtr subscription_;
  const LocalInfo::LocalInfo& local_info_;
  const std::string cluster_name_;
  Stats::Scope& stats_scope_;
  // Holds the current list of LbEndpoints.
  std::list<envoy::config::endpoint::v3::LbEndpoint> endpoints_;
  // A map between a LEDS LbEndpoint resource name to its location in the endpoints_ list.
  absl::flat_hash_map<std::string, std::list<envoy::config::endpoint::v3::LbEndpoint>::iterator>
      endpoint_entry_map_;
  // A callback function activated after an update is received (either successfully or
  // unsuccessfully).
  UpdateCb callback_;
  // Once the endpoints of the locality are updated, it is considered active.
  bool active_{false};
};

using LedsSubscriptionPtr = std::unique_ptr<LedsSubscription>;

} // namespace Upstream
} // namespace Envoy

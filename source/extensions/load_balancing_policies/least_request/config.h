#pragma once

#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request.pb.h"
#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/load_balancing_policies/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace LeastRequest {

using LeastRequestLbProto =
    envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest;
using ClusterProto = envoy::config::cluster::v3::Cluster;
using LegacyLeastRequestLbProto = ClusterProto::LeastRequestLbConfig;

/**
 * Load balancer config that used to wrap the legacy least request config.
 */
class LegacyLeastRequestLbConfig : public Upstream::LoadBalancerConfig {
public:
  LegacyLeastRequestLbConfig(const ClusterProto& cluster);

  OptRef<const LegacyLeastRequestLbProto> lbConfig() const {
    if (lb_config_.has_value()) {
      return lb_config_.value();
    }
    return {};
  };

private:
  absl::optional<LegacyLeastRequestLbProto> lb_config_;
};

/**
 * Load balancer config that used to wrap the least request config.
 */
class TypedLeastRequestLbConfig : public Upstream::LoadBalancerConfig {
public:
  TypedLeastRequestLbConfig(const LeastRequestLbProto& lb_config);

  const LeastRequestLbProto lb_config_;
};

struct LeastRequestCreator : public Logger::Loggable<Logger::Id::upstream> {
  Upstream::LoadBalancerPtr operator()(Upstream::LoadBalancerParams params,
                                       OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                       const Upstream::ClusterInfo& cluster_info,
                                       const Upstream::PrioritySet& priority_set,
                                       Runtime::Loader& runtime, Random::RandomGenerator& random,
                                       TimeSource& time_source);
};

class Factory : public Common::FactoryBase<LeastRequestLbProto, LeastRequestCreator> {
public:
  Factory() : FactoryBase("envoy.load_balancing_policies.least_request") {}

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext&,
             const Protobuf::Message& config) override {
    ASSERT(dynamic_cast<const LeastRequestLbProto*>(&config) != nullptr);
    const LeastRequestLbProto& typed_config = dynamic_cast<const LeastRequestLbProto&>(config);
    // TODO(wbocode): to merge the legacy and typed config and related constructors into one.
    return Upstream::LoadBalancerConfigPtr{new TypedLeastRequestLbConfig(typed_config)};
  }

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadLegacy(Server::Configuration::ServerFactoryContext&, const ClusterProto& cluster) override {
    return Upstream::LoadBalancerConfigPtr{new LegacyLeastRequestLbConfig(cluster)};
  }
};

DECLARE_FACTORY(Factory);

} // namespace LeastRequest
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy

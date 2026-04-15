#pragma once

#include <memory>

#include "envoy/extensions/load_balancing_policies/original_dst/v3/original_dst.pb.h"
#include "envoy/extensions/load_balancing_policies/original_dst/v3/original_dst.pb.validate.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/upstream/load_balancer_factory_base.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace OriginalDst {

using OriginalDstLbProto =
    envoy::extensions::load_balancing_policies::original_dst::v3::OriginalDst;
using ClusterProto = envoy::config::cluster::v3::Cluster;

class OriginalDstLbConfig : public Upstream::LoadBalancerConfig {
public:
  OriginalDstLbConfig(const OriginalDstLbProto& config)
      : use_http_header_(config.use_http_header()),
        http_header_name_(config.http_header_name()),
        upstream_port_override_(config.has_upstream_port_override()
                                    ? absl::optional<uint32_t>(config.upstream_port_override().value())
                                    : absl::nullopt),
        has_metadata_key_(config.has_metadata_key()),
        metadata_key_(config.metadata_key()) {}

  OriginalDstLbConfig(const ClusterProto& cluster)
      : use_http_header_(cluster.original_dst_lb_config().use_http_header()),
        http_header_name_(cluster.original_dst_lb_config().http_header_name()),
        upstream_port_override_(
            cluster.original_dst_lb_config().has_upstream_port_override()
                ? absl::optional<uint32_t>(
                      cluster.original_dst_lb_config().upstream_port_override().value())
                : absl::nullopt),
        has_metadata_key_(cluster.original_dst_lb_config().has_metadata_key()),
        metadata_key_(cluster.original_dst_lb_config().metadata_key()) {}

  bool useHttpHeader() const { return use_http_header_; }
  const std::string& httpHeaderName() const { return http_header_name_; }
  absl::optional<uint32_t> upstreamPortOverride() const { return upstream_port_override_; }
  bool hasMetadataKey() const { return has_metadata_key_; }
  const envoy::type::metadata::v3::MetadataKey& metadataKey() const { return metadata_key_; }

private:
  const bool use_http_header_;
  const std::string http_header_name_;
  const absl::optional<uint32_t> upstream_port_override_;
  const bool has_metadata_key_;
  const envoy::type::metadata::v3::MetadataKey metadata_key_;
};

class Factory : public Upstream::TypedLoadBalancerFactoryBase<OriginalDstLbProto> {
public:
  Factory() : TypedLoadBalancerFactoryBase("envoy.load_balancing_policies.original_dst") {}

  Upstream::ThreadAwareLoadBalancerPtr create(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                              const Upstream::ClusterInfo& cluster_info,
                                              const Upstream::PrioritySet& priority_set,
                                              Runtime::Loader& runtime,
                                              Random::RandomGenerator& random,
                                              TimeSource& time_source) override;

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext&,
             const Protobuf::Message& config) override {
    ASSERT(dynamic_cast<const OriginalDstLbProto*>(&config) != nullptr);
    const auto& typed_config = dynamic_cast<const OriginalDstLbProto&>(config);
    return std::make_unique<OriginalDstLbConfig>(typed_config);
  }

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadLegacy(Server::Configuration::ServerFactoryContext&,
             const ClusterProto& cluster) override {
    return std::make_unique<OriginalDstLbConfig>(cluster);
  }
};

DECLARE_FACTORY(Factory);

} // namespace OriginalDst
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy

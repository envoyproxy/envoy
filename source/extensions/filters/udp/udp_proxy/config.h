#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/udp/udp_proxy/udp_proxy_filter.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

class UdpProxyFilterConfigImpl : public UdpProxyFilterConfig, Logger::Loggable<Logger::Id::config> {
public:
  UdpProxyFilterConfigImpl(
      Server::Configuration::ListenerFactoryContext& context,
      const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig& config);

  // UdpProxyFilterConfig
  const std::string route(const Network::Address::Instance& destination_address,
                          const Network::Address::Instance& source_address) const override {
    return router_->route(destination_address, source_address);
  }
  const std::vector<std::string>& allClusterNames() const override {
    return router_->allClusterNames();
  }
  Upstream::ClusterManager& clusterManager() const override { return cluster_manager_; }
  std::chrono::milliseconds sessionTimeout() const override { return session_timeout_; }
  bool usingOriginalSrcIp() const override { return use_original_src_ip_; }
  bool usingPerPacketLoadBalancing() const override { return use_per_packet_load_balancing_; }
  const Udp::HashPolicy* hashPolicy() const override { return hash_policy_.get(); }
  UdpProxyDownstreamStats& stats() const override { return stats_; }
  TimeSource& timeSource() const override { return time_source_; }
  Random::RandomGenerator& randomGenerator() const override { return random_; }
  const Network::ResolvedUdpSocketConfig& upstreamSocketConfig() const override {
    return upstream_socket_config_;
  }
  const std::vector<AccessLog::InstanceSharedPtr>& sessionAccessLogs() const override {
    return session_access_logs_;
  }
  const std::vector<AccessLog::InstanceSharedPtr>& proxyAccessLogs() const override {
    return proxy_access_logs_;
  }

private:
  static UdpProxyDownstreamStats generateStats(const std::string& stat_prefix,
                                               Stats::Scope& scope) {
    const auto final_prefix = absl::StrCat("udp.", stat_prefix);
    return {ALL_UDP_PROXY_DOWNSTREAM_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                           POOL_GAUGE_PREFIX(scope, final_prefix))};
  }

  Upstream::ClusterManager& cluster_manager_;
  TimeSource& time_source_;
  Router::RouterConstSharedPtr router_;
  const std::chrono::milliseconds session_timeout_;
  const bool use_original_src_ip_;
  const bool use_per_packet_load_balancing_;
  std::unique_ptr<const HashPolicyImpl> hash_policy_;
  mutable UdpProxyDownstreamStats stats_;
  const Network::ResolvedUdpSocketConfig upstream_socket_config_;
  std::vector<AccessLog::InstanceSharedPtr> session_access_logs_;
  std::vector<AccessLog::InstanceSharedPtr> proxy_access_logs_;
  Random::RandomGenerator& random_;
};

/**
 * Config registration for the UDP proxy filter. @see NamedUdpListenerFilterConfigFactory.
 */
class UdpProxyFilterConfigFactory
    : public Server::Configuration::NamedUdpListenerFilterConfigFactory {
public:
  // NamedUdpListenerFilterConfigFactory
  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               Server::Configuration::ListenerFactoryContext& context) override {
    auto shared_config = std::make_shared<UdpProxyFilterConfigImpl>(
        context, MessageUtil::downcastAndValidate<
                     const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig&>(
                     config, context.messageValidationVisitor()));
    return [shared_config](Network::UdpListenerFilterManager& filter_manager,
                           Network::UdpReadFilterCallbacks& callbacks) -> void {
      filter_manager.addReadFilter(std::make_unique<UdpProxyFilter>(callbacks, shared_config));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig>();
  }

  std::string name() const override { return "envoy.filters.udp_listener.udp_proxy"; }
};

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

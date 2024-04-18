#pragma once

#include <memory>
#include <string>

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "contrib/envoy/extensions/filters/network/rocketmq_proxy/v3/rocketmq_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/rocketmq_proxy/v3/rocketmq_proxy.pb.validate.h"
#include "contrib/rocketmq_proxy/filters/network/source/conn_manager.h"
#include "contrib/rocketmq_proxy/filters/network/source/router/route_matcher.h"
#include "contrib/rocketmq_proxy/filters/network/source/router/router_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

class RocketmqProxyFilterConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::rocketmq_proxy::v3::RocketmqProxy> {
public:
  RocketmqProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().RocketmqProxy, true) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::rocketmq_proxy::v3::RocketmqProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

class ConfigImpl : public Config, public Router::Config, Logger::Loggable<Logger::Id::config> {
public:
  using RocketmqProxyConfig =
      envoy::extensions::filters::network::rocketmq_proxy::v3::RocketmqProxy;

  ConfigImpl(const RocketmqProxyConfig& config, Server::Configuration::FactoryContext& context);
  ~ConfigImpl() override = default;

  // Config
  RocketmqFilterStats& stats() override { return stats_; }
  Upstream::ClusterManager& clusterManager() override {
    return context_.serverFactoryContext().clusterManager();
  }
  Router::RouterPtr createRouter() override {
    return std::make_unique<Router::RouterImpl>(context_.serverFactoryContext().clusterManager());
  }
  bool developMode() const override { return develop_mode_; }

  std::chrono::milliseconds transientObjectLifeSpan() const override {
    return transient_object_life_span_;
  }

  std::string proxyAddress() override;
  Router::Config& routerConfig() override { return *this; }

  // Router::Config
  Router::RouteConstSharedPtr route(const MessageMetadata& metadata) const override;

private:
  Server::Configuration::FactoryContext& context_;
  const std::string stats_prefix_;
  RocketmqFilterStats stats_;
  Router::RouteMatcherPtr route_matcher_;
  const bool develop_mode_;
  std::chrono::milliseconds transient_object_life_span_;

  static constexpr uint64_t TransientObjectLifeSpan = 30 * 1000;
};

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

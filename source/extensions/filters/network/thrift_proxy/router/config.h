#pragma once

#include "envoy/extensions/filters/network/thrift_proxy/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/router/v3/router.pb.validate.h"

#include "source/extensions/filters/network/thrift_proxy/filters/factory_base.h"
#include "source/extensions/filters/network/thrift_proxy/router/router_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

class RouterFilterConfig
    : public ThriftFilters::FactoryBase<
          envoy::extensions::filters::network::thrift_proxy::router::v3::Router> {
public:
  RouterFilterConfig() : FactoryBase("envoy.filters.thrift.router") {}

private:
  ThriftFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::thrift_proxy::router::v3::Router& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

class ConfigImpl : public Config, Logger::Loggable<Logger::Id::config> {
public:
  ConfigImpl(
      const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& config,
      Server::Configuration::ServerFactoryContext& context, bool validate_clusters_default) {
    absl::optional<Upstream::ClusterManager::ClusterInfoMaps> validation_clusters;
    if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, validate_clusters, validate_clusters_default)) {
      validation_clusters = context.clusterManager().clusters();
    }
    route_matcher_ = std::make_unique<RouteMatcher>(config, validation_clusters, context);
  }

  // Config
  RouteConstSharedPtr route(const MessageMetadata& metadata, uint64_t random_value) const override {
    return route_matcher_->route(metadata, random_value);
  }

private:
  std::unique_ptr<RouteMatcher> route_matcher_;
};

class NullConfigImpl : public Config {
public:
  RouteConstSharedPtr route(const MessageMetadata&, uint64_t) const override { return nullptr; }
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

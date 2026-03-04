#pragma once

#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Server {
namespace Configuration {

using FilterConfig = envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilter;
using RouteConfigProto =
    envoy::extensions::filters::http::dynamic_modules::v3::DynamicModuleFilterPerRoute;

class DynamicModuleConfigFactory
    : public Extensions::HttpFilters::Common::DualFactoryBase<FilterConfig, RouteConfigProto> {
public:
  DynamicModuleConfigFactory() : DualFactoryBase("envoy.extensions.filters.http.dynamic_modules") {}
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const FilterConfig& proto_config,
                                    const std::string& stat_prefix, DualInfo dual_info,
                                    Server::Configuration::ServerFactoryContext& context) override {
    return createFilterFactory(proto_config, stat_prefix, context, dual_info.scope);
  }
  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoWithServerContextTyped(
      const FilterConfig& proto_config, const std::string& stat_prefix,
      Server::Configuration::ServerFactoryContext& context) override;

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactory(const FilterConfig& proto_config, const std::string& stat_prefix,
                      Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope);

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(const RouteConfigProto&,
                                       Server::Configuration::ServerFactoryContext&,
                                       ProtobufMessage::ValidationVisitor&) override;

  std::string name() const override { return "envoy.extensions.filters.http.dynamic_modules"; }

  bool isTerminalFilterByProtoTyped(const FilterConfig& proto_config,
                                    Server::Configuration::ServerFactoryContext&) override {
    return proto_config.terminal_filter();
  }
};
using UpstreamDynamicModuleConfigFactory = DynamicModuleConfigFactory;

} // namespace Configuration
} // namespace Server
} // namespace Envoy

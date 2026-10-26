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
    : public Extensions::HttpFilters::Common::UnifiedFactoryBase<FilterConfig, RouteConfigProto> {
public:
  DynamicModuleConfigFactory()
      : UnifiedFactoryBase("envoy.extensions.filters.http.dynamic_modules") {}
  absl::StatusOr<Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const FilterConfig& proto_config, Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactory(const FilterConfig& proto_config, const std::string& stat_prefix,
                      Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope,
                      OptRef<Init::Manager> init_manager);

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createHttpFilterRouteConfigTyped(const RouteConfigProto&,
                                   Server::Configuration::ServerFactoryContext&,
                                   Server::Configuration::ExtraFactoryContext&) override;

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

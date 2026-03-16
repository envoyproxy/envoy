#pragma once

#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/filters/http/common/factory_base.h"

#include "absl/container/flat_hash_map.h"

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
    return createFilterFactory(proto_config, stat_prefix, context, dual_info.scope,
                               &dual_info.init_manager);
  }
  Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoWithServerContextTyped(
      const FilterConfig& proto_config, const std::string& stat_prefix,
      Server::Configuration::ServerFactoryContext& context) override;

  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactory(const FilterConfig& proto_config, const std::string& stat_prefix,
                      Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope,
                      Init::Manager* init_manager = nullptr);

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(const RouteConfigProto&,
                                       Server::Configuration::ServerFactoryContext&,
                                       ProtobufMessage::ValidationVisitor&) override;

  std::string name() const override { return "envoy.extensions.filters.http.dynamic_modules"; }

  bool isTerminalFilterByProtoTyped(const FilterConfig& proto_config,
                                    Server::Configuration::ServerFactoryContext&) override {
    return proto_config.terminal_filter();
  }

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromRemoteSource(
      const FilterConfig& proto_config,
      const envoy::extensions::dynamic_modules::v3::DynamicModuleConfig& module_config,
      Server::Configuration::ServerFactoryContext& context, Stats::Scope& scope,
      Init::Manager& init_manager);

  // Cache of successfully fetched remote modules, keyed by SHA256.
  // Maps sha256 → absolute file path where the module .so was written.
  // Populated after successful remote fetch, checked before starting new fetches.
  // No mutex needed: factory methods are only called from the main thread.
  absl::flat_hash_map<std::string, std::string> remote_module_cache_;
};
using UpstreamDynamicModuleConfigFactory = DynamicModuleConfigFactory;

} // namespace Configuration
} // namespace Server
} // namespace Envoy

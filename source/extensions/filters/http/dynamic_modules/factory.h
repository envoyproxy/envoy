#pragma once

#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/filters/http/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/common/config/remote_data_fetcher.h"
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

  // Background HTTP fetch state for nack_on_cache_miss mode, keyed by SHA256
  // in background_fetches_ to deduplicate across listeners referencing the same module.
  struct BackgroundFetchState : public Config::DataFetcher::RemoteDataFetcherCallback,
                                public Logger::Loggable<Logger::Id::dynamic_modules> {
    BackgroundFetchState(Upstream::ClusterManager& cm,
                         const envoy::config::core::v3::RemoteDataSource& source,
                         bool do_not_close, bool load_globally);

    // Config::DataFetcher::RemoteDataFetcherCallback
    void onSuccess(const std::string& data) override;
    void onFailure(Config::DataFetcher::FailureReason reason) override;

    Config::DataFetcher::RemoteDataFetcherPtr fetcher_;
    std::string sha256_;
    bool do_not_close_;
    bool load_globally_;
    bool completed_{false};
  };

  // This lives on the factory (rather than a singletonManager singleton) because
  // REGISTER_FACTORY already makes the factory a process-lifetime singleton, so a
  // separate registration would just be boilerplate for the same semantics.
  // Entries are erased in createFilterFactory(), never inside a fetch callback,
  // which means the RemoteDataFetcher is never destroyed while it's still running.
  absl::flat_hash_map<std::string, std::unique_ptr<BackgroundFetchState>> background_fetches_;
};
using UpstreamDynamicModuleConfigFactory = DynamicModuleConfigFactory;

} // namespace Configuration
} // namespace Server
} // namespace Envoy

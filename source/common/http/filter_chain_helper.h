#pragma once

#include <list>

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/filter/config_provider_manager.h"
#include "envoy/http/filter.h"

#include "source/common/common/logger.h"
#include "source/common/filter/config_discovery_impl.h"
#include "source/common/http/dependency_manager.h"

namespace Envoy {
namespace Http {

using FilterConfigProviderManager =
    Filter::FilterConfigProviderManager<Filter::NamedHttpFilterFactoryCb,
                                        Server::Configuration::FactoryContext>;

class FilterChainHelper : Logger::Loggable<Logger::Id::config> {
public:
  using FilterFactoriesList =
      std::list<Filter::FilterConfigProviderPtr<Filter::NamedHttpFilterFactoryCb>>;
  static void createFilterChainForFactories(Http::FilterChainManager& manager,
                                            const FilterFactoriesList& filter_factories);

  static std::shared_ptr<FilterConfigProviderManager>
  createSingletonFilterConfigProviderManager(Server::Configuration::ServerFactoryContext& context);

  FilterChainHelper(FilterConfigProviderManager& filter_config_provider_manager,
                    Server::Configuration::FactoryContext& factory_context,
                    const std::string& stats_prefix)
      : filter_config_provider_manager_(filter_config_provider_manager),
        factory_context_(factory_context), stats_prefix_(stats_prefix) {}

  using FiltersList = Protobuf::RepeatedPtrField<
      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter>;

  void processFilters(const FiltersList& list, const std::string& prefix,
                      const std::string& filter_chain_type, FilterFactoriesList& filter_factories);

private:
  void
  processFilter(const envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&
                    proto_config,
                int i, const std::string& prefix, const std::string& filter_chain_type,
                bool last_filter_in_current_config, FilterFactoriesList& filter_factories,
                DependencyManager& dependency_manager);
  void
  processDynamicFilterConfig(const std::string& name,
                             const envoy::config::core::v3::ExtensionConfigSource& config_discovery,
                             FilterFactoriesList& filter_factories,
                             const std::string& filter_chain_type,
                             bool last_filter_in_current_config);

  FilterConfigProviderManager& filter_config_provider_manager_;
  Server::Configuration::FactoryContext& factory_context_;
  const std::string& stats_prefix_;
};

} // namespace Http
} // namespace Envoy

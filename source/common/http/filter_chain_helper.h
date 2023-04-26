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

using DownstreamFilterConfigProviderManager =
    Filter::FilterConfigProviderManager<Filter::NamedHttpFilterFactoryCb,
                                        Server::Configuration::FactoryContext>;
using UpstreamFilterConfigProviderManager =
    Filter::FilterConfigProviderManager<Filter::NamedHttpFilterFactoryCb,
                                        Server::Configuration::UpstreamHttpFactoryContext>;

class FilterChainUtility : Logger::Loggable<Logger::Id::config> {
public:
  using FilterFactoriesList =
      std::list<Filter::FilterConfigProviderPtr<Filter::NamedHttpFilterFactoryCb>>;
  using FiltersList = Protobuf::RepeatedPtrField<
      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter>;

  static void createFilterChainForFactories(Http::FilterChainManager& manager,
                                            const FilterFactoriesList& filter_factories);

  static std::shared_ptr<DownstreamFilterConfigProviderManager>
  createSingletonDownstreamFilterConfigProviderManager(
      Server::Configuration::ServerFactoryContext& context);

  static std::shared_ptr<UpstreamFilterConfigProviderManager>
  createSingletonUpstreamFilterConfigProviderManager(
      Server::Configuration::ServerFactoryContext& context);

  // Our tooling checks say that throwing exceptions is not allowed in .h files, so work
  // around the fact the template required moving code to the .h file with a
  // static method.
  static void throwError(std::string message);
};

template <class FilterCtx, class NeutralNamedHttpFilterFactory>
class FilterChainHelper : Logger::Loggable<Logger::Id::config> {
public:
  using FilterFactoriesList =
      std::list<Filter::FilterConfigProviderPtr<Filter::NamedHttpFilterFactoryCb>>;
  using FilterConfigProviderManager =
      Filter::FilterConfigProviderManager<Filter::NamedHttpFilterFactoryCb, FilterCtx>;

  FilterChainHelper(FilterConfigProviderManager& filter_config_provider_manager,
                    Server::Configuration::ServerFactoryContext& server_context,
                    FilterCtx& factory_context, const std::string& stats_prefix)
      : filter_config_provider_manager_(filter_config_provider_manager),
        server_context_(server_context), factory_context_(factory_context),
        stats_prefix_(stats_prefix) {}

  using FiltersList = Protobuf::RepeatedPtrField<
      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter>;

  // Process the filters in this filter chain.
  void processFilters(const FiltersList& filters, const std::string& prefix,
                      const std::string& filter_chain_type, FilterFactoriesList& filter_factories) {

    DependencyManager dependency_manager;
    for (int i = 0; i < filters.size(); i++) {
      processFilter(filters[i], i, prefix, filter_chain_type, i == filters.size() - 1,
                    filter_factories, dependency_manager);
    }
    // TODO(auni53): Validate encode dependencies too.
    auto status = dependency_manager.validDecodeDependencies();
    if (!status.ok()) {
      FilterChainUtility::throwError(std::string(status.message()));
    }
  }

private:
  void
  processFilter(const envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&
                    proto_config,
                int i, const std::string& prefix, const std::string& filter_chain_type,
                bool last_filter_in_current_config, FilterFactoriesList& filter_factories,
                DependencyManager& dependency_manager) {
    ENVOY_LOG(debug, "    {} filter #{}", prefix, i);
    if (proto_config.config_type_case() ==
        envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter::
            ConfigTypeCase::kConfigDiscovery) {
      processDynamicFilterConfig(proto_config.name(), proto_config.config_discovery(),
                                 filter_factories, filter_chain_type,
                                 last_filter_in_current_config);
      return;
    }

    // Now see if there is a factory that will accept the config.
    auto* factory = Config::Utility::getAndCheckFactory<NeutralNamedHttpFilterFactory>(
        proto_config, proto_config.is_optional());
    // null pointer returned only when the filter is optional, then skip all the processes.
    if (factory == nullptr) {
      ENVOY_LOG(warn, "Didn't find a registered factory for the optional http filter {}",
                proto_config.name());
      return;
    }
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        proto_config, server_context_.messageValidationVisitor(), *factory);
    Http::FilterFactoryCb callback =
        factory->createFilterFactoryFromProto(*message, stats_prefix_, factory_context_);
    dependency_manager.registerFilter(factory->name(), *factory->dependencies());
    const bool is_terminal = factory->isTerminalFilterByProto(*message, server_context_);
    Config::Utility::validateTerminalFilters(proto_config.name(), factory->name(),
                                             filter_chain_type, is_terminal,
                                             last_filter_in_current_config);
    auto filter_config_provider = filter_config_provider_manager_.createStaticFilterConfigProvider(
        {factory->name(), callback}, proto_config.name());
    ENVOY_LOG(debug, "      name: {}", filter_config_provider->name());
    ENVOY_LOG(debug, "    config: {}",
              MessageUtil::getJsonStringFromMessageOrError(
                  static_cast<const Protobuf::Message&>(proto_config.typed_config())));
    filter_factories.push_back(std::move(filter_config_provider));
  }

  void
  processDynamicFilterConfig(const std::string& name,
                             const envoy::config::core::v3::ExtensionConfigSource& config_discovery,
                             FilterFactoriesList& filter_factories,
                             const std::string& filter_chain_type,
                             bool last_filter_in_current_config) {
    ENVOY_LOG(debug, "      dynamic filter name: {}", name);
    if (config_discovery.apply_default_config_without_warming() &&
        !config_discovery.has_default_config()) {
      FilterChainUtility::throwError(fmt::format(
          "Error: filter config {} applied without warming but has no default config.", name));
    }
    for (const auto& type_url : config_discovery.type_urls()) {
      auto factory_type_url = TypeUtil::typeUrlToDescriptorFullName(type_url);
      auto* factory = Registry::FactoryRegistry<NeutralNamedHttpFilterFactory>::getFactoryByType(
          factory_type_url);
      if (factory == nullptr) {
        FilterChainUtility::throwError(
            fmt::format("Error: no factory found for a required type URL {}.", factory_type_url));
      }
    }

    auto filter_config_provider = filter_config_provider_manager_.createDynamicFilterConfigProvider(
        config_discovery, name, server_context_, factory_context_, last_filter_in_current_config,
        filter_chain_type, nullptr);
    filter_factories.push_back(std::move(filter_config_provider));
  }

  FilterConfigProviderManager& filter_config_provider_manager_;
  Server::Configuration::ServerFactoryContext& server_context_;
  FilterCtx& factory_context_;
  const std::string& stats_prefix_;
};

} // namespace Http
} // namespace Envoy

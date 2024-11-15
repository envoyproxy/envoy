#pragma once

#include <list>

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/filter/config_provider_manager.h"
#include "envoy/http/filter.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/logger.h"
#include "source/common/filter/config_discovery_impl.h"
#include "source/common/http/dependency_manager.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Http {

using DownstreamFilterConfigProviderManager =
    Filter::FilterConfigProviderManager<Filter::HttpFilterFactoryCb,
                                        Server::Configuration::FactoryContext>;
using UpstreamFilterConfigProviderManager =
    Filter::FilterConfigProviderManager<Filter::HttpFilterFactoryCb,
                                        Server::Configuration::UpstreamFactoryContext>;

// Allows graceful handling of missing configuration for ECDS.
class MissingConfigFilter : public Http::PassThroughDecoderFilter {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    decoder_callbacks_->streamInfo().setResponseFlag(
        StreamInfo::CoreResponseFlag::NoFilterConfigFound);
    decoder_callbacks_->sendLocalReply(Http::Code::InternalServerError, EMPTY_STRING, nullptr,
                                       absl::nullopt, EMPTY_STRING);
    return Http::FilterHeadersStatus::StopIteration;
  }
};

static Http::FilterFactoryCb MissingConfigFilterFactory =
    [](Http::FilterChainFactoryCallbacks& cb) {
      cb.addStreamDecoderFilter(std::make_shared<MissingConfigFilter>());
    };

class FilterChainUtility : Logger::Loggable<Logger::Id::config> {
public:
  struct FilterFactoryProvider {
    Filter::FilterConfigProviderPtr<Filter::HttpFilterFactoryCb> provider;
    // If true, this filter is disabled by default and must be explicitly enabled by
    // route configuration.
    bool disabled{};
  };

  using FilterFactoriesList = std::list<FilterFactoryProvider>;
  using FiltersList = Protobuf::RepeatedPtrField<
      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter>;

  static void createFilterChainForFactories(Http::FilterChainManager& manager,
                                            const FilterChainOptions& options,
                                            const FilterFactoriesList& filter_factories);

  static std::shared_ptr<DownstreamFilterConfigProviderManager>
  createSingletonDownstreamFilterConfigProviderManager(
      Server::Configuration::ServerFactoryContext& context);

  static std::shared_ptr<UpstreamFilterConfigProviderManager>
  createSingletonUpstreamFilterConfigProviderManager(
      Server::Configuration::ServerFactoryContext& context);
};

template <class FilterCtx, class NeutralNamedHttpFilterFactory>
class FilterChainHelper : Logger::Loggable<Logger::Id::config> {
public:
  using FilterFactoriesList = FilterChainUtility::FilterFactoriesList;
  using FilterConfigProviderManager =
      Filter::FilterConfigProviderManager<Filter::HttpFilterFactoryCb, FilterCtx>;

  FilterChainHelper(FilterConfigProviderManager& filter_config_provider_manager,
                    Server::Configuration::ServerFactoryContext& server_context,
                    Upstream::ClusterManager& cluster_manager, FilterCtx& factory_context,
                    const std::string& stats_prefix)
      : filter_config_provider_manager_(filter_config_provider_manager),
        server_context_(server_context), cluster_manager_(cluster_manager),
        factory_context_(factory_context), stats_prefix_(stats_prefix) {}

  using FiltersList = Protobuf::RepeatedPtrField<
      envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter>;

  // Process the filters in this filter chain.
  absl::Status processFilters(const FiltersList& filters, const std::string& prefix,
                              const std::string& filter_chain_type,
                              FilterFactoriesList& filter_factories) {

    DependencyManager dependency_manager;
    for (int i = 0; i < filters.size(); i++) {
      absl::Status status =
          processFilter(filters[i], i, prefix, filter_chain_type, i == filters.size() - 1,
                        filter_factories, dependency_manager);
      if (!status.ok()) {
        return status;
      }
    }
    // TODO(auni53): Validate encode dependencies too.
    return dependency_manager.validDecodeDependencies();
  }

private:
  absl::Status
  processFilter(const envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&
                    proto_config,
                int i, const std::string& prefix, const std::string& filter_chain_type,
                bool last_filter_in_current_config, FilterFactoriesList& filter_factories,
                DependencyManager& dependency_manager) {
    ENVOY_LOG(debug, "    {} filter #{}", prefix, i);

    const bool disabled_by_default = proto_config.disabled();

    // Ensure the terminal filter will not be disabled by default. Because the terminal filter must
    // be the last filter in the chain (ensured by 'validateTerminalFilters') and terminal flag of
    // dynamic filter could not determined and may changed at runtime, so we check the last filter
    // flag here as alternative.
    if (last_filter_in_current_config && disabled_by_default) {
      return absl::InvalidArgumentError(fmt::format(
          "Error: the last (terminal) filter ({}) in the chain cannot be disabled by default.",
          proto_config.name()));
    }

    if (proto_config.config_type_case() ==
        envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter::
            ConfigTypeCase::kConfigDiscovery) {
      return processDynamicFilterConfig(proto_config.name(), proto_config.config_discovery(),
                                        filter_factories, filter_chain_type,
                                        last_filter_in_current_config, disabled_by_default);
    }

    // Now see if there is a factory that will accept the config.
    auto* factory = Config::Utility::getAndCheckFactory<NeutralNamedHttpFilterFactory>(
        proto_config, proto_config.is_optional());
    // null pointer returned only when the filter is optional, then skip all the processes.
    if (factory == nullptr) {
      ENVOY_LOG(warn, "Didn't find a registered factory for the optional http filter {}",
                proto_config.name());
      return absl::OkStatus();
    }
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        proto_config, server_context_.messageValidationVisitor(), *factory);
    absl::StatusOr<Http::FilterFactoryCb> callback_or_error =
        factory->createFilterFactoryFromProto(*message, stats_prefix_, factory_context_);
    if (!callback_or_error.status().ok()) {
      return callback_or_error.status();
    }
    dependency_manager.registerFilter(factory->name(), *factory->dependencies());
    const bool is_terminal = factory->isTerminalFilterByProto(*message, server_context_);
    RETURN_IF_NOT_OK(Config::Utility::validateTerminalFilters(proto_config.name(), factory->name(),
                                                              filter_chain_type, is_terminal,
                                                              last_filter_in_current_config));
    auto filter_config_provider = filter_config_provider_manager_.createStaticFilterConfigProvider(
        callback_or_error.value(), proto_config.name());
#ifdef ENVOY_ENABLE_YAML
    ENVOY_LOG(debug, "      name: {}", filter_config_provider->name());
    ENVOY_LOG(debug, "    config: {}",
              MessageUtil::getJsonStringFromMessageOrError(
                  static_cast<const Protobuf::Message&>(proto_config.typed_config())));
#endif
    filter_factories.push_back({std::move(filter_config_provider), disabled_by_default});
    return absl::OkStatus();
  }

  absl::Status
  processDynamicFilterConfig(const std::string& name,
                             const envoy::config::core::v3::ExtensionConfigSource& config_discovery,
                             FilterFactoriesList& filter_factories,
                             const std::string& filter_chain_type,
                             bool last_filter_in_current_config, bool disabled_by_default) {
    ENVOY_LOG(debug, "      dynamic filter name: {}", name);
    if (config_discovery.apply_default_config_without_warming() &&
        !config_discovery.has_default_config()) {
      return absl::InvalidArgumentError(fmt::format(
          "Error: filter config {} applied without warming but has no default config.", name));
    }
    for (const auto& type_url : config_discovery.type_urls()) {
      auto factory_type_url = TypeUtil::typeUrlToDescriptorFullName(type_url);
      auto* factory = Registry::FactoryRegistry<NeutralNamedHttpFilterFactory>::getFactoryByType(
          factory_type_url);
      if (factory == nullptr) {
        return absl::InvalidArgumentError(
            fmt::format("Error: no factory found for a required type URL {}.", factory_type_url));
      }
    }

    auto filter_config_provider = filter_config_provider_manager_.createDynamicFilterConfigProvider(
        config_discovery, name, server_context_, factory_context_, cluster_manager_,
        last_filter_in_current_config, filter_chain_type, nullptr);
    filter_factories.push_back({std::move(filter_config_provider), disabled_by_default});
    return absl::OkStatus();
  }

  FilterConfigProviderManager& filter_config_provider_manager_;
  Server::Configuration::ServerFactoryContext& server_context_;
  Upstream::ClusterManager& cluster_manager_;
  FilterCtx& factory_context_;
  const std::string& stats_prefix_;
};

} // namespace Http
} // namespace Envoy

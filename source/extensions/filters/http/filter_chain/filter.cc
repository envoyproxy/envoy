#include "source/extensions/filters/http/filter_chain/filter.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/config/utility.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FilterChain {

namespace {

constexpr absl::string_view FilterChainName = "envoy.filters.http.filter_chain";

// Helper to process filter config and create filter factories
absl::StatusOr<FilterFactoriesVector> createFilterFactoriesFromConfig(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
    Server::Configuration::ServerFactoryContext& context, const std::string& stats_prefix) {
  FilterFactoriesVector filter_factories;
  filter_factories.reserve(proto_config.filters_size());

  for (const auto& filter_config : proto_config.filters()) {
    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
            filter_config);
    if (factory.name() == FilterChainName) {
      return absl::InvalidArgumentError("FilterChain filter cannot be configured recursively.");
    }

    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        filter_config, context.messageValidationVisitor(), factory);
    auto callback_or_error =
        factory.createHttpFilterFactoryFromProto(*message, stats_prefix, context);
    RETURN_IF_NOT_OK_REF(callback_or_error.status());

    auto filter_config_provider =
        Http::FilterChainUtility::createSingletonDownstreamFilterConfigProviderManager(context)
            ->createStaticFilterConfigProvider(callback_or_error.value(), filter_config.name());
    filter_factories.push_back({std::move(filter_config_provider)});
  }
  return filter_factories;
}

// Helper to process filter config and create filter factories
absl::StatusOr<FilterFactoriesVector> createFilterFactoriesFromConfig(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
    Server::Configuration::FactoryContext& context, const std::string& stats_prefix) {
  FilterFactoriesVector filter_factories;
  filter_factories.reserve(proto_config.filters_size());

  for (const auto& filter_config : proto_config.filters()) {
    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
            filter_config);
    if (factory.name() == FilterChainName) {
      return absl::InvalidArgumentError("FilterChain filter cannot be configured recursively.");
    }

    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        filter_config, context.messageValidationVisitor(), factory);
    auto callback_or_error = factory.createFilterFactoryFromProto(*message, stats_prefix, context);
    RETURN_IF_NOT_OK_REF(callback_or_error.status());

    auto filter_config_provider =
        Http::FilterChainUtility::createSingletonDownstreamFilterConfigProviderManager(
            context.serverFactoryContext())
            ->createStaticFilterConfigProvider(std::move(callback_or_error.value()),
                                               filter_config.name());
    filter_factories.push_back({std::move(filter_config_provider)});
  }
  return filter_factories;
}

} // namespace

FilterChain::FilterChain(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
    Server::Configuration::ServerFactoryContext& context, const std::string& stats_prefix,
    absl::Status& creation_status) {
  auto filter_factories_or = createFilterFactoriesFromConfig(proto_config, context, stats_prefix);
  SET_AND_RETURN_IF_NOT_OK(filter_factories_or.status(), creation_status);
  filter_factories_ = std::move(filter_factories_or.value());
  for (const auto& factory : filter_factories_) {
    filters_.insert(factory->name());
  }
}

FilterChain::FilterChain(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
    Server::Configuration::FactoryContext& context, const std::string& stats_prefix,
    absl::Status& creation_status) {
  auto filter_factories_or = createFilterFactoriesFromConfig(proto_config, context, stats_prefix);
  SET_AND_RETURN_IF_NOT_OK(filter_factories_or.status(), creation_status);
  filter_factories_ = std::move(filter_factories_or.value());
  for (const auto& factory : filter_factories_) {
    filters_.insert(factory->name());
  }
}

FilterChainPerRouteConfig::FilterChainPerRouteConfig(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute&
        proto_config,
    Server::Configuration::ServerFactoryContext& context, const std::string& stats_prefix,
    absl::Status& creation_status) {
  filter_chain_ = std::make_shared<FilterChain>(proto_config.filter_chain(), context, stats_prefix,
                                                creation_status);
}

FilterChainConfig::FilterChainConfig(const FilterChainConfigProto& proto_config,
                                     Server::Configuration::FactoryContext& context,
                                     const std::string& stats_prefix, absl::Status& creation_status)
    : stats_(createStats(stats_prefix, context.scope())) {
  if (proto_config.has_default_filter_chain()) {
    default_filter_chain_ = std::make_shared<FilterChain>(proto_config.default_filter_chain(),
                                                          context, stats_prefix, creation_status);
  }
}

} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

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

// Helper to process filter config and create filter factories
Http::FilterChainUtility::FilterFactoriesList createFilterFactoriesFromConfig(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
    Server::Configuration::ServerFactoryContext& context, const std::string& stats_prefix) {
  Http::FilterChainUtility::FilterFactoriesList filter_factories;
  filter_factories.reserve(proto_config.filters_size());

  for (const auto& filter_config : proto_config.filters()) {
    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
            filter_config);

    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        filter_config, context.messageValidationVisitor(), factory);
    auto callback_or_error =
        factory.createFilterFactoryFromProtoWithServerContext(*message, stats_prefix, context);

    auto filter_config_provider =
        Http::FilterChainUtility::createSingletonDownstreamFilterConfigProviderManager(context)
            ->createStaticFilterConfigProvider(callback_or_error, filter_config.name());
    filter_factories.push_back({std::move(filter_config_provider), false});
  }
  return filter_factories;
}

// Helper to process filter config and create filter factories
Http::FilterChainUtility::FilterFactoriesList createFilterFactoriesFromConfig(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
    Server::Configuration::FactoryContext& context, const std::string& stats_prefix) {
  Http::FilterChainUtility::FilterFactoriesList filter_factories;
  filter_factories.reserve(proto_config.filters_size());

  for (const auto& filter_config : proto_config.filters()) {
    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
            filter_config);

    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        filter_config, context.messageValidationVisitor(), factory);
    auto callback_or_error = factory.createFilterFactoryFromProto(*message, stats_prefix, context);
    THROW_IF_NOT_OK_REF(callback_or_error.status());

    auto filter_config_provider =
        Http::FilterChainUtility::createSingletonDownstreamFilterConfigProviderManager(
            context.serverFactoryContext())
            ->createStaticFilterConfigProvider(std::move(callback_or_error.value()),
                                               filter_config.name());
    filter_factories.push_back({std::move(filter_config_provider), false});
  }
  return filter_factories;
}

} // namespace

FilterChain::FilterChain(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
    Server::Configuration::ServerFactoryContext& context, const std::string& stats_prefix)
    : filter_factories_(createFilterFactoriesFromConfig(proto_config, context, stats_prefix)) {}

FilterChain::FilterChain(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
    Server::Configuration::FactoryContext& context, const std::string& stats_prefix)
    : filter_factories_(createFilterFactoriesFromConfig(proto_config, context, stats_prefix)) {}

FilterChainPerRouteConfig::FilterChainPerRouteConfig(
    const envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute&
        proto_config,
    Server::Configuration::ServerFactoryContext& context, const std::string& stats_prefix,
    absl::Status& creation_status)
    : filter_chain_name_(proto_config.filter_chain_name()) {

  if (!proto_config.filter_chain_name().empty() == proto_config.has_filter_chain()) {
    creation_status = absl::InvalidArgumentError(
        "One and only one of filter_chain_name or filter_chain must be set");
    return;
  }

  if (proto_config.has_filter_chain()) {
    filter_chain_ =
        std::make_shared<FilterChain>(proto_config.filter_chain(), context, stats_prefix);
  }
}

FilterChainConfig::FilterChainConfig(const FilterChainConfigProto& proto_config,
                                     Server::Configuration::FactoryContext& context,
                                     const std::string& stats_prefix)
    : stats_(createStats(stats_prefix, context.scope())) {
  if (proto_config.has_filter_chain()) {
    default_filter_chain_ =
        std::make_shared<FilterChain>(proto_config.filter_chain(), context, stats_prefix);
  }

  for (const auto& [name, chain] : proto_config.filter_chains()) {
    named_filter_chains_[name] = std::make_shared<FilterChain>(chain, context, stats_prefix);
  }
}

} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

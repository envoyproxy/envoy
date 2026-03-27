#include "source/extensions/filters/http/composite/config.h"

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/filters/http/composite/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

absl::StatusOr<NamedFilterChainFactoryMapSharedPtr>
CompositeFilterFactory::compileNamedFilterChains(
    const envoy::extensions::filters::http::composite::v3::Composite& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  if (config.named_filter_chains().empty()) {
    return nullptr;
  }

  auto named_chains = std::make_shared<NamedFilterChainFactoryMap>();
  for (const auto& [name, filter_chain_config] : config.named_filter_chains()) {
    if (filter_chain_config.typed_config().empty()) {
      return absl::InvalidArgumentError(
          fmt::format("Named filter chain '{}' must contain at least one filter.", name));
    }

    std::vector<Http::FilterFactoryCb> filter_factories;
    filter_factories.reserve(filter_chain_config.typed_config().size());

    for (const auto& filter_config : filter_chain_config.typed_config()) {
      auto& factory =
          Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
              filter_config);
      ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
          filter_config.typed_config(), context.messageValidationVisitor(), factory);
      auto callback_or_status =
          factory.createFilterFactoryFromProto(*message, stats_prefix, context);
      if (!callback_or_status.status().ok()) {
        return absl::InvalidArgumentError(
            fmt::format("Failed to create filter factory for filter '{}' in named filter chain "
                        "'{}': {}",
                        filter_config.name(), name, callback_or_status.status().message()));
      }
      filter_factories.push_back(std::move(callback_or_status.value()));
    }
    (*named_chains)[name] = std::move(filter_factories);
  }

  return named_chains;
}

absl::StatusOr<Http::FilterFactoryCb> CompositeFilterFactory::createFilterFactoryFromProto(
    const Protobuf::Message& config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::http::composite::v3::Composite&>(
      config, context.messageValidationVisitor());

  // Compile named filter chains with FactoryContext access.
  auto named_chains_or_error = compileNamedFilterChains(proto_config, stats_prefix, context);
  RETURN_IF_NOT_OK(named_chains_or_error.status());
  auto named_chains = std::move(named_chains_or_error.value());

  const auto& prefix = stats_prefix + "composite.";
  auto stats = std::make_shared<FilterStats>(
      FilterStats{ALL_COMPOSITE_FILTER_STATS(POOL_COUNTER_PREFIX(context.scope(), prefix))});

  return [stats, named_chains](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<Filter>(*stats, callbacks.dispatcher(), false /* is_upstream */,
                                           named_chains);
    callbacks.addStreamFilter(filter);
    callbacks.addAccessLogHandler(filter);
  };
}

absl::StatusOr<Http::FilterFactoryCb> CompositeFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::composite::v3::Composite&,
    const std::string& stat_prefix, DualInfo dual_info,
    Server::Configuration::ServerFactoryContext&) {
  // This method is called for upstream filters.
  // Named filter chains are not supported for upstream filters.
  const auto& prefix = stat_prefix + "composite.";
  auto stats = std::make_shared<FilterStats>(
      FilterStats{ALL_COMPOSITE_FILTER_STATS(POOL_COUNTER_PREFIX(dual_info.scope, prefix))});

  return [stats, dual_info](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<Filter>(*stats, callbacks.dispatcher(), dual_info.is_upstream);
    callbacks.addStreamFilter(filter);
    callbacks.addAccessLogHandler(filter);
  };
}

/**
 * Static registration for the composite filter. @see RegisterFactory.
 */
REGISTER_FACTORY(CompositeFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamCompositeFilterFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

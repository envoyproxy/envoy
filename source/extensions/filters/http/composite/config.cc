#include "source/extensions/filters/http/composite/config.h"

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/extensions/filters/http/composite/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

class MatchTreeValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Envoy::Http::HttpMatchingData> {
public:
  absl::Status
  performDataInputValidation(const Matcher::DataInputFactory<Envoy::Http::HttpMatchingData>&,
                             absl::string_view) override {
    return absl::OkStatus();
  }
};

absl::StatusOr<Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData>>
createMatcherTree(const xds::type::matcher::v3::Matcher& matcher_config,
                  Envoy::Http::Matching::HttpFilterActionContext& action_context) {
  MatchTreeValidationVisitor match_tree_validation_visitor;
  Matcher::MatchTreeFactory<Envoy::Http::HttpMatchingData,
                            Envoy::Http::Matching::HttpFilterActionContext>
      matcher_factory(action_context, action_context.server_factory_context_.value(),
                      match_tree_validation_visitor);
  auto factory_cb = matcher_factory.create(matcher_config);
  if (!match_tree_validation_visitor.errors().empty()) {
    return absl::InvalidArgumentError(
        fmt::format("requirement violation while creating match tree: {}",
                    match_tree_validation_visitor.errors()[0]));
  }
  return factory_cb();
}

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

  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree = nullptr;
  if (proto_config.has_matcher()) {
    Envoy::Http::Matching::HttpFilterActionContext action_context{
        .is_downstream_ = true,
        .stat_prefix_ = stats_prefix,
        .factory_context_ = context,
        .upstream_factory_context_ = absl::nullopt,
        .server_factory_context_ = context.serverFactoryContext()};
    auto match_tree_or_error = createMatcherTree(proto_config.matcher(), action_context);
    RETURN_IF_NOT_OK(match_tree_or_error.status());
    match_tree = std::move(match_tree_or_error.value());
  }

  const auto& prefix = stats_prefix + "composite.";
  auto stats = std::make_shared<FilterStats>(
      FilterStats{ALL_COMPOSITE_FILTER_STATS(POOL_COUNTER_PREFIX(context.scope(), prefix))});

  return [stats, named_chains, match_tree](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<Filter>(*stats, callbacks.dispatcher(), false /* is_upstream */,
                                           match_tree, named_chains);
    callbacks.addStreamFilter(filter);
    callbacks.addAccessLogHandler(filter);
  };
}

absl::StatusOr<Envoy::Http::FilterFactoryCb> CompositeFilterFactory::createFilterFactoryFromProto(
    const Protobuf::Message& config, const std::string& stats_prefix,
    Server::Configuration::UpstreamFactoryContext& context) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::http::composite::v3::Composite&>(
      config, context.serverFactoryContext().messageValidationVisitor());

  Matcher::MatchTreeSharedPtr<Envoy::Http::HttpMatchingData> match_tree = nullptr;
  if (proto_config.has_matcher()) {
    Envoy::Http::Matching::HttpFilterActionContext action_context{
        .is_downstream_ = false,
        .stat_prefix_ = stats_prefix,
        .factory_context_ = absl::nullopt,
        .upstream_factory_context_ = context,
        .server_factory_context_ = context.serverFactoryContext()};

    auto match_tree_or_error = createMatcherTree(proto_config.matcher(), action_context);
    RETURN_IF_NOT_OK(match_tree_or_error.status());
    match_tree = std::move(match_tree_or_error.value());
  }

  // This method is called for upstream filters.
  // Named filter chains are not supported for upstream filters.
  const auto& prefix = stats_prefix + "composite.";
  auto stats = std::make_shared<FilterStats>(
      FilterStats{ALL_COMPOSITE_FILTER_STATS(POOL_COUNTER_PREFIX(context.scope(), prefix))});

  return [stats, match_tree](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<Filter>(*stats, callbacks.dispatcher(), true /* is_upstream */,
                                           match_tree, nullptr);
    callbacks.addStreamFilter(filter);
    callbacks.addAccessLogHandler(filter);
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
CompositeFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::composite::v3::CompositePerRoute& config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {

  Envoy::Http::Matching::HttpFilterActionContext action_context{
      .is_downstream_ = true,
      .stat_prefix_ =
          fmt::format("http.{}.", context.scope().symbolTable().toString(context.scope().prefix())),
      .factory_context_ = absl::nullopt,
      .upstream_factory_context_ = absl::nullopt,
      .server_factory_context_ = context};

  auto match_tree_or_error = createMatcherTree(config.matcher(), action_context);
  RETURN_IF_NOT_OK(match_tree_or_error.status());
  auto match_tree = std::move(match_tree_or_error.value());
  if (match_tree == nullptr) {
    return nullptr;
  }
  return std::make_shared<CompositePerRouteConfig>(match_tree);
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

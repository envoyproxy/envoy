#include "source/extensions/filters/http/header_mutation/config.h"

#include <memory>

#include "envoy/registry/registry.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {

namespace {

absl::StatusOr<Formatter::CommandParserPtrVector>
parseFormatters(const MutationsProto& config, Server::Configuration::ServerFactoryContext& context,
                Server::Configuration::ExtraFactoryContext& extra_context) {
  if (config.formatters().empty()) {
    return Formatter::CommandParserPtrVector{};
  }
  Server::GenericFactoryContextImpl generic_context(
      context, extra_context.scope, extra_context.visitor, extra_context.init_manager);
  return Formatter::SubstitutionFormatStringUtils::parseFormatters(config.formatters(),
                                                                   generic_context);
}

} // namespace

absl::StatusOr<Http::FilterFactoryCb>
HeaderMutationFactoryConfig::createHttpFilterFactoryFromProtoTyped(
    const ProtoConfig& config, Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {
  auto command_parsers = parseFormatters(config.mutations(), context, extra_context);
  RETURN_IF_NOT_OK_REF(command_parsers.status());
  absl::Status creation_status = absl::OkStatus();
  auto filter_config =
      std::make_shared<HeaderMutationConfig>(config, context, *command_parsers, creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<HeaderMutation>(filter_config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
HeaderMutationFactoryConfig::createHttpFilterRouteConfigTyped(
    const PerRouteProtoConfig& proto_config, Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {
  auto command_parsers = parseFormatters(proto_config.mutations(), context, extra_context);
  RETURN_IF_NOT_OK_REF(command_parsers.status());
  absl::Status creation_status = absl::OkStatus();
  auto route_config = std::make_shared<PerRouteHeaderMutation>(proto_config, context,
                                                               *command_parsers, creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);
  return route_config;
}

using UpstreamHeaderMutationFactoryConfig = HeaderMutationFactoryConfig;

REGISTER_FACTORY(HeaderMutationFactoryConfig, Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamHeaderMutationFactoryConfig,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

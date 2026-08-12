#include "source/extensions/filters/http/a2a/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/a2a/a2a_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace A2a {

absl::StatusOr<Http::FilterFactoryCb> A2aFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::a2a::v3::A2a& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  // This filter only uses the server factory context, so delegate to the server-context variant.
  Server::Configuration::ExtraFactoryContext extra_context{context.messageValidationVisitor(),
                                                           stats_prefix};
  return createHttpFilterFactoryFromProtoTyped(proto_config, context.serverFactoryContext(),
                                               extra_context);
}

absl::StatusOr<Http::FilterFactoryCb> A2aFilterConfigFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::a2a::v3::A2a& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {

  // Use the server factory context to access the root scope for stats.
  auto config =
      std::make_shared<A2aFilterConfig>(proto_config, extra_context.stats_prefix, context.scope());

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<A2aFilter>(config));
  };
}

/**
 * Static registration for the A2A filter. @see RegisterFactory.
 */
REGISTER_FACTORY(A2aFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

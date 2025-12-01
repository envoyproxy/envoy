#include "source/extensions/filters/http/connection_pool_cardinality/config.h"

#include "source/extensions/filters/http/connection_pool_cardinality/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectionPoolCardinality {

Envoy::Http::FilterFactoryCb
ConnectionPoolCardinalityFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::connection_pool_cardinality::v3::
        ConnectionPoolCardinalityConfig& config,
    const std::string& /*stats_prefix*/, Server::Configuration::FactoryContext& context) {

  uint32_t connection_pool_count = std::max(1U, config.connection_pool_count());
  return [connection_pool_count, &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<Filter>(
        connection_pool_count, context.serverFactoryContext().api().randomGenerator()));
  };
}

REGISTER_FACTORY(ConnectionPoolCardinalityFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace ConnectionPoolCardinality
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

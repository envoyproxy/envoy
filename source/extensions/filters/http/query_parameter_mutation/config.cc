#include "source/extensions/filters/http/query_parameter_mutation/config.h"

#include <memory>

#include "source/extensions/filters/http/query_parameter_mutation/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace QueryParameterMutation {

Http::FilterFactoryCb
Factory::createFilterFactoryFromProtoTyped(const FilterConfigProto& proto, const std::string&,
                                           Server::Configuration::FactoryContext&) {
  auto config = std::make_shared<Config>(proto);
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<Filter>(config));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
Factory::createRouteSpecificFilterConfigTyped(const FilterConfigProto& proto_config,
                                              Server::Configuration::ServerFactoryContext&,
                                              ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<Config>(proto_config);
}

REGISTER_FACTORY(Factory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace QueryParameterMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

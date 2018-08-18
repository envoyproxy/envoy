#include "test/integration/add_trailers_filter_config.h"

#include "envoy/registry/registry.h"

#include "test/integration/add_trailers_filter.h"

namespace Envoy {
Http::FilterFactoryCb
AddTrailersStreamFilterConfig::createFilter(const std::string&,
                                            Server::Configuration::FactoryContext&) {
  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<::Envoy::AddTrailersStreamFilter>());
  };
}

// perform static registration
static Registry::RegisterFactory<AddTrailersStreamFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
} // namespace Envoy

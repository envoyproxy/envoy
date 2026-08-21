#include "contrib/peak_ewma/filters/http/source/peak_ewma_filter_config.h"

#include "envoy/registry/registry.h"

#include "contrib/peak_ewma/filters/http/source/peak_ewma_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PeakEwma {

absl::StatusOr<Http::FilterFactoryCb>
PeakEwmaFilterConfigFactory::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::peak_ewma::v3alpha::PeakEwmaConfig&,
    Server::Configuration::ServerFactoryContext&, Server::Configuration::ExtraFactoryContext&) {
  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<PeakEwmaRttFilter>());
  };
}

REGISTER_FACTORY(PeakEwmaFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace PeakEwma
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

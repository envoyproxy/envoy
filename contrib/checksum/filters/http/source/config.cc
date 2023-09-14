#include "contrib/checksum/filters/http/source/config.h"

#include <cstdint>
#include <string>

#include "envoy/registry/registry.h"

#include "contrib/checksum/filters/http/source/checksum_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ChecksumFilter {

Http::FilterFactoryCb ChecksumFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::checksum::v3alpha::ChecksumConfig& proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {
  ChecksumFilterConfigSharedPtr filter_config(new ChecksumFilterConfig(proto_config));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<ChecksumFilter>(filter_config));
  };
}

/**
 * Static registration for the checksum filter (sha256). @see RegisterFactory.
 */
REGISTER_FACTORY(ChecksumFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace ChecksumFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

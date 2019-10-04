#include "extensions/filters/http/alpn/config.h"

#include "extensions/filters/http/alpn/alpn_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Alpn {

Http::FilterFactoryCb AlpnConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::alpn::v2alpha::FilterConfig& proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {
  AlpnFilterConfigSharedPtr filter_config{std::make_shared<AlpnFilterConfig>(proto_config)};
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_unique<AlpnFilter>(filter_config));
  };
}

/**
 * Static registration for the alpn override filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AlpnConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Alpn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

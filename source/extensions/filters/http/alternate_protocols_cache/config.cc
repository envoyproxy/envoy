#include "source/extensions/filters/http/alternate_protocols_cache/config.h"

#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"
#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.validate.h"

#include "source/common/http/alternate_protocols_cache_manager_impl.h"
#include "source/extensions/filters/http/alternate_protocols_cache/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AlternateProtocolsCache {

Http::FilterFactoryCb AlternateProtocolsCacheFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig&
        proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  Http::AlternateProtocolsCacheManagerFactoryImpl alternate_protocol_cache_manager_factory(
      context.singletonManager(), context.dispatcher().timeSource(), context.threadLocal());
  FilterConfigSharedPtr filter_config(std::make_shared<FilterConfig>(
      proto_config, alternate_protocol_cache_manager_factory, context.dispatcher().timeSource()));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamEncoderFilter(std::make_shared<Filter>(filter_config));
  };
}

/**
 * Static registration for the alternate protocols cache filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AlternateProtocolsCacheFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AlternateProtocolsCache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

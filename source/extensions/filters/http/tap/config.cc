#include "extensions/filters/http/tap/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/http/tap/tap_config_impl.h"
#include "extensions/filters/http/tap/tap_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

class HttpTapConfigFactoryImpl : public Extensions::Common::Tap::TapConfigFactory {
public:
  // TapConfigFactory
  Extensions::Common::Tap::TapConfigSharedPtr
  createConfigFromProto(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                        Extensions::Common::Tap::Sink* admin_streamer) override {
    return std::make_shared<HttpTapConfigImpl>(std::move(proto_config), admin_streamer);
  }
};

Http::FilterFactoryCb TapFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::tap::v2alpha::Tap& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  FilterConfigSharedPtr filter_config(new FilterConfigImpl(
      proto_config, stats_prefix, std::make_unique<HttpTapConfigFactoryImpl>(), context.scope(),
      context.admin(), context.singletonManager(), context.threadLocal(), context.dispatcher()));
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<Filter>(filter_config);
    callbacks.addStreamFilter(filter);
    callbacks.addAccessLogHandler(filter);
  };
}

/**
 * Static registration for the tap filter. @see RegisterFactory.
 */
REGISTER_FACTORY(TapFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

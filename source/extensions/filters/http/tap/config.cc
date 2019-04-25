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
  HttpTapConfigFactoryImpl(Server::Configuration::FactoryContext& context) : context_(context) {}

  // TODO: save factory context here? and give it to tap config? or do it in the base?
  // and deliver it to the cofnig impl and mak the base class of httptapcofngimpl require it!

  // TapConfigFactory
  Extensions::Common::Tap::TapConfigSharedPtr
  createConfigFromProto(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                        Extensions::Common::Tap::Sink* admin_streamer) override {
    return std::make_shared<HttpTapConfigImpl>(std::move(proto_config), admin_streamer,
                                               context_.clusterManager(), context_.scope(),
                                               context_.localInfo());
  }

private:
  Server::Configuration::FactoryContext& context_;
};

Http::FilterFactoryCb TapFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::tap::v2alpha::Tap& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  FilterConfigSharedPtr filter_config(new FilterConfigImpl(
      proto_config, stats_prefix, std::make_unique<HttpTapConfigFactoryImpl>(context),
      context.scope(), context.admin(), context.singletonManager(), context.threadLocal(),
      context.dispatcher()));
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

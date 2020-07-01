#include "extensions/filters/http/router/config.h"

#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.validate.h"

#include "common/router/router.h"
#include "common/router/shadow_writer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouterFilter {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(date_provider);

Http::FilterFactoryCb RouterFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::router::v3::Router& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {
  std::shared_ptr<Http::TlsCachingDateProviderImpl> date_provider =
      context.singletonManager().getTyped<Http::TlsCachingDateProviderImpl>(
          SINGLETON_MANAGER_REGISTERED_NAME(date_provider), [&context] {
            return std::make_shared<Http::TlsCachingDateProviderImpl>(context.dispatcher(),
                                                                      context.threadLocal());
          });

  Router::FilterConfigSharedPtr filter_config(new Router::FilterConfig(
      stat_prefix, context, std::make_unique<Router::ShadowWriterImpl>(context.clusterManager()),
      *date_provider, proto_config));

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<Router::ProdFilter>(*filter_config));
  };
}

/**
 * Static registration for the router filter. @see RegisterFactory.
 */
REGISTER_FACTORY(RouterFilterConfig,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.router"};

} // namespace RouterFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

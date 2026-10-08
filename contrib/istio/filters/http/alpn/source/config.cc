#include "contrib/istio/filters/http/alpn/source/config.h"

#include "source/common/protobuf/message_validator_impl.h"

#include "contrib/istio/filters/http/alpn/source/alpn_filter.h"

using istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig;

namespace Envoy {
namespace Http {
namespace Alpn {
absl::StatusOr<Http::FilterFactoryCb> AlpnConfigFactory::createHttpFilterFactoryFromProtoTyped(
    const FilterConfig& proto_config, Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext&) {
  return createFilterFactory(proto_config, context.clusterManager());
}

Http::FilterFactoryCb
AlpnConfigFactory::createFilterFactory(const FilterConfig& proto_config,
                                       Upstream::ClusterManager& cluster_manager) {
  AlpnFilterConfigSharedPtr filter_config{
      std::make_shared<AlpnFilterConfig>(proto_config, cluster_manager)};
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_unique<AlpnFilter>(filter_config));
  };
}

/**
 * Static registration for the alpn override filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AlpnConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Alpn
} // namespace Http
} // namespace Envoy

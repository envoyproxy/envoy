#include "contrib/istio/filters/http/alpn/source/config.h"

#include "source/common/protobuf/message_validator_impl.h"

#include "contrib/istio/filters/http/alpn/source/alpn_filter.h"

using istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig;

namespace Envoy {
namespace Http {
namespace Alpn {
absl::StatusOr<Http::FilterFactoryCb>
AlpnConfigFactory::createFilterFactoryFromProto(const Protobuf::Message& config, const std::string&,
                                                Server::Configuration::FactoryContext& context) {
  return createFilterFactory(dynamic_cast<const FilterConfig&>(config),
                             context.serverFactoryContext().clusterManager());
}

ProtobufTypes::MessagePtr AlpnConfigFactory::createEmptyConfigProto() {
  return ProtobufTypes::MessagePtr{new FilterConfig};
}

std::string AlpnConfigFactory::name() const { return "istio.alpn"; }

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

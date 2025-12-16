#include "source/extensions/filters/udp/dns_gateway/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/common/synthetic_ip/cache_manager.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsGateway {

Network::UdpListenerFilterFactoryCb DnsGatewayConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, Server::Configuration::ListenerFactoryContext& context) {

  auto config = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::udp::dns_gateway::v3::DnsGateway&>(
      proto_config, context.messageValidationVisitor());

  // Parse cache configuration
  std::chrono::seconds eviction_interval(60);
  std::chrono::seconds default_ttl(300);
  uint32_t max_entries = 10000;

  if (config.has_cache_config()) {
    const auto& cache_config = config.cache_config();
    if (cache_config.has_eviction_interval()) {
      eviction_interval = std::chrono::seconds(cache_config.eviction_interval().seconds());
    }
    if (cache_config.has_default_ttl()) {
      default_ttl = std::chrono::seconds(cache_config.default_ttl().seconds());
    }
    if (cache_config.max_entries() > 0) {
      max_entries = cache_config.max_entries();
    }
  }

  // Get or create singleton cache manager
  // We use the singleton manager to ensure only one cache manager exists per server instance
  auto cache_manager =
      context.serverFactoryContext()
          .singletonManager()
          .getTyped<Common::SyntheticIp::SyntheticIpCacheManager>(
              "synthetic_ip_cache_manager_singleton", [&context, eviction_interval, max_entries]() {
                return std::make_shared<Common::SyntheticIp::SyntheticIpCacheManager>(
                    context.serverFactoryContext().threadLocal(),
                    context.serverFactoryContext().mainThreadDispatcher(), eviction_interval,
                    max_entries);
              });

  return [config, cache_manager, &context](Network::UdpListenerFilterManager& filter_manager,
                                           Network::UdpReadFilterCallbacks& callbacks) -> void {
    filter_manager.addReadFilter(std::make_unique<DnsGatewayFilter>(
        callbacks, config, cache_manager, context.serverFactoryContext().api().timeSource(),
        context.serverFactoryContext().api().randomGenerator()));
  };
}

ProtobufTypes::MessagePtr DnsGatewayConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::filters::udp::dns_gateway::v3::DnsGateway>();
}

std::string DnsGatewayConfigFactory::name() const {
  return "envoy.filters.udp_listener.dns_gateway";
}

/**
 * Static registration for the DNS Gateway Filter config factory.
 */
REGISTER_FACTORY(DnsGatewayConfigFactory,
                 Server::Configuration::NamedUdpListenerFilterConfigFactory);

} // namespace DnsGateway
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

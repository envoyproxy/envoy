#include "source/extensions/wildcard/dns_gateway/config.h"

#include "envoy/extensions/wildcard/dns_gateway/v3/dns_gateway.pb.h"
#include "envoy/extensions/wildcard/dns_gateway/v3/dns_gateway.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/wildcard/virtual_ip_cache/cache_manager.h"

namespace Envoy {
namespace Extensions {
namespace Wildcard {
namespace DnsGateway {

Network::UdpListenerFilterFactoryCb DnsGatewayConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, Server::Configuration::ListenerFactoryContext& context) {

  const auto& config = MessageUtil::downcastAndValidate<
      const envoy::extensions::wildcard::dns_gateway::v3::DnsGateway&>(
      proto_config, context.messageValidationVisitor());

  auto cache_manager = VirtualIp::VirtualIpCacheManager::singleton(context.serverFactoryContext());

  RELEASE_ASSERT(cache_manager != nullptr,
                 "VirtualIpCacheManager not found. Configure envoy.bootstrap.virtual_ip_cache in "
                 "the bootstrap section.");

  std::vector<PolicyConfig> policies;
  for (const auto& policy_proto : config.policies()) {
    policies.emplace_back(policy_proto.domain());
  }

  return [cache_manager, policies, &time_source = context.serverFactoryContext().timeSource(),
          &random = context.serverFactoryContext().api().randomGenerator()](
             Network::UdpListenerFilterManager& filter_manager,
             Network::UdpReadFilterCallbacks& callbacks) -> void {
    filter_manager.addReadFilter(std::make_unique<DnsGatewayFilter>(
        callbacks, policies, cache_manager, time_source, random));
  };
}

ProtobufTypes::MessagePtr DnsGatewayConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::wildcard::dns_gateway::v3::DnsGateway>();
}

std::string DnsGatewayConfigFactory::name() const {
  return "envoy.filters.udp_listener.dns_gateway";
}

REGISTER_FACTORY(DnsGatewayConfigFactory,
                 Server::Configuration::NamedUdpListenerFilterConfigFactory);

} // namespace DnsGateway
} // namespace Wildcard
} // namespace Extensions
} // namespace Envoy

#include "source/extensions/filters/udp/dns_filter/config.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

namespace {
const auto& validatedConfig(const Protobuf::Message& config,
                            Server::Configuration::ListenerFactoryContext& context) {
  return MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig&>(
      config, context.messageValidationVisitor());
}

Network::UdpListenerFilterFactoryCb createFilterFactory(DnsFilterEnvoyConfigSharedPtr config) {
  return [config = std::move(config)](Network::UdpListenerFilterManager& filter_manager,
                                      Network::UdpReadFilterCallbacks& callbacks) {
    filter_manager.addReadFilter(std::make_unique<DnsFilter>(callbacks, config));
  };
}
} // namespace

Network::UdpListenerFilterFactoryCb DnsFilterConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& config, Server::Configuration::ListenerFactoryContext& context) {
  return createFilterFactory(
      std::make_shared<DnsFilterEnvoyConfig>(context, validatedConfig(config, context)));
}

Network::UdpListenerFilterFactoryCb DnsFilterConfigFactory::createDynamicFilterFactoryFromProto(
    const Protobuf::Message& config, Server::Configuration::ListenerFactoryContext& context) {
  return createFilterFactory(
      DnsFilterEnvoyConfig::createForLiveReplacement(context, validatedConfig(config, context)));
}

ProtobufTypes::MessagePtr DnsFilterConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig>();
}

std::string DnsFilterConfigFactory::name() const { return "envoy.filters.udp.dns_filter"; }

/**
 * Static registration for the DNS Filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<DnsFilterConfigFactory,
                                 Server::Configuration::NamedUdpListenerFilterConfigFactory>
    register_;

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

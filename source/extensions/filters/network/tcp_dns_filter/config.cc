#include "source/extensions/filters/network/tcp_dns_filter/config.h"

#include "envoy/extensions/filters/udp/dns_filter/v3/dns_filter.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpDnsFilter {

/**
 * Adapter that wraps a FactoryContext as a ListenerFactoryContext.
 */
class ListenerFactoryContextAdapter : public Server::Configuration::ListenerFactoryContext {
public:
  explicit ListenerFactoryContextAdapter(Server::Configuration::FactoryContext& context)
      : context_(context) {}

  Server::Configuration::ServerFactoryContext& serverFactoryContext() override {
    return context_.serverFactoryContext();
  }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return context_.messageValidationVisitor();
  }
  Init::Manager& initManager() override { return context_.initManager(); }
  Stats::Scope& scope() override { return context_.scope(); }
  Stats::Scope& listenerScope() override { return context_.listenerScope(); }
  const Network::DrainDecision& drainDecision() override { return context_.drainDecision(); }
  const Network::ListenerInfo& listenerInfo() const override { return context_.listenerInfo(); }

private:
  Server::Configuration::FactoryContext& context_;
};

Network::FilterFactoryCb TcpDnsFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::tcp::dns_filter::v3::DnsFilterConfig& config,
    Server::Configuration::FactoryContext& context) {
  // Convert to UDP filter's proto (identical structure, different package)
  envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig udp_config;
  udp_config.MergeFrom(config);

  ListenerFactoryContextAdapter adapter(context);
  auto shared_config =
      std::make_shared<UdpFilters::DnsFilter::DnsFilterEnvoyConfig>(adapter, udp_config);

  return [shared_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<TcpDnsFilter>(shared_config));
  };
}

/**
 * Static registration for the TCP DNS filter. @see RegisterFactory.
 */
REGISTER_FACTORY(TcpDnsFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace TcpDnsFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

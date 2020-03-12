#include "extensions/filters/udp/dns_filter/dns_filter.h"

#include "envoy/network/listener.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

DnsFilterEnvoyConfig::DnsFilterEnvoyConfig(
    Server::Configuration::ListenerFactoryContext& context,
    const envoy::config::filter::udp::dns_filter::v2alpha::DnsFilterConfig& config)
    : root_scope(context.scope()), stats_(generateStats(config.stat_prefix(), root_scope)) {

  using envoy::config::filter::udp::dns_filter::v2alpha::DnsFilterConfig;

  // store configured data for server context
  const auto& server_config = config.server_config();

  if (server_config.has_control_plane_cfg()) {

    const auto& cfg = server_config.control_plane_cfg();
    const size_t entries = cfg.virtual_domains().size();

    virtual_domains_.reserve(entries);
    for (const auto& virtual_domain : cfg.virtual_domains()) {
      DnsAddressList addresses{};

      if (virtual_domain.endpoint().has_addresslist()) {
        const auto& address_list = virtual_domain.endpoint().addresslist().address();
        addresses.reserve(address_list.size());
        for (const auto& configured_address : address_list) {
          addresses.push_back(configured_address);
        }
      }

      virtual_domains_.emplace(std::make_pair(virtual_domain.name(), addresses));
    }
  }
}

void DnsFilter::onData(Network::UdpRecvData& client_request) {
  // Handle incoming request and respond with an answer
  (void)client_request;
}

void DnsFilter::onReceiveError(Api::IoError::IoErrorCode error_code) {
  // Increment error stats
  (void)error_code;
}

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

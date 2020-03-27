#include "extensions/filters/udp/dns_filter/dns_filter.h"

#include "envoy/network/listener.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

DnsFilterEnvoyConfig::DnsFilterEnvoyConfig(
    Server::Configuration::ListenerFactoryContext& context,
    const envoy::config::filter::udp::dns_filter::v2alpha::DnsFilterConfig& config)
    : root_scope_(context.scope()), stats_(generateStats(config.stat_prefix(), root_scope_)) {

  using envoy::config::filter::udp::dns_filter::v2alpha::DnsFilterConfig;

  // store configured data for server context
  const auto& server_config = config.server_config();

  if (server_config.has_inline_dns_table()) {

    const auto& cfg = server_config.inline_dns_table();
    const size_t entries = cfg.virtual_domains().size();

    // TODO (abaptiste): Check that the domain configured here appears
    // in the known domains list
    virtual_domains_.reserve(entries);
    for (const auto& virtual_domain : cfg.virtual_domains()) {
      DnsAddressList addresses{};

      if (virtual_domain.endpoint().has_address_list()) {
        const auto& address_list = virtual_domain.endpoint().address_list().address();
        addresses.reserve(address_list.size());
        for (const auto& configured_address : address_list) {
          addresses.push_back(configured_address);
        }
      }

      virtual_domains_.emplace(virtual_domain.name(), std::move(addresses));
    }
  }
}

void DnsFilter::onData(Network::UdpRecvData& client_request) {
  // Handle incoming request and respond with an answer
  UNREFERENCED_PARAMETER(client_request);
}

void DnsFilter::onReceiveError(Api::IoError::IoErrorCode error_code) {
  // Increment error stats
  UNREFERENCED_PARAMETER(error_code);
}

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

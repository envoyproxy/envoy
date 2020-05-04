#include "extensions/filters/udp/dns_filter/dns_filter.h"

#include "envoy/network/listener.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "common/network/address_impl.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

DnsFilterEnvoyConfig::DnsFilterEnvoyConfig(
    Server::Configuration::ListenerFactoryContext& context,
    const envoy::extensions::filters::udp::dns_filter::v3alpha::DnsFilterConfig& config)
    : root_scope_(context.scope()), stats_(generateStats(config.stat_prefix(), root_scope_)) {
  using envoy::extensions::filters::udp::dns_filter::v3alpha::DnsFilterConfig;

  static constexpr std::chrono::milliseconds DEFAULT_RESOLVER_TIMEOUT{500};
  static constexpr std::chrono::seconds DEFAULT_RESOLVER_TTL{300};

  const auto& server_config = config.server_config();

  // TODO(abaptiste): Read the external DataSource
  if (server_config.has_inline_dns_table()) {
    const auto& dns_table = server_config.inline_dns_table();
    const size_t entries = dns_table.virtual_domains().size();

    virtual_domains_.reserve(entries);
    for (const auto& virtual_domain : dns_table.virtual_domains()) {
      AddressConstPtrVec addrs{};
      if (virtual_domain.endpoint().has_address_list()) {
        const auto& address_list = virtual_domain.endpoint().address_list().address();
        addrs.reserve(address_list.size());
        // This will throw an exception if the configured_address string is malformed
        for (const auto& configured_address : address_list) {
          const auto ipaddr =
              Network::Utility::parseInternetAddress(configured_address, 0 /* port */);
          addrs.push_back(ipaddr);
        }
      }
      virtual_domains_.emplace(virtual_domain.name(), std::move(addrs));
      std::chrono::seconds ttl = virtual_domain.has_answer_ttl()
                                     ? std::chrono::seconds(virtual_domain.answer_ttl().seconds())
                                     : DEFAULT_RESOLVER_TTL;
      domain_ttl_.emplace(virtual_domain.name(), ttl);
    }

    // Add known domains
    known_suffixes_.reserve(dns_table.known_suffixes().size());
    for (const auto& suffix : dns_table.known_suffixes()) {
      auto matcher_ptr = std::make_unique<Matchers::StringMatcherImpl>(suffix);
      known_suffixes_.push_back(std::move(matcher_ptr));
    }
  }

  forward_queries_ = config.has_client_config();
  if (forward_queries_) {
    const auto& client_config = config.client_config();
    const auto& upstream_resolvers = client_config.upstream_resolvers();
    resolvers_.reserve(upstream_resolvers.size());
    for (const auto& resolver : upstream_resolvers) {
      auto ipaddr = Network::Utility::parseInternetAddress(resolver, 0 /* port */);
      resolvers_.push_back(std::move(ipaddr));
    }
    resolver_timeout_ = std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(
        client_config, resolver_timeout, DEFAULT_RESOLVER_TIMEOUT.count()));
  }
}

void DnsFilter::onData(Network::UdpRecvData& client_request) {
  // Parse the query, if it fails return an response to the client
  DnsQueryContextPtr query_context = message_parser_.createQueryContext(client_request);
  if (!query_context->parse_status_) {
    sendDnsResponse(std::move(query_context));
    return;
  }

  // TODO(abaptiste): Resolve the requested name

  // Send an answer to the client
  sendDnsResponse(std::move(query_context));
}

void DnsFilter::sendDnsResponse(DnsQueryContextPtr query_context) {
  Buffer::OwnedImpl response;
  // TODO(abaptiste): serialize and return a response to the client

  Network::UdpSendData response_data{query_context->local_->ip(), *(query_context->peer_),
                                     response};
  listener_.send(response_data);
}

void DnsFilter::onReceiveError(Api::IoError::IoErrorCode error_code) {
  UNREFERENCED_PARAMETER(error_code);
}

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

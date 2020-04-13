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
    const envoy::config::filter::udp::dns_filter::v2alpha::DnsFilterConfig& config)
    : root_scope_(context.scope()), stats_(generateStats(config.stat_prefix(), root_scope_)) {

  using envoy::config::filter::udp::dns_filter::v2alpha::DnsFilterConfig;

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
          const auto ipaddr = Network::Utility::parseInternetAddress(
              configured_address, 0 /* port */, true /* v6only */);
          addrs.push_back(ipaddr);
        }
      }
      virtual_domains_.emplace(virtual_domain.name(), std::move(addrs));

      uint64_t ttl = virtual_domain.has_answer_ttl()
                         ? DurationUtil::durationToSeconds(virtual_domain.answer_ttl())
                         : DefaultResolverTTLs;
      domain_ttl_.emplace(virtual_domain.name(), ttl);
    }

    // Add known domains
    known_suffixes_.reserve(dns_table.known_suffixes().size());
    for (const auto& suffix : dns_table.known_suffixes()) {
      // TODO(abaptiste): We support only suffixes here. Expand this to support other StringMatcher
      // types
      envoy::type::matcher::v3::StringMatcher matcher;
      matcher.set_suffix(suffix.suffix());
      auto matcher_ptr = std::make_unique<Matchers::StringMatcherImpl>(matcher);
      known_suffixes_.push_back(std::move(matcher_ptr));
    }
  }

  const auto& client_config = config.client_config();
  forward_queries_ = client_config.forward_query();
  if (forward_queries_) {
    const auto& upstream_resolvers = client_config.upstream_resolvers();
    resolvers_.reserve(upstream_resolvers.size());
    for (const auto& resolver : upstream_resolvers) {
      auto ipaddr =
          Network::Utility::parseInternetAddress(resolver, 0 /* port */, true /* v6only */);
      resolvers_.push_back(std::move(ipaddr));
    }
  }

  resolver_timeout_ms_ = std::chrono::milliseconds(
      PROTOBUF_GET_MS_OR_DEFAULT(client_config, resolver_timeout, DefaultResolverTimeoutMs));
}

void DnsFilter::onData(Network::UdpRecvData& client_request) {

  // Parse the query, if it fails return an response to the client
  DnsQueryContextPtr query_context = message_parser_->createQueryContext(client_request);
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

void DnsFilter::onReceiveError(Api::IoError::IoErrorCode) {
  // config_->stats().downstream_sess_rx_errors_.inc();
}

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

#include "source/extensions/filters/network/tcp_dns_filter/tcp_dns_filter.h"

#include "envoy/network/connection.h"

#include "source/extensions/filters/udp/dns_filter/dns_filter_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpDnsFilter {

using UdpFilters::DnsFilter::AddressConstPtrVec;
using UdpFilters::DnsFilter::DNS_RESPONSE_CODE_FORMAT_ERROR;
using UdpFilters::DnsFilter::DnsEndpointConfig;
using UdpFilters::DnsFilter::Utils::getDomainSuffix;

TcpDnsFilter::TcpDnsFilter(const DnsFilterEnvoyConfigSharedPtr& config) : config_(config) {}

Network::FilterStatus TcpDnsFilter::onData(Buffer::Instance& data, bool) {
  buffer_.add(data);
  data.drain(data.length());

  // TCP DNS messages are prefixed with a 2-byte length (RFC 1035 §4.2.2)
  while (buffer_.length() >= 2) {
    uint16_t msg_len = buffer_.peekBEInt<uint16_t>(0);
    if (buffer_.length() < static_cast<uint64_t>(msg_len) + 2) {
      break;
    }

    buffer_.drain(2);
    Buffer::OwnedImpl dns_buffer;
    dns_buffer.move(buffer_, msg_len);

    processDnsQuery(dns_buffer);
  }

  return Network::FilterStatus::StopIteration;
}

void TcpDnsFilter::processDnsQuery(Buffer::OwnedImpl& dns_buffer) {
  DnsParserCounters counters(
      config_->stats().query_buffer_underflow_, config_->stats().record_name_overflow_,
      config_->stats().query_parsing_failure_, config_->stats().queries_with_additional_rrs_,
      config_->stats().queries_with_ans_or_authority_rrs_);

  DnsMessageParser parser(config_->forwardQueries(),
                          read_callbacks_->connection().dispatcher().timeSource(),
                          config_->retryCount(), config_->random(),
                          config_->stats().downstream_rx_query_latency_);

  auto local_addr = read_callbacks_->connection().connectionInfoProvider().localAddress();
  auto peer_addr = read_callbacks_->connection().connectionInfoProvider().remoteAddress();
  DnsQueryContextPtr context = std::make_unique<DnsQueryContext>(
      local_addr, peer_addr, counters, config_->retryCount());

  // Parse the DNS query
  Buffer::InstancePtr buffer_ptr = std::make_unique<Buffer::OwnedImpl>();
  static_cast<Buffer::OwnedImpl*>(buffer_ptr.get())->move(dns_buffer);

  context->parse_status_ = parser.parseDnsObject(context, buffer_ptr);
  if (!context->parse_status_) {
    context->response_code_ = DNS_RESPONSE_CODE_FORMAT_ERROR;
    ENVOY_LOG(debug, "TCP DNS: failed to parse query from '{}'",
              peer_addr->ip()->addressAsString());
  } else {
    // Resolve each query using the inline DNS table
    for (const auto& query : context->queries_) {
      resolveQuery(context, *query, parser);
    }
  }

  // Build and send response with TCP length prefix
  Buffer::OwnedImpl response_buffer;
  parser.buildResponseBuffer(context, response_buffer);

  Buffer::OwnedImpl framed_response;
  framed_response.writeBEInt<uint16_t>(static_cast<uint16_t>(response_buffer.length()));
  framed_response.add(response_buffer);

  read_callbacks_->connection().write(framed_response, false);
}

bool TcpDnsFilter::resolveQuery(DnsQueryContextPtr& context, const DnsQueryRecord& query,
                                DnsMessageParser& parser) {
  const absl::string_view suffix = getDomainSuffix(query.name_);
  const auto virtual_domains = config_->getDnsTrie().find(suffix);
  if (virtual_domains == nullptr) {
    return false;
  }

  // Try exact match, then wildcard match
  const DnsEndpointConfig* endpoint_config = nullptr;
  size_t pos = 0;
  while (pos != absl::string_view::npos) {
    absl::string_view lookup = absl::string_view(query.name_).substr(pos);
    const auto iter = virtual_domains->find(lookup);
    if (iter != virtual_domains->end()) {
      endpoint_config = &(iter->second);
      break;
    }
    pos = query.name_.find('.', pos + 1);
  }

  if (endpoint_config == nullptr || !endpoint_config->address_list.has_value()) {
    return false;
  }

  // Get TTL for this domain
  const auto& domain_ttl = config_->domainTtl();
  const auto ttl_iter = domain_ttl.find(query.name_);
  const std::chrono::seconds ttl =
      (ttl_iter != domain_ttl.end()) ? ttl_iter->second : std::chrono::seconds(300);

  for (const auto& addr : endpoint_config->address_list.value()) {
    parser.storeDnsAnswerRecord(context, query, ttl, addr);
  }
  return true;
}

} // namespace TcpDnsFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

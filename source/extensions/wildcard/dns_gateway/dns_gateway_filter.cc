#include "source/extensions/wildcard/dns_gateway/dns_gateway_filter.h"

#include <arpa/inet.h>

#include <limits>

#include "source/common/network/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Wildcard {
namespace DnsGateway {

PolicyConfig::PolicyConfig(const std::string& pattern) : domain_pattern(pattern) {}

bool PolicyConfig::matches(absl::string_view domain) const {
  // Wildcard matching
  if (domain_pattern[0] == '*' && domain_pattern[1] == '.') {
    absl::string_view suffix = absl::string_view(domain_pattern).substr(2);
    return domain.size() > suffix.size() && domain.ends_with(suffix);
  }
  // Exact match
  return domain == domain_pattern;
}

DnsGatewayFilter::DnsGatewayFilter(Network::UdpReadFilterCallbacks& callbacks,
                                   const std::vector<PolicyConfig>& policies,
                                   VirtualIp::VirtualIpCacheManagerSharedPtr cache_manager,
                                   TimeSource& time_source, Random::RandomGenerator& random)
    : UdpListenerReadFilter(callbacks), policies_(policies), cache_manager_(cache_manager),
      stats_store_(), latency_stat_name_("latency", stats_store_.symbolTable()),
      underflow_stat_name_("underflow", stats_store_.symbolTable()),
      record_name_overflow_stat_name_("record_name_overflow", stats_store_.symbolTable()),
      query_parsing_failure_stat_name_("query_parsing_failure", stats_store_.symbolTable()),
      queries_with_additional_rrs_stat_name_("queries_with_additional_rrs",
                                             stats_store_.symbolTable()),
      queries_with_ans_or_authority_rrs_stat_name_("queries_with_ans_or_authority_rrs",
                                                   stats_store_.symbolTable()),
      latency_histogram_(stats_store_.rootScope()->histogramFromStatName(
          latency_stat_name_.statName(), Stats::Histogram::Unit::Milliseconds)),
      underflow_counter_(
          stats_store_.rootScope()->counterFromStatName(underflow_stat_name_.statName())),
      record_name_overflow_counter_(stats_store_.rootScope()->counterFromStatName(
          record_name_overflow_stat_name_.statName())),
      query_parsing_failure_counter_(stats_store_.rootScope()->counterFromStatName(
          query_parsing_failure_stat_name_.statName())),
      queries_with_additional_rrs_counter_(stats_store_.rootScope()->counterFromStatName(
          queries_with_additional_rrs_stat_name_.statName())),
      queries_with_ans_or_authority_rrs_counter_(stats_store_.rootScope()->counterFromStatName(
          queries_with_ans_or_authority_rrs_stat_name_.statName())),
      message_parser_(false, time_source, 0, random, latency_histogram_) {
  ENVOY_LOG(info, "DNS Gateway Filter initialized with {} policies", policies_.size());
}

Network::FilterStatus DnsGatewayFilter::onData(Network::UdpRecvData& data) {
  // Parse DNS query
  UdpFilters::DnsFilter::DnsParserCounters parser_counters(
      underflow_counter_, record_name_overflow_counter_, query_parsing_failure_counter_,
      queries_with_additional_rrs_counter_, queries_with_ans_or_authority_rrs_counter_);

  UdpFilters::DnsFilter::DnsQueryContextPtr query_context =
      message_parser_.createQueryContext(data, parser_counters);

  if (!query_context->parse_status_) {
    ENVOY_LOG(debug, "Failed to parse DNS query");
    return Network::FilterStatus::Continue;
  }

  // Process queries
  bool matched = false;
  for (const auto& query : query_context->queries_) {
    if (query->type_ != UdpFilters::DnsFilter::DNS_RECORD_TYPE_A) {
      continue;
    }

    const PolicyConfig* policy = findMatchingPolicy(query->name_);
    if (!policy) {
      continue;
    }

    matched = true;
    ENVOY_LOG(debug, "Matched policy for domain: {}", query->name_);

    // Capture data needed for response - avoid capturing the whole context
    std::string domain_copy(query->name_);
    uint16_t query_id = query_context->id_;
    Network::Address::InstanceConstSharedPtr local_addr = query_context->local_;
    Network::Address::InstanceConstSharedPtr peer_addr = query_context->peer_;

    // Request allocation from main thread (uses global CIDR configured at startup)
    cache_manager_->allocate(
        domain_copy,
        read_callbacks_->udpListener().dispatcher(), // Current worker's dispatcher
        [this, domain_copy, query_id, local_addr, peer_addr](std::string virtual_ip) {
          // This callback runs on THIS worker thread after replication completes
          ENVOY_LOG(info, "Allocated IP {} for domain {}", virtual_ip, domain_copy);
          sendDnsResponse(virtual_ip, domain_copy, query_id, local_addr, peer_addr);
        });
  }

  if (!matched) {
    ENVOY_LOG(debug, "No matching policy, passing through");
    return Network::FilterStatus::Continue;
  }

  return Network::FilterStatus::StopIteration;
}

void DnsGatewayFilter::sendDnsResponse(const std::string& virtual_ip, const std::string& domain,
                                       uint16_t query_id,
                                       const Network::Address::InstanceConstSharedPtr& local_addr,
                                       const Network::Address::InstanceConstSharedPtr& peer_addr) {
  ENVOY_LOG(info, "sendDnsResponse called: domain={}, query_id={}, virtual_ip={}", domain, query_id,
            virtual_ip);

  // Parse virtual IP
  auto ipaddr = Network::Utility::parseInternetAddressNoThrow(virtual_ip, 0);
  if (!ipaddr) {
    ENVOY_LOG(warn, "Failed to parse virtual IP: {}", virtual_ip);
    return;
  }

  // Create a minimal query context for building the response
  UdpFilters::DnsFilter::DnsParserCounters counters(
      underflow_counter_, record_name_overflow_counter_, query_parsing_failure_counter_,
      queries_with_additional_rrs_counter_, queries_with_ans_or_authority_rrs_counter_);

  auto query_context =
      std::make_unique<UdpFilters::DnsFilter::DnsQueryContext>(local_addr, peer_addr, counters, 0);
  query_context->id_ = query_id;
  query_context->header_.id = query_id; // setDnsResponseFlags copies from here!
  query_context->parse_status_ = true;

  ENVOY_LOG(info, "Query context created with id={}", query_context->id_);

  // Create query record (DNS responses must echo the question)
  auto query_record = std::make_unique<UdpFilters::DnsFilter::DnsQueryRecord>(
      domain, UdpFilters::DnsFilter::DNS_RECORD_TYPE_A, UdpFilters::DnsFilter::DNS_RECORD_CLASS_IN);
  query_context->queries_.push_back(std::move(query_record));

  // Use the parser's helper to store the answer (it handles all the details)
  message_parser_.storeDnsAnswerRecord(query_context, *query_context->queries_[0],
                                       std::chrono::seconds(std::numeric_limits<uint32_t>::max()),
                                       ipaddr);

  uint16_t response_id = query_context->response_header_.id;
  ENVOY_LOG(info, "Before buildResponseBuffer: response_header_.id={}", response_id);

  // Serialize response
  Buffer::OwnedImpl response_buffer;
  message_parser_.buildResponseBuffer(query_context, response_buffer);

  ENVOY_LOG(info, "Built response buffer, size={} bytes", response_buffer.length());

  // Send to client
  read_callbacks_->udpListener().send(
      Network::UdpSendData{local_addr->ip(), *peer_addr, response_buffer});

  ENVOY_LOG(info, "Sent DNS response: {} -> {}, query_id={}", domain, virtual_ip, query_id);
}

const PolicyConfig* DnsGatewayFilter::findMatchingPolicy(absl::string_view domain) const {
  for (const auto& policy : policies_) {
    if (policy.matches(domain)) {
      return &policy;
    }
  }
  return nullptr;
}

} // namespace DnsGateway
} // namespace Wildcard
} // namespace Extensions
} // namespace Envoy

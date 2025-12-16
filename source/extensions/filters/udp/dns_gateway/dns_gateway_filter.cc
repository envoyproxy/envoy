#include "source/extensions/filters/udp/dns_gateway/dns_gateway_filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/network/utility.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/udp/dns_filter/dns_filter_constants.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsGateway {

namespace {

// Simple hash function for deterministic IP allocation
uint32_t hashDomain(absl::string_view domain) {
  uint32_t hash = 5381;
  for (char c : domain) {
    hash = ((hash << 5) + hash) + static_cast<uint32_t>(c);
  }
  return hash;
}

} // namespace

PolicyConfig::PolicyConfig(const std::string& pattern, const std::string& cidr_ip,
                           uint32_t prefix_len, std::chrono::seconds ttl_seconds)
    : domain_pattern(pattern), cidr_prefix_len(prefix_len), ttl(ttl_seconds) {
  // Parse CIDR base IP
  struct in_addr addr;
  if (inet_pton(AF_INET, cidr_ip.c_str(), &addr) == 1) {
    cidr_base_ip = ntohl(addr.s_addr);
  } else {
    cidr_base_ip = 0;
  }

  // Calculate maximum IPs available in CIDR block
  max_ips_in_cidr = (1u << (32 - cidr_prefix_len)) - 2; // Exclude network and broadcast
}

bool PolicyConfig::matches(absl::string_view domain) const {
  // Check for wildcard pattern (e.g., "*.azure.com")
  if (domain_pattern[0] == '*' && domain_pattern[1] == '.') {
    absl::string_view suffix = absl::string_view(domain_pattern).substr(2);

    // Domain must end with the suffix
    if (domain.length() <= suffix.length()) {
      return false;
    }

    // Check if domain ends with suffix
    if (!absl::EndsWith(domain, suffix)) {
      return false;
    }

    // Ensure there's at least one label before the suffix
    size_t suffix_pos = domain.length() - suffix.length();
    if (suffix_pos > 0 && domain[suffix_pos - 1] != '.') {
      return false;
    }

    return true;
  }

  // Exact match
  return domain == domain_pattern;
}

std::string PolicyConfig::allocateSyntheticIp(absl::string_view domain) const {
  // Deterministic hash-based allocation
  uint32_t hash = hashDomain(domain);
  uint32_t ip_offset = (hash % max_ips_in_cidr) + 1; // +1 to skip network address

  // Calculate synthetic IP
  uint32_t synthetic_ip_int = cidr_base_ip + ip_offset;

  // Convert back to string
  struct in_addr addr;
  addr.s_addr = htonl(synthetic_ip_int);
  char ip_str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &addr, ip_str, INET_ADDRSTRLEN);

  return std::string(ip_str);
}

DnsGatewayFilter::DnsGatewayFilter(
    Network::UdpReadFilterCallbacks& callbacks, const DnsGatewayConfig& config,
    Common::SyntheticIp::SyntheticIpCacheManagerSharedPtr cache_manager, TimeSource& time_source,
    Random::RandomGenerator& random)
    : UdpListenerReadFilter(callbacks), cache_manager_(cache_manager), stats_store_(),
      // Create stats counters during construction
      underflow_counter_(stats_store_.counterFromString("underflow")),
      record_name_overflow_counter_(stats_store_.counterFromString("record_name_overflow")),
      query_parsing_failure_counter_(stats_store_.counterFromString("query_parsing_failure")),
      queries_with_additional_rrs_counter_(stats_store_.counterFromString("queries_with_additional_rrs")),
      queries_with_ans_or_authority_rrs_counter_(stats_store_.counterFromString("queries_with_ans_or_authority_rrs")),
      latency_histogram_(stats_store_.histogramFromString("latency", Stats::Histogram::Unit::Milliseconds)),
      // Initialize DNS parser with recursion disabled (we don't forward queries)
      message_parser_(false, time_source, 0, random, latency_histogram_) {

  // Parse default TTL
  if (config.has_default_ttl()) {
    default_ttl_ = std::chrono::seconds(config.default_ttl().seconds());
  } else {
    default_ttl_ = std::chrono::seconds(300); // 5 minutes default
  }

  // Parse policies
  for (const auto& policy_proto : config.policies()) {
    std::chrono::seconds ttl =
        policy_proto.has_ttl() ? std::chrono::seconds(policy_proto.ttl().seconds()) : default_ttl_;

    const auto& cidr = policy_proto.cidr_block();
    policies_.emplace_back(policy_proto.domain_pattern(), cidr.address_prefix(),
                           cidr.prefix_len().value(), ttl);
  }

  ENVOY_LOG(info, "DNS Gateway Filter initialized with {} policies", policies_.size());
}

Network::FilterStatus DnsGatewayFilter::onData(Network::UdpRecvData& data) {
  DnsFilter::DnsParserCounters parser_counters(
      underflow_counter_,
      record_name_overflow_counter_,
      query_parsing_failure_counter_,
      queries_with_additional_rrs_counter_,
      queries_with_ans_or_authority_rrs_counter_);

  // Use the safe DNS parser to parse the query
  DnsFilter::DnsQueryContextPtr query_context =
      message_parser_.createQueryContext(data, parser_counters);

  // If parsing failed, drop the packet silently
  // Don't send error responses to avoid potential loops
  if (!query_context->parse_status_) {
    ENVOY_LOG(debug, "Failed to parse DNS query, dropping packet");
    return Network::FilterStatus::StopIteration;
  }

  // Process each query in the request
  bool matched = false;
  for (const auto& query : query_context->queries_) {
    // Only handle A record queries
    if (query->type_ != DnsFilter::DNS_RECORD_TYPE_A) {
      ENVOY_LOG(debug, "Skipping non-A record query for {}", query->name_);
      continue;
    }

    ENVOY_LOG(debug, "DNS A record query for domain: {}", query->name_);

    // Find matching policy
    const PolicyConfig* policy = findMatchingPolicy(query->name_);
    if (!policy) {
      ENVOY_LOG(debug, "No matching policy for domain: {}, passing through", query->name_);
      continue;
    }

    matched = true;
    ENVOY_LOG(debug, "Matched policy for domain: {} with pattern: {}", query->name_,
              policy->domain_pattern);

    // Allocate synthetic IP
    std::string synthetic_ip = policy->allocateSyntheticIp(query->name_);
    ENVOY_LOG(info, "Allocated synthetic IP {} for domain {}", synthetic_ip, query->name_);

    // Store in current worker's cache immediately
    cache_manager_->put(synthetic_ip, query->name_, policy->ttl);

    // Replicate to all workers via main thread
    // This ensures TCP connections on any worker can find the mapping
    cache_manager_->replicateFromWorker(synthetic_ip, query->name_, policy->ttl);

    // Parse the synthetic IP and create an address
    auto ipaddr = Network::Utility::parseInternetAddressNoThrow(synthetic_ip, 0);
    if (!ipaddr) {
      ENVOY_LOG(warn, "Failed to parse synthetic IP: {}", synthetic_ip);
      continue;
    }

    // Use the safe DNS parser to store the answer record
    message_parser_.storeDnsAnswerRecord(query_context, *query, policy->ttl, ipaddr);
  }

  // If no policies matched, pass through (return Continue to let other filters handle it)
  if (!matched) {
    return Network::FilterStatus::Continue;
  }

  // Build and send the response using the safe parser
  Buffer::OwnedImpl response;
  message_parser_.buildResponseBuffer(query_context, response);

  read_callbacks_->udpListener().send(
      Network::UdpSendData{data.addresses_.local_->ip(), *data.addresses_.peer_, response});

  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus DnsGatewayFilter::onReceiveError(Api::IoError::IoErrorCode error_code) {
  ENVOY_LOG(debug, "DNS Gateway Filter receive error: {}", static_cast<int>(error_code));
  return Network::FilterStatus::Continue;
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
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

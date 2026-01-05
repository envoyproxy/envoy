#pragma once

#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/filters/udp/dns_filter/dns_parser.h"
#include "source/extensions/wildcard/virtual_ip_cache/cache_manager.h"

namespace Envoy {
namespace Extensions {
namespace Wildcard {
namespace DnsGateway {

/**
 * Policy configuration for domain pattern matching.
 * Virtual IPs are allocated from a global CIDR block shared across all policies.
 */
struct PolicyConfig {
  std::string domain_pattern;

  explicit PolicyConfig(const std::string& pattern);

  bool matches(absl::string_view domain) const;
};

/**
 * DNS Gateway Filter - allocates virtual IPs for wildcard domain patterns.
 * Runs on UDP listener, intercepts DNS queries and responds with virtual IPs.
 * All policies share a global CIDR block for IP allocation.
 */
class DnsGatewayFilter : public Network::UdpListenerReadFilter,
                         Logger::Loggable<Logger::Id::filter> {
public:
  DnsGatewayFilter(Network::UdpReadFilterCallbacks& callbacks,
                   const std::vector<PolicyConfig>& policies,
                   VirtualIp::VirtualIpCacheManagerSharedPtr cache_manager, TimeSource& time_source,
                   Random::RandomGenerator& random);

  // Network::UdpListenerReadFilter
  Network::FilterStatus onData(Network::UdpRecvData& data) override;
  Network::FilterStatus onReceiveError(Api::IoError::IoErrorCode) override {
    return Network::FilterStatus::StopIteration;
  }

private:
  const PolicyConfig* findMatchingPolicy(absl::string_view domain) const;

  void sendDnsResponse(const std::string& virtual_ip, const std::string& domain, uint16_t query_id,
                       const Network::Address::InstanceConstSharedPtr& local_addr,
                       const Network::Address::InstanceConstSharedPtr& peer_addr);

  std::vector<PolicyConfig> policies_;
  VirtualIp::VirtualIpCacheManagerSharedPtr cache_manager_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::StatNameStorage latency_stat_name_;
  Stats::StatNameStorage underflow_stat_name_;
  Stats::StatNameStorage record_name_overflow_stat_name_;
  Stats::StatNameStorage query_parsing_failure_stat_name_;
  Stats::StatNameStorage queries_with_additional_rrs_stat_name_;
  Stats::StatNameStorage queries_with_ans_or_authority_rrs_stat_name_;
  Stats::Histogram& latency_histogram_;
  Stats::Counter& underflow_counter_;
  Stats::Counter& record_name_overflow_counter_;
  Stats::Counter& query_parsing_failure_counter_;
  Stats::Counter& queries_with_additional_rrs_counter_;
  Stats::Counter& queries_with_ans_or_authority_rrs_counter_;
  UdpFilters::DnsFilter::DnsMessageParser message_parser_;
};

} // namespace DnsGateway
} // namespace Wildcard
} // namespace Extensions
} // namespace Envoy

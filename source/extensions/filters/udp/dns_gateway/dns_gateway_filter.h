#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/udp/dns_gateway/v3/dns_gateway.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/filter.h"

#include "source/common/common/logger.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/common/synthetic_ip/cache_manager.h"
#include "source/extensions/filters/udp/dns_filter/dns_parser.h"

#include "absl/container/flat_hash_map.h"

#include <atomic>
#include <memory>

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsGateway {

using DnsGatewayConfig = envoy::extensions::filters::udp::dns_gateway::v3::DnsGateway;
using DnsGatewayPolicy = envoy::extensions::filters::udp::dns_gateway::v3::DnsGatewayPolicy;

/**
 * Policy configuration for domain pattern matching and synthetic IP allocation.
 */
struct PolicyConfig {
  // Allocation strategy enum
  enum class AllocationStrategy { Hash, Linear, Random };

  std::string domain_pattern;
  uint32_t cidr_base_ip; // Base IP as uint32 in host byte order
  uint32_t cidr_prefix_len;
  std::chrono::seconds ttl;

  // For deterministic IP allocation from CIDR block
  uint32_t max_ips_in_cidr;

  // Allocation strategy for this policy
  AllocationStrategy strategy_;

  // Shared counter for LINEAR strategy (shared across all workers)
  std::shared_ptr<std::atomic<uint32_t>> shared_counter_;

  PolicyConfig(const std::string& pattern, const std::string& cidr_ip, uint32_t prefix_len,
               std::chrono::seconds ttl_seconds, AllocationStrategy strategy,
               std::shared_ptr<std::atomic<uint32_t>> shared_counter = nullptr);

  // Check if a domain matches this policy's pattern (supports wildcards)
  bool matches(absl::string_view domain) const;

  // Allocate a synthetic IP from this policy's CIDR block
  std::string allocateSyntheticIp(absl::string_view domain,
                                  Random::RandomGenerator* random = nullptr) const;
};

/**
 * DNS Gateway Filter - allocates synthetic IPs for wildcard domain patterns.
 * This is a UDP listener filter that intercepts DNS queries.
 */
class DnsGatewayFilter : public Network::UdpListenerReadFilter,
                         Logger::Loggable<Logger::Id::filter> {
public:
  DnsGatewayFilter(
      Network::UdpReadFilterCallbacks& callbacks, const DnsGatewayConfig& config,
      Common::SyntheticIp::SyntheticIpCacheManagerSharedPtr cache_manager,
      const absl::flat_hash_map<std::string, std::shared_ptr<std::atomic<uint32_t>>>&
          shared_counters,
      TimeSource& time_source, Random::RandomGenerator& random);

  // Network::UdpListenerReadFilter
  Network::FilterStatus onData(Network::UdpRecvData& data) override;
  Network::FilterStatus onReceiveError(Api::IoError::IoErrorCode error_code) override;

private:
  // Find matching policy for a domain
  const PolicyConfig* findMatchingPolicy(absl::string_view domain) const;

  std::vector<PolicyConfig> policies_;
  std::chrono::seconds default_ttl_;
  Common::SyntheticIp::SyntheticIpCacheManagerSharedPtr cache_manager_;
  Random::RandomGenerator& random_;

  // Temporary stats store for DNS parser (doesn't persist anything)
  Stats::IsolatedStoreImpl stats_store_;

  // Use the battle-tested DNS parser from dns_filter
  DnsFilter::DnsMessageParser message_parser_;
};

} // namespace DnsGateway
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

#pragma once

#include "envoy/config/filter/udp/dns_filter/v2alpha/dns_filter.pb.h"
#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/config/config_provider_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"

#include "extensions/filters/udp/dns_filter/dns_parser.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

/**
 * All Dns Filter stats. @see stats_macros.h
 * Track the number of answered and un-answered queries for A and AAAA records
 */
#define ALL_DNS_FILTER_STATS(COUNTER)                                                              \
  COUNTER(queries_a_record)                                                                        \
  COUNTER(noanswers_a_record)                                                                      \
  COUNTER(answers_a_record)                                                                        \
  COUNTER(queries_aaaa_record)                                                                     \
  COUNTER(noanswers_aaaa_record)                                                                   \
  COUNTER(answers_aaaa_record)

/**
 * Struct definition for all Dns Filter stats. @see stats_macros.h
 */
struct DnsProxyFilterStats {
  ALL_DNS_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

using DnsAddressList = std::vector<std::string>;
using DnsVirtualDomainConfig = absl::flat_hash_map<std::string, DnsAddressList>;

class DnsFilterEnvoyConfig {
public:
  DnsFilterEnvoyConfig(
      Server::Configuration::ListenerFactoryContext& context,
      const envoy::config::filter::udp::dns_filter::v2alpha::DnsFilterConfig& config);

  DnsProxyFilterStats& stats() const { return stats_; }
  DnsVirtualDomainConfig domains() const { return virtual_domains_; }

private:
  static DnsProxyFilterStats generateStats(const std::string& stat_prefix, Stats::Scope& scope) {
    const auto final_prefix = absl::StrCat("dns_filter.", stat_prefix);
    return {ALL_DNS_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
  }

  Stats::Scope& root_scope;
  mutable DnsProxyFilterStats stats_;
  DnsVirtualDomainConfig virtual_domains_;
};

using DnsFilterEnvoyConfigSharedPtr = std::shared_ptr<const DnsFilterEnvoyConfig>;

class DnsFilter : public Network::UdpListenerReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  DnsFilter(Network::UdpReadFilterCallbacks& callbacks, const DnsFilterEnvoyConfigSharedPtr& config)
      : UdpListenerReadFilter(callbacks), config_(config),
        query_parser_(std::make_unique<DnsQueryParser>()),
        response_parser_(std::make_unique<DnsResponseParser>()),
        listener_(callbacks.udpListener()) {}

  // Network::UdpListenerReadFilter callbacks
  void onData(Network::UdpRecvData& client_request) override;
  void onReceiveError(Api::IoError::IoErrorCode error_code) override;

private:
  void sendDnsResponse(const Network::UdpRecvData& request_data);
  DnsAnswerRecordPtr getResponseForQuery();

  const DnsFilterEnvoyConfigSharedPtr config_;
  DnsQueryParserPtr query_parser_;
  DnsResponseParserPtr response_parser_;
  Network::UdpListener& listener_;
  Runtime::RandomGeneratorImpl rng_;
  DnsAnswerRecordPtr answer_rec_;
};

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

#pragma once

#include "envoy/event/file_event.h"
#include "envoy/extensions/filters/udp/dns_filter/v3alpha/dns_filter.pb.h"
#include "envoy/network/filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/matchers.h"
#include "common/config/config_provider_impl.h"
#include "common/network/utility.h"

#include "extensions/filters/udp/dns_filter/dns_parser.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

/**
 * All DNS Filter stats. @see stats_macros.h
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
 * Struct definition for all DNS Filter stats. @see stats_macros.h
 */
struct DnsFilterStats {
  ALL_DNS_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

using DnsVirtualDomainConfig = absl::flat_hash_map<std::string, AddressConstPtrVec>;

/**
 * DnsFilter configuration class abstracting access to data necessary for the filter's operation
 */
class DnsFilterEnvoyConfig {
public:
  DnsFilterEnvoyConfig(
      Server::Configuration::ListenerFactoryContext& context,
      const envoy::extensions::filters::udp::dns_filter::v3alpha::DnsFilterConfig& config);

  DnsFilterStats& stats() const { return stats_; }
  const DnsVirtualDomainConfig& domains() const { return virtual_domains_; }
  const std::vector<Matchers::StringMatcherPtr>& knownSuffixes() const { return known_suffixes_; }
  const absl::flat_hash_map<std::string, std::chrono::seconds>& domainTtl() const {
    return domain_ttl_;
  }
  const AddressConstPtrVec& resolvers() const { return resolvers_; }
  bool forwardQueries() const { return forward_queries_; }
  const std::chrono::milliseconds resolverTimeout() const { return resolver_timeout_; }

private:
  static DnsFilterStats generateStats(const std::string& stat_prefix, Stats::Scope& scope) {
    const auto final_prefix = absl::StrCat("dns_filter.", stat_prefix);
    return {ALL_DNS_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
  }

  Stats::Scope& root_scope_;
  mutable DnsFilterStats stats_;
  DnsVirtualDomainConfig virtual_domains_;
  std::vector<Matchers::StringMatcherPtr> known_suffixes_;
  absl::flat_hash_map<std::string, std::chrono::seconds> domain_ttl_;
  bool forward_queries_;
  AddressConstPtrVec resolvers_;
  std::chrono::milliseconds resolver_timeout_;
};

using DnsFilterEnvoyConfigSharedPtr = std::shared_ptr<const DnsFilterEnvoyConfig>;

enum class DnsLookupResponseCode { Success, Failure, External };

/**
 * This class is responsible for handling incoming DNS datagrams and responding to the queries.
 * The filter will attempt to resolve the query via its configuration or direct to an external
 * resolver when necessary
 */
class DnsFilter : public Network::UdpListenerReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  DnsFilter(Network::UdpReadFilterCallbacks& callbacks, const DnsFilterEnvoyConfigSharedPtr& config)
      : UdpListenerReadFilter(callbacks), config_(config), listener_(callbacks.udpListener()) {}

  // Network::UdpListenerReadFilter callbacks
  void onData(Network::UdpRecvData& client_request) override;
  void onReceiveError(Api::IoError::IoErrorCode error_code) override;

  /**
   * @return bool true if the domain_name is a known domain for which we respond to queries
   */
  bool isKnownDomain(const absl::string_view domain_name);

private:
  /**
   * Prepare the response buffer and send it to the client
   *
   * @param context contains the data necessary to create a response and send it to a client
   */
  void sendDnsResponse(DnsQueryContextPtr context);

  /**
   * @brief Encapsulates all of the logic required to find an answer for a DNS query
   *
   * @return DnsLookupResponseCode indicating whether we were able to respond to the query or send
   * the query to an external resolver
   */
  DnsLookupResponseCode getResponseForQuery();

  /**
   * @return uint32_t retrieves the configured per domain TTL to be inserted into answer records
   */
  uint32_t getDomainTTL(const absl::string_view domain);

  /**
   * Resolves the supplied query from configured hosts
   * @param query query object containing the name to be resolved
   * @return bool true if the requested name matches a configured domain and answer records can be
   * constructed
   */
  bool resolveViaConfiguredHosts(const DnsQueryRecord& query);

  const DnsFilterEnvoyConfigSharedPtr config_;
  Network::UdpListener& listener_;
  DnsMessageParser message_parser_;
  Network::Address::InstanceConstSharedPtr local_;
  Network::Address::InstanceConstSharedPtr peer_;
  AnswerCallback answer_callback_;
};

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

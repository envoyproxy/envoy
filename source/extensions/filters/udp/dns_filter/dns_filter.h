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

struct DnsEndpointConfig {
  absl::optional<AddressConstPtrVec> address_list;
  absl::optional<std::string> cluster_name;
};

using DnsVirtualDomainConfig = absl::flat_hash_map<std::string, DnsEndpointConfig>;

/**
 * DnsFilter configuration class abstracting access to data necessary for the filter's operation
 */
class DnsFilterEnvoyConfig : public Logger::Loggable<Logger::Id::filter> {
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
  Upstream::ClusterManager& clusterManager() const { return cluster_manager_; }
  uint64_t retryCount() const { return retry_count_; }
  Runtime::RandomGenerator& random() const { return random_; }

private:
  static DnsFilterStats generateStats(const std::string& stat_prefix, Stats::Scope& scope) {
    const auto final_prefix = absl::StrCat("dns_filter.", stat_prefix);
    return {ALL_DNS_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
  }

  bool loadServerConfig(const envoy::extensions::filters::udp::dns_filter::v3alpha::
                            DnsFilterConfig::ServerContextConfig& config,
                        envoy::data::dns::v3::DnsTable& table);

  Stats::Scope& root_scope_;
  Upstream::ClusterManager& cluster_manager_;
  Api::Api& api_;

  mutable DnsFilterStats stats_;
  DnsVirtualDomainConfig virtual_domains_;
  std::vector<Matchers::StringMatcherPtr> known_suffixes_;
  absl::flat_hash_map<std::string, std::chrono::seconds> domain_ttl_;
  bool forward_queries_;
  uint64_t retry_count_;
  AddressConstPtrVec resolvers_;
  std::chrono::milliseconds resolver_timeout_;
  Runtime::RandomGenerator& random_;
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
      : UdpListenerReadFilter(callbacks), config_(config), listener_(callbacks.udpListener()),
        cluster_manager_(config_->clusterManager()),
        message_parser_(config->forwardQueries(), config->retryCount(), config->random()) {}

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
  DnsLookupResponseCode getResponseForQuery(DnsQueryContextPtr& context);

  /**
   * @return uint32_t retrieves the configured per domain TTL to be inserted into answer records
   */
  std::chrono::seconds getDomainTTL(const absl::string_view domain);

  /**
   * @brief Resolves the supplied query from configured clusters
   *
   * @param context object containing the query context
   * @param query query object containing the name to be resolved
   * @return bool true if the requested name matched a cluster and an answer record was constructed
   */
  bool resolveViaClusters(DnsQueryContextPtr& context, const DnsQueryRecord& query);

  /**
   * @brief Resolves the supplied query from configured hosts
   *
   * @param context object containing the query context
   * @param query query object containing the name to be resolved
   * @return bool true if the requested name matches a configured domain and answer records can be
   * constructed
   */
  bool resolveViaConfiguredHosts(DnsQueryContextPtr& context, const DnsQueryRecord& query);

  /**
   * @brief Helper function to retrieve the Endpoint configuration for a requested domain
   */
  const DnsEndpointConfig* getEndpointConfigForDomain(const absl::string_view domain);

  /**
   * @brief Helper function to retrieve the Address List for a requested domain
   */
  const AddressConstPtrVec* getAddressListForDomain(const absl::string_view domain);

  /**
   * @brief Helper function to retrieve a cluster name that a domain may be redirected towards
   */
  const absl::string_view getClusterNameForDomain(const absl::string_view domain);

  const DnsFilterEnvoyConfigSharedPtr config_;
  Network::UdpListener& listener_;
  Upstream::ClusterManager& cluster_manager_;
  DnsMessageParser message_parser_;
  Network::Address::InstanceConstSharedPtr local_;
  Network::Address::InstanceConstSharedPtr peer_;
  AnswerCallback answer_callback_;
};

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

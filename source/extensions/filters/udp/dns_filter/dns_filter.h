#pragma once

#include "envoy/event/file_event.h"
#include "envoy/extensions/filters/udp/dns_filter/v3/dns_filter.pb.h"
#include "envoy/network/dns.h"
#include "envoy/network/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/utility.h"
#include "source/common/config/config_provider_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/udp/dns_filter/dns_filter_resolver.h"
#include "source/extensions/filters/udp/dns_filter/dns_parser.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

/**
 * All DNS Filter stats. @see stats_macros.h
 */
#define ALL_DNS_FILTER_STATS(COUNTER, HISTOGRAM)                                                   \
  COUNTER(a_record_queries)                                                                        \
  COUNTER(aaaa_record_queries)                                                                     \
  COUNTER(srv_record_queries)                                                                      \
  COUNTER(cluster_a_record_answers)                                                                \
  COUNTER(cluster_aaaa_record_answers)                                                             \
  COUNTER(cluster_srv_record_answers)                                                              \
  COUNTER(cluster_unsupported_answers)                                                             \
  COUNTER(downstream_rx_errors)                                                                    \
  COUNTER(downstream_rx_invalid_queries)                                                           \
  COUNTER(downstream_rx_queries)                                                                   \
  COUNTER(external_a_record_queries)                                                               \
  COUNTER(external_a_record_answers)                                                               \
  COUNTER(external_aaaa_record_answers)                                                            \
  COUNTER(external_aaaa_record_queries)                                                            \
  COUNTER(external_unsupported_answers)                                                            \
  COUNTER(external_unsupported_queries)                                                            \
  COUNTER(externally_resolved_queries)                                                             \
  COUNTER(known_domain_queries)                                                                    \
  COUNTER(local_a_record_answers)                                                                  \
  COUNTER(local_aaaa_record_answers)                                                               \
  COUNTER(local_srv_record_answers)                                                                \
  COUNTER(local_unsupported_answers)                                                               \
  COUNTER(unanswered_queries)                                                                      \
  COUNTER(unsupported_queries)                                                                     \
  COUNTER(downstream_tx_responses)                                                                 \
  COUNTER(query_buffer_underflow)                                                                  \
  COUNTER(query_parsing_failure)                                                                   \
  COUNTER(queries_with_additional_rrs)                                                             \
  COUNTER(queries_with_ans_or_authority_rrs)                                                       \
  COUNTER(record_name_overflow)                                                                    \
  HISTOGRAM(downstream_rx_bytes, Bytes)                                                            \
  HISTOGRAM(downstream_rx_query_latency, Milliseconds)                                             \
  HISTOGRAM(downstream_tx_bytes, Bytes)

/**
 * Struct definition for all DNS Filter stats. @see stats_macros.h
 */
struct DnsFilterStats {
  ALL_DNS_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

struct DnsEndpointConfig {
  absl::optional<AddressConstPtrVec> address_list;
  absl::optional<std::string> cluster_name;
  absl::optional<DnsSrvRecordPtr> service_list;
};

using DnsVirtualDomainConfig = absl::flat_hash_map<std::string, DnsEndpointConfig>;
using DnsVirtualDomainConfigSharedPtr = std::shared_ptr<DnsVirtualDomainConfig>;

/**
 * DnsFilter configuration class abstracting access to data necessary for the filter's operation
 */
class DnsFilterEnvoyConfig : public Logger::Loggable<Logger::Id::filter> {
public:
  DnsFilterEnvoyConfig(
      Server::Configuration::ListenerFactoryContext& context,
      const envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig& config);

  DnsFilterStats& stats() const { return stats_; }
  const absl::flat_hash_map<std::string, std::chrono::seconds>& domainTtl() const {
    return domain_ttl_;
  }
  bool forwardQueries() const { return forward_queries_; }
  const std::chrono::milliseconds resolverTimeout() const { return resolver_timeout_; }
  Upstream::ClusterManager& clusterManager() const { return cluster_manager_; }
  uint64_t retryCount() const { return retry_count_; }
  Random::RandomGenerator& random() const { return random_; }
  uint64_t maxPendingLookups() const { return max_pending_lookups_; }
  const envoy::config::core::v3::TypedExtensionConfig& typedDnsResolverConfig() const {
    return typed_dns_resolver_config_;
  }
  const Network::DnsResolverFactory& dnsResolverFactory() const { return *dns_resolver_factory_; }
  Api::Api& api() const { return api_; }
  const TrieLookupTable<DnsVirtualDomainConfigSharedPtr>& getDnsTrie() const {
    return dns_lookup_trie_;
  }

private:
  static DnsFilterStats generateStats(const std::string& stat_prefix, Stats::Scope& scope) {
    const auto final_prefix = absl::StrCat("dns_filter.", stat_prefix);
    return {ALL_DNS_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                 POOL_HISTOGRAM_PREFIX(scope, final_prefix))};
  }

  bool loadServerConfig(
      const envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig::ServerContextConfig&
          config,
      envoy::data::dns::v3::DnsTable& table);

  void addEndpointToSuffix(const absl::string_view suffix, const absl::string_view domain_name,
                           DnsEndpointConfig& endpoint_config);

  Stats::Scope& root_scope_;
  Upstream::ClusterManager& cluster_manager_;
  Network::DnsResolverSharedPtr resolver_;
  Api::Api& api_;

  mutable DnsFilterStats stats_;

  TrieLookupTable<DnsVirtualDomainConfigSharedPtr> dns_lookup_trie_;
  absl::flat_hash_map<std::string, std::chrono::seconds> domain_ttl_;
  bool forward_queries_;
  uint64_t retry_count_;
  std::chrono::milliseconds resolver_timeout_;
  Random::RandomGenerator& random_;
  uint64_t max_pending_lookups_;
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config_;
  Network::DnsResolverFactory* dns_resolver_factory_;
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
  DnsFilter(Network::UdpReadFilterCallbacks& callbacks,
            const DnsFilterEnvoyConfigSharedPtr& config);

  // Network::UdpListenerReadFilter callbacks
  Network::FilterStatus onData(Network::UdpRecvData& client_request) override;
  Network::FilterStatus onReceiveError(Api::IoError::IoErrorCode error_code) override;

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
   * @return std::chrono::seconds retrieves the configured per domain TTL to be inserted into answer
   * records
   */
  std::chrono::seconds getDomainTTL(const absl::string_view domain);

  /**
   * @brief Resolves a hostname query from configured clusters
   *
   * @param context object containing the query context
   * @param query query object containing the name to be resolved
   * @return bool true if the requested name matched a cluster and an answer record was constructed
   */
  bool resolveClusterHost(DnsQueryContextPtr& context, const DnsQueryRecord& query);

  /**
   * @brief Resolves a service query from configured clusters
   *
   * @param context object containing the query context
   * @param query query object containing the name to be resolved
   * @return bool true if the requested name matched a cluster and an answer record was constructed
   */
  bool resolveClusterService(DnsQueryContextPtr& context, const DnsQueryRecord& query);

  /**
   * @brief Resolves the supplied query from configured clusters
   *
   * @param context object containing the query context
   * @param query query object containing the name to be resolved
   * @return bool true if the requested name matched a cluster and an answer record was constructed
   */
  bool resolveViaClusters(DnsQueryContextPtr& context, const DnsQueryRecord& query);

  /**
   * @brief Resolves the supplied query from the configured set of domains
   *
   * @param context object containing the query context
   * @param query query object containing the name to be resolved
   * @return bool true if the requested name matched a cluster and an answer record was constructed
   */
  bool resolveConfiguredDomain(DnsQueryContextPtr& context, const DnsQueryRecord& query);

  /**
   * @brief Resolves the supplied query from configured services
   *
   * @param context object containing the query context
   * @param query query object containing the name to be resolved
   * @return bool true if the requested name matched a cluster and an answer record was constructed
   */
  bool resolveConfiguredService(DnsQueryContextPtr& context, const DnsQueryRecord& query);

  /**
   * @brief Resolves the supplied query from configured hostnames or services
   *
   * @param context object containing the query context
   * @param query query object containing the name to be resolved
   * @return bool true if the requested name matches a configured domain and answer records can be
   * constructed
   */
  bool resolveViaConfiguredHosts(DnsQueryContextPtr& context, const DnsQueryRecord& query);

  /**
   * @brief Increment the counter for the given query type for external queries
   *
   * @param query_type indicate the type of record being resolved (A, AAAA, or other).
   */
  void incrementExternalQueryTypeCount(const uint16_t query_type) {
    switch (query_type) {
    case DNS_RECORD_TYPE_A:
      config_->stats().external_a_record_queries_.inc();
      break;
    case DNS_RECORD_TYPE_AAAA:
      config_->stats().external_aaaa_record_queries_.inc();
      break;
    default:
      config_->stats().external_unsupported_queries_.inc();
      break;
    }
  }

  /**
   * @brief Increment the counter for the parsed query type
   *
   * @param queries a vector of all the incoming queries received from a client
   */
  void incrementQueryTypeCount(const DnsQueryPtrVec& queries) {
    for (const auto& query : queries) {
      incrementQueryTypeCount(query->type_);
    }
  }

  /**
   * @brief Increment the counter for the given query type.
   *
   * @param query_type indicate the type of record being resolved (A, AAAA, or other).
   */
  void incrementQueryTypeCount(const uint16_t query_type) {
    switch (query_type) {
    case DNS_RECORD_TYPE_A:
      config_->stats().a_record_queries_.inc();
      break;
    case DNS_RECORD_TYPE_AAAA:
      config_->stats().aaaa_record_queries_.inc();
      break;
    case DNS_RECORD_TYPE_SRV:
      config_->stats().srv_record_queries_.inc();
      break;
    default:
      config_->stats().unsupported_queries_.inc();
      break;
    }
  }

  /**
   * @brief Increment the counter for answers for the given query type resolved via cluster names
   *
   * @param query_type indicate the type of answer record returned to the client
   */
  void incrementClusterQueryTypeAnswerCount(const uint16_t query_type) {
    switch (query_type) {
    case DNS_RECORD_TYPE_A:
      config_->stats().cluster_a_record_answers_.inc();
      break;
    case DNS_RECORD_TYPE_AAAA:
      config_->stats().cluster_aaaa_record_answers_.inc();
      break;
    case DNS_RECORD_TYPE_SRV:
      config_->stats().cluster_srv_record_answers_.inc();
      break;
    default:
      config_->stats().cluster_unsupported_answers_.inc();
      break;
    }
  }

  /**
   * @brief Increment the counter for answers for the given query type resolved from the local
   * configuration.
   *
   * @param query_type indicate the type of answer record returned to the client
   */
  void incrementLocalQueryTypeAnswerCount(const uint16_t query_type) {
    switch (query_type) {
    case DNS_RECORD_TYPE_A:
      config_->stats().local_a_record_answers_.inc();
      break;
    case DNS_RECORD_TYPE_AAAA:
      config_->stats().local_aaaa_record_answers_.inc();
      break;
    case DNS_RECORD_TYPE_SRV:
      config_->stats().local_srv_record_answers_.inc();
      break;
    default:
      config_->stats().local_unsupported_answers_.inc();
      break;
    }
  }

  /**
   * @brief Increment the counter for answers for the given query type resolved via an external
   * resolver
   *
   * @param query_type indicate the type of answer record returned to the client
   */
  void incrementExternalQueryTypeAnswerCount(const uint16_t query_type) {
    switch (query_type) {
    case DNS_RECORD_TYPE_A:
      config_->stats().external_a_record_answers_.inc();
      break;
    case DNS_RECORD_TYPE_AAAA:
      config_->stats().external_aaaa_record_answers_.inc();
      break;
    default:
      config_->stats().external_unsupported_answers_.inc();
      break;
    }
  }

  /**
   * @brief Helper function to retrieve the Endpoint configuration for a requested domain
   */
  const DnsEndpointConfig* getEndpointConfigForDomain(const absl::string_view domain);

  /**
   * @brief Helper function to retrieve the Service Config for a requested domain
   */
  const DnsSrvRecord* getServiceConfigForDomain(const absl::string_view domain);

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
  DnsFilterResolverPtr resolver_;
  Network::Address::InstanceConstSharedPtr local_;
  Network::Address::InstanceConstSharedPtr peer_;
  DnsFilterResolverCallback resolver_callback_;
};

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

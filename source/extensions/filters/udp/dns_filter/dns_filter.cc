#include "extensions/filters/udp/dns_filter/dns_filter.h"

#include "envoy/network/listener.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "common/config/datasource.h"
#include "common/network/address_impl.h"
#include "common/protobuf/message_validator_impl.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

static constexpr std::chrono::milliseconds DEFAULT_RESOLVER_TIMEOUT{500};
static constexpr std::chrono::seconds DEFAULT_RESOLVER_TTL{300};

DnsFilterEnvoyConfig::DnsFilterEnvoyConfig(
    Server::Configuration::ListenerFactoryContext& context,
    const envoy::extensions::filters::udp::dns_filter::v3alpha::DnsFilterConfig& config)
    : root_scope_(context.scope()), cluster_manager_(context.clusterManager()), api_(context.api()),
      stats_(generateStats(config.stat_prefix(), root_scope_)), random_(context.random()) {
  using envoy::extensions::filters::udp::dns_filter::v3alpha::DnsFilterConfig;

  const auto& server_config = config.server_config();

  envoy::data::dns::v3::DnsTable dns_table;
  bool result = loadServerConfig(server_config, dns_table);
  ENVOY_LOG(debug, "Loading DNS table from external file: {}", result ? "Success" : "Failure");

  retry_count_ = dns_table.external_retry_count();

  const size_t entries = dns_table.virtual_domains().size();
  virtual_domains_.reserve(entries);
  for (const auto& virtual_domain : dns_table.virtual_domains()) {
    AddressConstPtrVec addrs{};
    absl::string_view cluster_name;
    if (virtual_domain.endpoint().has_address_list()) {
      const auto& address_list = virtual_domain.endpoint().address_list().address();
      addrs.reserve(address_list.size());

      // Shuffle the configured addresses. We store the addresses starting at a random
      // list index so that we do not always return answers in the same order as the IPs
      // are configured.
      size_t i = random_.random();

      // Creating the IP address will throw an exception if the address string is malformed
      for (auto index = 0; index < address_list.size(); index++) {
        const auto address_iter = std::next(address_list.begin(), (i++ % address_list.size()));
        auto ipaddr = Network::Utility::parseInternetAddress(*address_iter, 0 /* port */);
        addrs.push_back(std::move(ipaddr));
      }
    } else {
      cluster_name = virtual_domain.endpoint().cluster_name();
    }

    DnsEndpointConfig endpoint_config;
    endpoint_config.address_list = absl::make_optional<AddressConstPtrVec>(std::move(addrs));
    endpoint_config.cluster_name = absl::make_optional<std::string>(cluster_name);

    virtual_domains_.emplace(virtual_domain.name(), endpoint_config);

    std::chrono::seconds ttl = virtual_domain.has_answer_ttl()
                                   ? std::chrono::seconds(virtual_domain.answer_ttl().seconds())
                                   : DEFAULT_RESOLVER_TTL;
    domain_ttl_.emplace(virtual_domain.name(), ttl);
  }

  // Add known domain suffixes
  known_suffixes_.reserve(dns_table.known_suffixes().size());
  for (const auto& suffix : dns_table.known_suffixes()) {
    auto matcher_ptr = std::make_unique<Matchers::StringMatcherImpl>(suffix);
    known_suffixes_.push_back(std::move(matcher_ptr));
  }

  forward_queries_ = config.has_client_config();
  if (forward_queries_) {
    const auto& client_config = config.client_config();
    const auto& upstream_resolvers = client_config.upstream_resolvers();
    resolvers_.reserve(upstream_resolvers.size());
    for (const auto& resolver : upstream_resolvers) {
      auto ipaddr = Network::Utility::protobufAddressToAddress(resolver);
      resolvers_.emplace_back(std::move(ipaddr));
    }
    resolver_timeout_ = std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(
        client_config, resolver_timeout, DEFAULT_RESOLVER_TIMEOUT.count()));

    max_pending_lookups_ = client_config.max_pending_lookups();
  }
}

bool DnsFilterEnvoyConfig::loadServerConfig(
    const envoy::extensions::filters::udp::dns_filter::v3alpha::DnsFilterConfig::
        ServerContextConfig& config,
    envoy::data::dns::v3::DnsTable& table) {
  using envoy::data::dns::v3::DnsTable;

  if (config.has_inline_dns_table()) {
    table = config.inline_dns_table();
    return true;
  }

  const auto& datasource = config.external_dns_table();
  bool data_source_loaded = false;
  try {
    // Data structure is deduced from the file extension. If the data is not read an exception
    // is thrown. If no table can be read, the filter will refer all queries to an external
    // DNS server, if configured, otherwise all queries will be responded to with Name Error.
    MessageUtil::loadFromFile(datasource.filename(), table,
                              ProtobufMessage::getNullValidationVisitor(), api_,
                              false /* do_boosting */);
    data_source_loaded = true;
  } catch (const ProtobufMessage::UnknownProtoFieldException& e) {
    ENVOY_LOG(warn, "Invalid field in DNS Filter datasource configuration: {}", e.what());
  } catch (const EnvoyException& e) {
    ENVOY_LOG(warn, "Filesystem DNS Filter config update failure: {}", e.what());
  }
  return data_source_loaded;
}

DnsFilter::DnsFilter(Network::UdpReadFilterCallbacks& callbacks,
                     const DnsFilterEnvoyConfigSharedPtr& config)
    : UdpListenerReadFilter(callbacks), config_(config), listener_(callbacks.udpListener()),
      cluster_manager_(config_->clusterManager()),
      message_parser_(config->forwardQueries(), listener_.dispatcher().timeSource(),
                      config->retryCount(), config->random(),
                      config_->stats().downstream_rx_query_latency_) {
  // This callback is executed when the dns resolution completes. At that time of a response by the
  // resolver, we build an answer record from each IP returned then send a response to the client
  resolver_callback_ = [this](DnsQueryContextPtr context, const DnsQueryRecord* query,
                              AddressConstPtrVec& iplist) -> void {
    if (context->resolution_status_ != Network::DnsResolver::ResolutionStatus::Success &&
        context->retry_ > 0) {
      --context->retry_;
      ENVOY_LOG(debug, "resolving name [{}] via external resolvers [retry {}]", query->name_,
                context->retry_);
      resolver_->resolveExternalQuery(std::move(context), query);
      return;
    }

    config_->stats().externally_resolved_queries_.inc();
    if (iplist.empty()) {
      config_->stats().unanswered_queries_.inc();
    }

    incrementExternalQueryTypeCount(query->type_);
    for (const auto& ip : iplist) {
      incrementExternalQueryTypeAnswerCount(query->type_);
      const std::chrono::seconds ttl = getDomainTTL(query->name_);
      message_parser_.buildDnsAnswerRecord(context, *query, ttl, std::move(ip));
    }
    sendDnsResponse(std::move(context));
  };

  resolver_ = std::make_unique<DnsFilterResolver>(resolver_callback_, config->resolvers(),
                                                  config->resolverTimeout(), listener_.dispatcher(),
                                                  config->maxPendingLookups());
}

void DnsFilter::onData(Network::UdpRecvData& client_request) {
  config_->stats().downstream_rx_bytes_.recordValue(client_request.buffer_->length());
  config_->stats().downstream_rx_queries_.inc();

  // Setup counters for the parser
  DnsParserCounters parser_counters(config_->stats().query_buffer_underflow_,
                                    config_->stats().record_name_overflow_,
                                    config_->stats().query_parsing_failure_);

  // Parse the query, if it fails return an response to the client
  DnsQueryContextPtr query_context =
      message_parser_.createQueryContext(client_request, parser_counters);
  incrementQueryTypeCount(query_context->queries_);
  if (!query_context->parse_status_) {
    config_->stats().downstream_rx_invalid_queries_.inc();
    sendDnsResponse(std::move(query_context));
    return;
  }

  // Resolve the requested name
  auto response = getResponseForQuery(query_context);

  // We were not able to satisfy the request locally. Return an empty response to the client
  if (response == DnsLookupResponseCode::Failure) {
    sendDnsResponse(std::move(query_context));
    return;
  }

  // Externally resolved. We'll respond to the client when the external DNS resolution callback
  // is executed
  if (response == DnsLookupResponseCode::External) {
    return;
  }

  // We have an answer. Send it to the client
  sendDnsResponse(std::move(query_context));
}

void DnsFilter::sendDnsResponse(DnsQueryContextPtr query_context) {
  Buffer::OwnedImpl response;

  // Serializes the generated response to the parsed query from the client. If there is a
  // parsing error or the incoming query is invalid, we will still generate a valid DNS response
  message_parser_.buildResponseBuffer(query_context, response);
  config_->stats().downstream_tx_responses_.inc();
  config_->stats().downstream_tx_bytes_.recordValue(response.length());
  Network::UdpSendData response_data{query_context->local_->ip(), *(query_context->peer_),
                                     response};
  listener_.send(response_data);
}

DnsLookupResponseCode DnsFilter::getResponseForQuery(DnsQueryContextPtr& context) {
  /* It appears to be a rare case where we would have more than one query in a single request.
   * It is allowed by the protocol but not widely supported:
   *
   * See: https://www.ietf.org/rfc/rfc1035.txt
   *
   * The question section is used to carry the "question" in most queries,
   * i.e., the parameters that define what is being asked. The section
   * contains QDCOUNT (usually 1) entries.
   */
  for (const auto& query : context->queries_) {
    // Try to resolve the query locally. If forwarding the query externally is disabled we will
    // always attempt to resolve with the configured domains
    if (isKnownDomain(query->name_) || !config_->forwardQueries()) {
      // Determine whether the name is a cluster. Move on to the next query if successful
      if (resolveViaClusters(context, *query)) {
        continue;
      }

      // Determine whether we an answer this query with the static configuration
      if (resolveViaConfiguredHosts(context, *query)) {
        continue;
      }
    }

    ENVOY_LOG(debug, "resolving name [{}] via external resolvers", query->name_);
    resolver_->resolveExternalQuery(std::move(context), query.get());

    return DnsLookupResponseCode::External;
  }

  if (context->answers_.empty()) {
    config_->stats().unanswered_queries_.inc();
    return DnsLookupResponseCode::Failure;
  }
  return DnsLookupResponseCode::Success;
}

std::chrono::seconds DnsFilter::getDomainTTL(const absl::string_view domain) {
  const auto& domain_ttl_config = config_->domainTtl();
  const auto& iter = domain_ttl_config.find(domain);

  if (iter == domain_ttl_config.end()) {
    return DEFAULT_RESOLVER_TTL;
  }
  return iter->second;
}

bool DnsFilter::isKnownDomain(const absl::string_view domain_name) {
  const auto& known_suffixes = config_->knownSuffixes();

  // If we don't have a list of allowlisted domain suffixes, we will resolve the name with an
  // external DNS server
  if (known_suffixes.empty()) {
    ENVOY_LOG(debug, "Known domains list is empty");
    return false;
  }

  // TODO(abaptiste): Use a trie to find a match instead of iterating through the list
  for (auto& suffix : known_suffixes) {
    if (suffix->match(domain_name)) {
      config_->stats().known_domain_queries_.inc();
      return true;
    }
  }
  return false;
}

const DnsEndpointConfig* DnsFilter::getEndpointConfigForDomain(const absl::string_view domain) {
  const auto& domains = config_->domains();
  const auto iter = domains.find(domain);
  if (iter == domains.end()) {
    ENVOY_LOG(debug, "No endpoint configuration exists for [{}]", domain);
    return nullptr;
  }
  return &(iter->second);
}

const AddressConstPtrVec* DnsFilter::getAddressListForDomain(const absl::string_view domain) {
  const DnsEndpointConfig* endpoint_config = getEndpointConfigForDomain(domain);
  if (endpoint_config != nullptr && endpoint_config->address_list.has_value()) {
    return &(endpoint_config->address_list.value());
  }
  return nullptr;
}

const absl::string_view DnsFilter::getClusterNameForDomain(const absl::string_view domain) {
  const DnsEndpointConfig* endpoint_config = getEndpointConfigForDomain(domain);
  if (endpoint_config != nullptr && endpoint_config->cluster_name.has_value()) {
    return endpoint_config->cluster_name.value();
  }
  return {};
}

bool DnsFilter::resolveViaClusters(DnsQueryContextPtr& context, const DnsQueryRecord& query) {
  // Determine if the domain name is being redirected to a cluster
  const auto cluster_name = getClusterNameForDomain(query.name_);
  absl::string_view lookup_name;
  if (!cluster_name.empty()) {
    lookup_name = cluster_name;
  } else {
    lookup_name = query.name_;
  }

  Upstream::ThreadLocalCluster* cluster = cluster_manager_.get(lookup_name);
  if (cluster == nullptr) {
    ENVOY_LOG(debug, "Did not find a cluster for name [{}]", lookup_name);
    return false;
  }

  // TODO(abaptiste): consider using host weights when returning answer addresses

  // Return the address for all discovered endpoints
  size_t discovered_endpoints = 0;
  const std::chrono::seconds ttl = getDomainTTL(query.name_);
  for (const auto& hostsets : cluster->prioritySet().hostSetsPerPriority()) {
    for (const auto& host : hostsets->hosts()) {
      ++discovered_endpoints;
      ENVOY_LOG(debug, "using cluster host address {} for domain [{}]",
                host->address()->ip()->addressAsString(), lookup_name);
      incrementClusterQueryTypeAnswerCount(query.type_);
      message_parser_.buildDnsAnswerRecord(context, query, ttl, host->address());
    }
  }
  return (discovered_endpoints != 0);
}

bool DnsFilter::resolveViaConfiguredHosts(DnsQueryContextPtr& context,
                                          const DnsQueryRecord& query) {
  const auto* configured_address_list = getAddressListForDomain(query.name_);
  if (configured_address_list == nullptr) {
    ENVOY_LOG(debug, "Domain [{}] address list was not found", query.name_);
    return false;
  }

  if (configured_address_list->empty()) {
    ENVOY_LOG(debug, "Domain [{}] address list is empty", query.name_);
    return false;
  }

  // Build an answer record from each configured IP address
  uint64_t hosts_found = 0;
  for (const auto& configured_address : *configured_address_list) {
    ASSERT(configured_address != nullptr);
    incrementLocalQueryTypeAnswerCount(query.type_);
    ENVOY_LOG(debug, "using local address {} for domain [{}]",
              configured_address->ip()->addressAsString(), query.name_);
    ++hosts_found;
    const std::chrono::seconds ttl = getDomainTTL(query.name_);
    message_parser_.buildDnsAnswerRecord(context, query, ttl, configured_address);
  }
  return (hosts_found != 0);
}

void DnsFilter::onReceiveError(Api::IoError::IoErrorCode error_code) {
  config_->stats().downstream_rx_errors_.inc();
  UNREFERENCED_PARAMETER(error_code);
}

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

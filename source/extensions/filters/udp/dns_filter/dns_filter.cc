#include "extensions/filters/udp/dns_filter/dns_filter.h"

#include "envoy/network/listener.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "common/config/datasource.h"
#include "common/network/address_impl.h"
#include "common/protobuf/message_validator_impl.h"

#include "extensions/filters/udp/dns_filter/dns_filter_utils.h"

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
      stats_(generateStats(config.stat_prefix(), root_scope_)),
      resolver_timeout_(DEFAULT_RESOLVER_TIMEOUT), random_(context.api().randomGenerator()) {
  using envoy::extensions::filters::udp::dns_filter::v3alpha::DnsFilterConfig;

  const auto& server_config = config.server_config();

  envoy::data::dns::v3::DnsTable dns_table;
  bool result = loadServerConfig(server_config, dns_table);
  ENVOY_LOG(debug, "Loading DNS table from external file: {}", result ? "Success" : "Failure");

  retry_count_ = dns_table.external_retry_count();

  virtual_domains_.reserve(dns_table.virtual_domains().size());
  for (const auto& virtual_domain : dns_table.virtual_domains()) {
    AddressConstPtrVec addrs{};

    const absl::string_view domain_name = virtual_domain.name();
    ENVOY_LOG(trace, "Loading configuration for domain: {}", domain_name);

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

      // If the domain already exists with a different endpoint config, update the address_list
      // with the data from the config
      if (virtual_domains_.contains(domain_name)) {
        auto& addr_vec = virtual_domains_[domain_name].address_list.value();
        addr_vec.reserve(addr_vec.size() + addrs.size());
        std::move(addrs.begin(), addrs.end(), std::inserter(addr_vec, addr_vec.end()));
      } else {
        DnsEndpointConfig endpoint_config{};
        endpoint_config.address_list = absl::make_optional<AddressConstPtrVec>(std::move(addrs));
        virtual_domains_.emplace(std::string(domain_name), std::move(endpoint_config));
      }
    }

    if (virtual_domain.endpoint().has_service_list()) {
      const auto& dns_service_list = virtual_domain.endpoint().service_list();
      for (const auto& dns_service : dns_service_list.services()) {

        // Each service should be its own domain in the stored config. The filter will see
        // the full service name in queries on the wire. The protocol string returned will be empty
        // if a numeric protocol is configured and we cannot resolve its name
        const std::string proto = Utils::getProtoName(dns_service.protocol());
        if (proto.empty()) {
          continue;
        }
        const std::chrono::seconds ttl = std::chrono::seconds(dns_service.ttl().seconds());

        // Generate the full name for the DNS service. All input parameters are populated
        // strings enforced by the message definition
        const std::string full_service_name =
            Utils::buildServiceName(dns_service.service_name(), proto, virtual_domain.name());

        DnsSrvRecordPtr service_record_ptr =
            std::make_unique<DnsSrvRecord>(full_service_name, proto, ttl);

        // Store service targets. We require at least one target to be present. The target should
        // be a fully qualified domain name. If the target name is not a fully qualified name, we
        // will consider this name to be that of a cluster
        for (const auto& target : dns_service.targets()) {
          DnsSrvRecord::DnsTargetAttributes attributes{};
          attributes.priority = target.priority();
          attributes.weight = target.weight();
          attributes.port = target.port();

          absl::string_view target_name = target.host_name();
          if (target_name.empty()) {
            target_name = target.cluster_name();
            attributes.is_cluster = true;
          }

          ENVOY_LOG(trace, "Storing service {} target {}", full_service_name, target_name);
          service_record_ptr->addTarget(target_name, attributes);
        }

        DnsEndpointConfig endpoint_config{};
        endpoint_config.service_list =
            absl::make_optional<DnsSrvRecordPtr>(std::move(service_record_ptr));
        virtual_domains_.emplace(full_service_name, std::move(endpoint_config));
      }
    }

    // A DNS name can be redirected to only one cluster.
    const absl::string_view cluster_name = virtual_domain.endpoint().cluster_name();
    if (!cluster_name.empty()) {
      DnsEndpointConfig endpoint_config{};
      endpoint_config.cluster_name = absl::make_optional<std::string>(cluster_name);
      virtual_domains_.emplace(domain_name, std::move(endpoint_config));
    }

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
  // This callback is executed when the dns resolution completes. At that time of a response by
  // the resolver, we build an answer record from each IP returned then send a response to the
  // client
  resolver_callback_ = [this](DnsQueryContextPtr context, const DnsQueryRecord* query,
                              AddressConstPtrVec& iplist) -> void {
    // We cannot retry the resolution if ares returns without a response. The ares context
    // is still dirty and will result in a segfault when it is freed during a subsequent resolve
    // call from here. We will retry resolutions for pending lookups only
    if (context->resolution_status_ != Network::DnsResolver::ResolutionStatus::Success &&
        !context->in_callback_ && context->retry_ > 0) {
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
      message_parser_.storeDnsAnswerRecord(context, *query, ttl, std::move(ip));
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

  // Resolve the requested name and respond to the client. If the return code is
  // External, we will respond to the client when the upstream resolver returns
  if (getResponseForQuery(query_context) == DnsLookupResponseCode::External) {
    return;
  }

  // We have an answer, it might be "No Answer". Send it to the client
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

bool DnsFilter::resolveViaConfiguredHosts(DnsQueryContextPtr& context,
                                          const DnsQueryRecord& query) {
  switch (query.type_) {
  case DNS_RECORD_TYPE_A:
  case DNS_RECORD_TYPE_AAAA:
    return resolveConfiguredDomain(context, query);
  case DNS_RECORD_TYPE_SRV:
    return resolveConfiguredService(context, query);
  default:
    return false;
  }
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

const DnsSrvRecord* DnsFilter::getServiceConfigForDomain(const absl::string_view domain) {
  const DnsEndpointConfig* endpoint_config = getEndpointConfigForDomain(domain);
  if (endpoint_config != nullptr && endpoint_config->service_list.has_value()) {
    return endpoint_config->service_list.value().get();
  }
  return nullptr;
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

bool DnsFilter::resolveClusterService(DnsQueryContextPtr& context, const DnsQueryRecord& query) {
  size_t cluster_endpoints = 0;

  // Get the service_list config for the domain
  const auto* service_config = getServiceConfigForDomain(query.name_);
  if (service_config != nullptr) {

    // We can redirect to more than one cluster, but only one is supported
    const auto& cluster_target = service_config->targets_.begin();
    const auto& target_name = cluster_target->first;
    const auto& attributes = cluster_target->second;

    if (!attributes.is_cluster) {
      ENVOY_LOG(trace, "Service target [{}] is not a cluster", target_name);
      return false;
    }

    // Determine if there is a cluster
    Upstream::ThreadLocalCluster* cluster = cluster_manager_.get(target_name);
    if (cluster == nullptr) {
      ENVOY_LOG(trace, "No cluster found for service target: {}", target_name);
      return false;
    }

    // Add a service record for each cluster endpoint using the cluster name
    const std::chrono::seconds ttl = getDomainTTL(target_name);
    for (const auto& hostsets : cluster->prioritySet().hostSetsPerPriority()) {
      for (const auto& host : hostsets->hosts()) {

        // If the target port is zero, use the port from the cluster host.
        // If the cluster host port is zero also, then this is the value that will
        // appear in the service record. Zero is a permitted value in the record
        DnsSrvRecord::DnsTargetAttributes new_attributes = attributes;
        if (!new_attributes.port) {
          new_attributes.port = host->address()->ip()->port();
        }

        // Create the service record element and increment the SRV record answer count
        auto config = std::make_unique<DnsSrvRecord>(service_config->name_, service_config->proto_,
                                                     service_config->ttl_);

        config->addTarget(target_name, new_attributes);
        message_parser_.storeDnsSrvAnswerRecord(context, query, std::move(config));
        incrementClusterQueryTypeAnswerCount(query.type_);

        // Return the address for all discovered endpoints
        ENVOY_LOG(debug, "using host address {} for cluster [{}]",
                  host->address()->ip()->addressAsString(), target_name);

        // We have to determine the address type here so that we increment the correct counter
        const auto type = Utils::getAddressRecordType(host->address());
        if (type.has_value() &&
            message_parser_.storeDnsAdditionalRecord(context, target_name, type.value(),
                                                     query.class_, ttl, host->address())) {
          ++cluster_endpoints;
          incrementClusterQueryTypeAnswerCount(type.value());
        }
      }
    }
  }
  return (cluster_endpoints != 0);
}

bool DnsFilter::resolveClusterHost(DnsQueryContextPtr& context, const DnsQueryRecord& query) {
  // Determine if the domain name is being redirected to a cluster
  const auto cluster_name = getClusterNameForDomain(query.name_);
  absl::string_view lookup_name;
  if (!cluster_name.empty()) {
    lookup_name = cluster_name;
  } else {
    lookup_name = query.name_;
  }

  // Return an address for all discovered endpoints. The address and query type must match
  // for the host to be included in the response
  size_t cluster_endpoints = 0;
  Upstream::ThreadLocalCluster* cluster = cluster_manager_.get(lookup_name);
  if (cluster != nullptr) {
    // TODO(abaptiste): consider using host weights when returning answer addresses
    const std::chrono::seconds ttl = getDomainTTL(lookup_name);

    for (const auto& hostsets : cluster->prioritySet().hostSetsPerPriority()) {
      for (const auto& host : hostsets->hosts()) {
        // Return the address for all discovered endpoints
        ENVOY_LOG(debug, "using cluster host address {} for domain [{}]",
                  host->address()->ip()->addressAsString(), lookup_name);
        if (message_parser_.storeDnsAnswerRecord(context, query, ttl, host->address())) {
          incrementClusterQueryTypeAnswerCount(query.type_);
          ++cluster_endpoints;
        }
      }
    }
  }
  return (cluster_endpoints != 0);
}

bool DnsFilter::resolveViaClusters(DnsQueryContextPtr& context, const DnsQueryRecord& query) {
  switch (query.type_) {
  case DNS_RECORD_TYPE_SRV:
    return resolveClusterService(context, query);
  case DNS_RECORD_TYPE_A:
  case DNS_RECORD_TYPE_AAAA:
    return resolveClusterHost(context, query);
  default:
    // unsupported query type
    return false;
  }
}

bool DnsFilter::resolveConfiguredDomain(DnsQueryContextPtr& context, const DnsQueryRecord& query) {
  const auto* configured_address_list = getAddressListForDomain(query.name_);
  uint64_t hosts_found = 0;
  if (configured_address_list != nullptr) {
    // Build an answer record from each configured IP address
    for (const auto& configured_address : *configured_address_list) {
      ASSERT(configured_address != nullptr);
      ENVOY_LOG(trace, "using local address {} for domain [{}]",
                configured_address->ip()->addressAsString(), query.name_);
      ++hosts_found;
      const std::chrono::seconds ttl = getDomainTTL(query.name_);
      if (message_parser_.storeDnsAnswerRecord(context, query, ttl, configured_address)) {
        incrementLocalQueryTypeAnswerCount(query.type_);
      }
    }
  }
  return (hosts_found != 0);
}

bool DnsFilter::resolveConfiguredService(DnsQueryContextPtr& context, const DnsQueryRecord& query) {
  const auto* service_config = getServiceConfigForDomain(query.name_);

  size_t targets_discovered = 0;
  if (service_config != nullptr) {
    // for each service target address, we must resolve the target's IP. The target record does not
    // specify the address type, so we must deduce it when building the record. It is possible that
    // the configured target's IP addresses are a mix of A and AAAA records.
    for (const auto& [target_name, attributes] : service_config->targets_) {
      const auto* configured_address_list = getAddressListForDomain(target_name);

      if (configured_address_list != nullptr) {
        // Build an SRV answer record for the service. We need a new SRV record for each target.
        // Although the same class is used, the target storage is different than the way the service
        // config is modeled. We store one SrvRecord per target so that we can enforce the response
        // size limit when serializing the answers to the client
        ENVOY_LOG(trace, "Adding srv record for target [{}]", target_name);

        incrementLocalQueryTypeAnswerCount(query.type_);
        auto config = std::make_unique<DnsSrvRecord>(service_config->name_, service_config->proto_,
                                                     service_config->ttl_);
        config->addTarget(target_name, attributes);
        message_parser_.storeDnsSrvAnswerRecord(context, query, std::move(config));

        for (const auto& configured_address : *configured_address_list) {
          ASSERT(configured_address != nullptr);

          // Since there is no type, only a name, we must determine the record type from its address
          ENVOY_LOG(trace, "using address {} for target [{}] in SRV record",
                    configured_address->ip()->addressAsString(), target_name);
          const std::chrono::seconds ttl = getDomainTTL(target_name);

          const auto type = Utils::getAddressRecordType(configured_address);
          if (type.has_value()) {
            incrementLocalQueryTypeAnswerCount(type.value());
            message_parser_.storeDnsAdditionalRecord(context, target_name, type.value(),
                                                     query.class_, ttl, configured_address);
            ++targets_discovered;
          }
        }
      }
    }
  }
  return (targets_discovered != 0);
}

void DnsFilter::onReceiveError(Api::IoError::IoErrorCode error_code) {
  config_->stats().downstream_rx_errors_.inc();
  UNREFERENCED_PARAMETER(error_code);
}

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

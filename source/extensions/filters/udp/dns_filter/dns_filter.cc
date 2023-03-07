#include "source/extensions/filters/udp/dns_filter/dns_filter.h"

#include "envoy/network/listener.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/config/datasource.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/dns_resolver/dns_factory_util.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/extensions/filters/udp/dns_filter/dns_filter_utils.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {

static constexpr std::chrono::milliseconds DEFAULT_RESOLVER_TIMEOUT{500};
static constexpr std::chrono::seconds DEFAULT_RESOLVER_TTL{300};

DnsFilterEnvoyConfig::DnsFilterEnvoyConfig(
    Server::Configuration::ListenerFactoryContext& context,
    const envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig& config)
    : root_scope_(context.scope()), cluster_manager_(context.clusterManager()), api_(context.api()),
      stats_(generateStats(config.stat_prefix(), root_scope_)),
      resolver_timeout_(DEFAULT_RESOLVER_TIMEOUT), random_(context.api().randomGenerator()) {
  using envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig;

  const auto& server_config = config.server_config();

  envoy::data::dns::v3::DnsTable dns_table;
  bool result = loadServerConfig(server_config, dns_table);
  ENVOY_LOG(debug, "Loading DNS table from external file: {}", result ? "Success" : "Failure");

  retry_count_ = dns_table.external_retry_count();

  for (const auto& virtual_domain : dns_table.virtual_domains()) {
    AddressConstPtrVec addrs{};

    const absl::string_view domain_name = virtual_domain.name();
    const absl::string_view suffix = Utils::getDomainSuffix(domain_name);
    ENVOY_LOG(trace, "Loading configuration for domain: {}. Suffix: {}", domain_name, suffix);

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

      DnsEndpointConfig endpoint_config{};

      // Check whether the trie contains an entry for this domain
      auto virtual_domains = dns_lookup_trie_.find(suffix);
      if (virtual_domains != nullptr) {
        // The suffix already has a node in the trie

        auto existing_endpoint_config = virtual_domains->find(domain_name);
        if (existing_endpoint_config != virtual_domains->end()) {
          // Update the existing endpoint config with the new addresses

          auto& addr_vec = existing_endpoint_config->second.address_list.value();
          addr_vec.reserve(addr_vec.size() + addrs.size());
          std::move(addrs.begin(), addrs.end(), std::inserter(addr_vec, addr_vec.end()));
        } else {
          // Add a new endpoint config for the new domain
          endpoint_config.address_list = absl::make_optional<AddressConstPtrVec>(std::move(addrs));
          virtual_domains->emplace(std::string(domain_name), std::move(endpoint_config));
        }
      } else {
        endpoint_config.address_list = absl::make_optional<AddressConstPtrVec>(std::move(addrs));
        addEndpointToSuffix(suffix, domain_name, endpoint_config);
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

        auto virtual_domains = dns_lookup_trie_.find(suffix);
        if (virtual_domains != nullptr) {
          virtual_domains->emplace(full_service_name, std::move(endpoint_config));
        }
      }
    }

    // A DNS name can be redirected to only one cluster.
    const absl::string_view cluster_name = virtual_domain.endpoint().cluster_name();
    if (!cluster_name.empty()) {
      DnsEndpointConfig endpoint_config{};
      endpoint_config.cluster_name = absl::make_optional<std::string>(cluster_name);

      // See if there's a suffix already configured
      auto virtual_domains = dns_lookup_trie_.find(suffix);
      if (virtual_domains == nullptr) {
        addEndpointToSuffix(suffix, domain_name, endpoint_config);
      } else {
        // A domain can be redirected to one cluster. If it appears multiple times, the first
        // entry is the only one used
        if (virtual_domains->find(domain_name) == virtual_domains->end()) {
          virtual_domains->emplace(domain_name, std::move(endpoint_config));
        }
      }
    }

    std::chrono::seconds ttl = virtual_domain.has_answer_ttl()
                                   ? std::chrono::seconds(virtual_domain.answer_ttl().seconds())
                                   : DEFAULT_RESOLVER_TTL;
    domain_ttl_.emplace(virtual_domain.name(), ttl);
  }

  forward_queries_ = config.has_client_config();
  if (forward_queries_) {
    const auto& client_config = config.client_config();
    dns_resolver_factory_ =
        &Network::createDnsResolverFactoryFromProto(client_config, typed_dns_resolver_config_);
    // Set additional resolving options from configuration
    resolver_timeout_ = std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(
        client_config, resolver_timeout, DEFAULT_RESOLVER_TIMEOUT.count()));
    max_pending_lookups_ = client_config.max_pending_lookups();
  } else {
    // In case client_config doesn't exist, create default DNS resolver factory and save it.
    dns_resolver_factory_ = &Network::createDefaultDnsResolverFactory(typed_dns_resolver_config_);
    max_pending_lookups_ = 0;
  }
}

void DnsFilterEnvoyConfig::addEndpointToSuffix(const absl::string_view suffix,
                                               const absl::string_view domain_name,
                                               DnsEndpointConfig& endpoint_config) {

  DnsVirtualDomainConfigSharedPtr virtual_domains = std::make_shared<DnsVirtualDomainConfig>();
  virtual_domains->emplace(std::string(domain_name), std::move(endpoint_config));

  auto success = dns_lookup_trie_.add(suffix, std::move(virtual_domains), false);
  ASSERT(success, "Unable to overwrite existing suffix in dns_filter trie");
}

bool DnsFilterEnvoyConfig::loadServerConfig(
    const envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig::ServerContextConfig&
        config,
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
                              ProtobufMessage::getNullValidationVisitor(), api_);
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

  resolver_ = std::make_unique<DnsFilterResolver>(
      resolver_callback_, config->resolverTimeout(), listener_.dispatcher(),
      config->maxPendingLookups(), config->typedDnsResolverConfig(), config->dnsResolverFactory(),
      config->api());
}

Network::FilterStatus DnsFilter::onData(Network::UdpRecvData& client_request) {
  config_->stats().downstream_rx_bytes_.recordValue(client_request.buffer_->length());
  config_->stats().downstream_rx_queries_.inc();

  // Setup counters for the parser
  DnsParserCounters parser_counters(
      config_->stats().query_buffer_underflow_, config_->stats().record_name_overflow_,
      config_->stats().query_parsing_failure_, config_->stats().queries_with_additional_rrs_,
      config_->stats().queries_with_ans_or_authority_rrs_);

  // Parse the query, if it fails return an response to the client
  DnsQueryContextPtr query_context =
      message_parser_.createQueryContext(client_request, parser_counters);
  incrementQueryTypeCount(query_context->queries_);
  if (!query_context->parse_status_) {
    config_->stats().downstream_rx_invalid_queries_.inc();
    sendDnsResponse(std::move(query_context));
    return Network::FilterStatus::StopIteration;
  }

  // Resolve the requested name and respond to the client. If the return code is
  // External, we will respond to the client when the upstream resolver returns
  if (getResponseForQuery(query_context) == DnsLookupResponseCode::External) {
    return Network::FilterStatus::StopIteration;
  }

  // We have an answer, it might be "No Answer". Send it to the client
  sendDnsResponse(std::move(query_context));

  return Network::FilterStatus::StopIteration;
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
    const bool forward_queries = config_->forwardQueries();
    if (isKnownDomain(query->name_) || !forward_queries) {
      // Determine whether the name is a cluster. Move on to the next query if successful
      if (resolveViaClusters(context, *query)) {
        continue;
      }

      // Determine whether we an answer this query with the static configuration
      if (resolveViaConfiguredHosts(context, *query)) {
        continue;
      }
    }

    // Forwarding queries is enabled if the configuration contains a client configuration
    // for the dns_filter.
    if (forward_queries) {
      ENVOY_LOG(debug, "resolving name [{}] via external resolvers", query->name_);
      resolver_->resolveExternalQuery(std::move(context), query.get());

      return DnsLookupResponseCode::External;
    }
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
  const absl::string_view suffix = Utils::getDomainSuffix(domain_name);
  auto config = config_->getDnsTrie().find(suffix);

  if (config != nullptr) {
    config_->stats().known_domain_queries_.inc();
    return true;
  }

  return false;
}

const DnsEndpointConfig* DnsFilter::getEndpointConfigForDomain(const absl::string_view domain) {
  const absl::string_view suffix = Utils::getDomainSuffix(domain);
  const auto virtual_domains = config_->getDnsTrie().find(suffix);

  if (virtual_domains == nullptr) {
    ENVOY_LOG(debug, "No domain configuration exists for [{}]", domain);
    return nullptr;
  }

  const auto iter = virtual_domains->find(domain);
  if (iter == virtual_domains->end()) {
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
    Upstream::ThreadLocalCluster* cluster = cluster_manager_.getThreadLocalCluster(target_name);
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
  Upstream::ThreadLocalCluster* cluster = cluster_manager_.getThreadLocalCluster(lookup_name);
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

Network::FilterStatus DnsFilter::onReceiveError(Api::IoError::IoErrorCode error_code) {
  config_->stats().downstream_rx_errors_.inc();
  UNREFERENCED_PARAMETER(error_code);

  return Network::FilterStatus::StopIteration;
}

} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

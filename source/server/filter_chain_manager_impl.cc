#include "server/filter_chain_manager_impl.h"

#include "envoy/config/listener/v3/listener_components.pb.h"

#include "common/common/cleanup.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/network/socket_interface_impl.h"
#include "common/protobuf/utility.h"

#include "server/configuration_impl.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Server {

namespace {

// Return a fake address for use when either the source or destination is UDS.
Network::Address::InstanceConstSharedPtr fakeAddress() {
  CONSTRUCT_ON_FIRST_USE(Network::Address::InstanceConstSharedPtr,
                         Network::Utility::parseInternetAddress("255.255.255.255"));
}

} // namespace

PerFilterChainFactoryContextImpl::PerFilterChainFactoryContextImpl(
    Configuration::FactoryContext& parent_context, Init::Manager& init_manager)
    : parent_context_(parent_context), init_manager_(init_manager) {}

bool PerFilterChainFactoryContextImpl::drainClose() const {
  return is_draining_.load() || parent_context_.drainDecision().drainClose();
}

Network::DrainDecision& PerFilterChainFactoryContextImpl::drainDecision() { return *this; }

Init::Manager& PerFilterChainFactoryContextImpl::initManager() { return init_manager_; }

ThreadLocal::SlotAllocator& PerFilterChainFactoryContextImpl::threadLocal() {
  return parent_context_.threadLocal();
}

const envoy::config::core::v3::Metadata&
PerFilterChainFactoryContextImpl::listenerMetadata() const {
  return parent_context_.listenerMetadata();
}

envoy::config::core::v3::TrafficDirection PerFilterChainFactoryContextImpl::direction() const {
  return parent_context_.direction();
}

ProtobufMessage::ValidationContext& PerFilterChainFactoryContextImpl::messageValidationContext() {
  return parent_context_.messageValidationContext();
}
ProtobufMessage::ValidationVisitor& PerFilterChainFactoryContextImpl::messageValidationVisitor() {
  return parent_context_.messageValidationVisitor();
}

AccessLog::AccessLogManager& PerFilterChainFactoryContextImpl::accessLogManager() {
  return parent_context_.accessLogManager();
}

Upstream::ClusterManager& PerFilterChainFactoryContextImpl::clusterManager() {
  return parent_context_.clusterManager();
}

Event::Dispatcher& PerFilterChainFactoryContextImpl::dispatcher() {
  return parent_context_.dispatcher();
}

Grpc::Context& PerFilterChainFactoryContextImpl::grpcContext() {
  return parent_context_.grpcContext();
}

bool PerFilterChainFactoryContextImpl::healthCheckFailed() {
  return parent_context_.healthCheckFailed();
}

Http::Context& PerFilterChainFactoryContextImpl::httpContext() {
  return parent_context_.httpContext();
}

const LocalInfo::LocalInfo& PerFilterChainFactoryContextImpl::localInfo() const {
  return parent_context_.localInfo();
}

Envoy::Runtime::RandomGenerator& PerFilterChainFactoryContextImpl::random() {
  return parent_context_.random();
}

Envoy::Runtime::Loader& PerFilterChainFactoryContextImpl::runtime() {
  return parent_context_.runtime();
}

Stats::Scope& PerFilterChainFactoryContextImpl::scope() { return parent_context_.scope(); }

Singleton::Manager& PerFilterChainFactoryContextImpl::singletonManager() {
  return parent_context_.singletonManager();
}

OverloadManager& PerFilterChainFactoryContextImpl::overloadManager() {
  return parent_context_.overloadManager();
}

Admin& PerFilterChainFactoryContextImpl::admin() { return parent_context_.admin(); }

TimeSource& PerFilterChainFactoryContextImpl::timeSource() { return api().timeSource(); }

Api::Api& PerFilterChainFactoryContextImpl::api() { return parent_context_.api(); }

ServerLifecycleNotifier& PerFilterChainFactoryContextImpl::lifecycleNotifier() {
  return parent_context_.lifecycleNotifier();
}

ProcessContextOptRef PerFilterChainFactoryContextImpl::processContext() {
  return parent_context_.processContext();
}

Configuration::ServerFactoryContext&
PerFilterChainFactoryContextImpl::getServerFactoryContext() const {
  return parent_context_.getServerFactoryContext();
}

Configuration::TransportSocketFactoryContext&
PerFilterChainFactoryContextImpl::getTransportSocketFactoryContext() const {
  return parent_context_.getTransportSocketFactoryContext();
}

Stats::Scope& PerFilterChainFactoryContextImpl::listenerScope() {
  return parent_context_.listenerScope();
}

FilterChainManagerImpl::FilterChainManagerImpl(
    const Network::Address::InstanceConstSharedPtr& address,
    Configuration::FactoryContext& factory_context, Init::Manager& init_manager,
    const FilterChainManagerImpl& parent_manager)
    : address_(address), parent_context_(factory_context), origin_(&parent_manager),
      init_manager_(init_manager) {}

bool FilterChainManagerImpl::isWildcardServerName(const std::string& name) {
  return absl::StartsWith(name, "*.");
}

void FilterChainManagerImpl::addFilterChain(
    absl::Span<const envoy::config::listener::v3::FilterChain* const> filter_chain_span,
    FilterChainFactoryBuilder& filter_chain_factory_builder,
    FilterChainFactoryContextCreator& context_creator) {
  Cleanup cleanup([this]() { origin_ = absl::nullopt; });
  std::unordered_set<envoy::config::listener::v3::FilterChainMatch, MessageUtil, MessageUtil>
      filter_chains;
  uint32_t new_filter_chain_size = 0;
  for (const auto& filter_chain : filter_chain_span) {
    const auto& filter_chain_match = filter_chain->filter_chain_match();
    if (!filter_chain_match.address_suffix().empty() || filter_chain_match.has_suffix_len()) {
      throw EnvoyException(fmt::format("error adding listener '{}': contains filter chains with "
                                       "unimplemented fields",
                                       address_->asString()));
    }
    if (filter_chains.find(filter_chain_match) != filter_chains.end()) {
      throw EnvoyException(fmt::format("error adding listener '{}': multiple filter chains with "
                                       "the same matching rules are defined",
                                       address_->asString()));
    }
    filter_chains.insert(filter_chain_match);

    // Validate IP addresses.
    std::vector<std::string> destination_ips;
    destination_ips.reserve(filter_chain_match.prefix_ranges().size());
    for (const auto& destination_ip : filter_chain_match.prefix_ranges()) {
      const auto& cidr_range = Network::Address::CidrRange::create(destination_ip);
      destination_ips.push_back(cidr_range.asString());
    }

    std::vector<std::string> source_ips;
    source_ips.reserve(filter_chain_match.source_prefix_ranges().size());
    for (const auto& source_ip : filter_chain_match.source_prefix_ranges()) {
      const auto& cidr_range = Network::Address::CidrRange::create(source_ip);
      source_ips.push_back(cidr_range.asString());
    }

    // Reject partial wildcards, we don't match on them.
    for (const auto& server_name : filter_chain_match.server_names()) {
      if (server_name.find('*') != std::string::npos &&
          !FilterChainManagerImpl::isWildcardServerName(server_name)) {
        throw EnvoyException(
            fmt::format("error adding listener '{}': partial wildcards are not supported in "
                        "\"server_names\"",
                        address_->asString()));
      }
    }

    // Reuse created filter chain if possible.
    // FilterChainManager maintains the lifetime of FilterChainFactoryContext
    // ListenerImpl maintains the dependencies of FilterChainFactoryContext
    auto filter_chain_impl = findExistingFilterChain(*filter_chain);
    if (filter_chain_impl == nullptr) {
      filter_chain_impl =
          filter_chain_factory_builder.buildFilterChain(*filter_chain, context_creator);
      ++new_filter_chain_size;
    }

    addFilterChainForDestinationPorts(
        destination_ports_map_,
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(filter_chain_match, destination_port, 0), destination_ips,
        filter_chain_match.server_names(), filter_chain_match.transport_protocol(),
        filter_chain_match.application_protocols(), filter_chain_match.source_type(), source_ips,
        filter_chain_match.source_ports(), filter_chain_impl);
    fc_contexts_[*filter_chain] = filter_chain_impl;
  }
  convertIPsToTries();
  ENVOY_LOG(debug, "new fc_contexts has {} filter chains, including {} newly built",
            fc_contexts_.size(), new_filter_chain_size);
}

void FilterChainManagerImpl::addFilterChainForDestinationPorts(
    DestinationPortsMap& destination_ports_map, uint16_t destination_port,
    const std::vector<std::string>& destination_ips,
    const absl::Span<const std::string* const> server_names, const std::string& transport_protocol,
    const absl::Span<const std::string* const> application_protocols,
    const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
    const std::vector<std::string>& source_ips,
    const absl::Span<const Protobuf::uint32> source_ports,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (destination_ports_map.find(destination_port) == destination_ports_map.end()) {
    destination_ports_map[destination_port] =
        std::make_pair<DestinationIPsMap, DestinationIPsTriePtr>(DestinationIPsMap{}, nullptr);
  }
  addFilterChainForDestinationIPs(destination_ports_map[destination_port].first, destination_ips,
                                  server_names, transport_protocol, application_protocols,
                                  source_type, source_ips, source_ports, filter_chain);
}

void FilterChainManagerImpl::addFilterChainForDestinationIPs(
    DestinationIPsMap& destination_ips_map, const std::vector<std::string>& destination_ips,
    const absl::Span<const std::string* const> server_names, const std::string& transport_protocol,
    const absl::Span<const std::string* const> application_protocols,
    const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
    const std::vector<std::string>& source_ips,
    const absl::Span<const Protobuf::uint32> source_ports,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (destination_ips.empty()) {
    addFilterChainForServerNames(destination_ips_map[EMPTY_STRING], server_names,
                                 transport_protocol, application_protocols, source_type, source_ips,
                                 source_ports, filter_chain);
  } else {
    for (const auto& destination_ip : destination_ips) {
      addFilterChainForServerNames(destination_ips_map[destination_ip], server_names,
                                   transport_protocol, application_protocols, source_type,
                                   source_ips, source_ports, filter_chain);
    }
  }
}

void FilterChainManagerImpl::addFilterChainForServerNames(
    ServerNamesMapSharedPtr& server_names_map_ptr,
    const absl::Span<const std::string* const> server_names, const std::string& transport_protocol,
    const absl::Span<const std::string* const> application_protocols,
    const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
    const std::vector<std::string>& source_ips,
    const absl::Span<const Protobuf::uint32> source_ports,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (server_names_map_ptr == nullptr) {
    server_names_map_ptr = std::make_shared<ServerNamesMap>();
  }
  auto& server_names_map = *server_names_map_ptr;

  if (server_names.empty()) {
    addFilterChainForApplicationProtocols(server_names_map[EMPTY_STRING][transport_protocol],
                                          application_protocols, source_type, source_ips,
                                          source_ports, filter_chain);
  } else {
    for (const auto& server_name_ptr : server_names) {
      if (isWildcardServerName(*server_name_ptr)) {
        // Add mapping for the wildcard domain, i.e. ".example.com" for "*.example.com".
        addFilterChainForApplicationProtocols(
            server_names_map[server_name_ptr->substr(1)][transport_protocol], application_protocols,
            source_type, source_ips, source_ports, filter_chain);
      } else {
        addFilterChainForApplicationProtocols(
            server_names_map[*server_name_ptr][transport_protocol], application_protocols,
            source_type, source_ips, source_ports, filter_chain);
      }
    }
  }
}

void FilterChainManagerImpl::addFilterChainForApplicationProtocols(
    ApplicationProtocolsMap& application_protocols_map,
    const absl::Span<const std::string* const> application_protocols,
    const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
    const std::vector<std::string>& source_ips,
    const absl::Span<const Protobuf::uint32> source_ports,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (application_protocols.empty()) {
    addFilterChainForSourceTypes(application_protocols_map[EMPTY_STRING], source_type, source_ips,
                                 source_ports, filter_chain);
  } else {
    for (const auto& application_protocol_ptr : application_protocols) {
      addFilterChainForSourceTypes(application_protocols_map[*application_protocol_ptr],
                                   source_type, source_ips, source_ports, filter_chain);
    }
  }
}

void FilterChainManagerImpl::addFilterChainForSourceTypes(
    SourceTypesArray& source_types_array,
    const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
    const std::vector<std::string>& source_ips,
    const absl::Span<const Protobuf::uint32> source_ports,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (source_ips.empty()) {
    addFilterChainForSourceIPs(source_types_array[source_type].first, EMPTY_STRING, source_ports,
                               filter_chain);
  } else {
    for (const auto& source_ip : source_ips) {
      addFilterChainForSourceIPs(source_types_array[source_type].first, source_ip, source_ports,
                                 filter_chain);
    }
  }
}

void FilterChainManagerImpl::addFilterChainForSourceIPs(
    SourceIPsMap& source_ips_map, const std::string& source_ip,
    const absl::Span<const Protobuf::uint32> source_ports,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (source_ports.empty()) {
    addFilterChainForSourcePorts(source_ips_map[source_ip], 0, filter_chain);
  } else {
    for (auto source_port : source_ports) {
      addFilterChainForSourcePorts(source_ips_map[source_ip], source_port, filter_chain);
    }
  }
}

void FilterChainManagerImpl::addFilterChainForSourcePorts(
    SourcePortsMapSharedPtr& source_ports_map_ptr, uint32_t source_port,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (source_ports_map_ptr == nullptr) {
    source_ports_map_ptr = std::make_shared<SourcePortsMap>();
  }
  auto& source_ports_map = *source_ports_map_ptr;

  if (!source_ports_map.try_emplace(source_port, filter_chain).second) {
    // If we got here and found already configured branch, then it means that this FilterChainMatch
    // is a duplicate, and that there is some overlap in the repeated fields with already processed
    // FilterChainMatches.
    throw EnvoyException(fmt::format("error adding listener '{}': multiple filter chains with "
                                     "overlapping matching rules are defined",
                                     address_->asString()));
  }
}

namespace {

// Template function for creating a CIDR list entry for either source or destination address.
template <class T>
std::pair<T, std::vector<Network::Address::CidrRange>> makeCidrListEntry(const std::string& cidr,
                                                                         const T& data) {
  std::vector<Network::Address::CidrRange> subnets;
  if (cidr == EMPTY_STRING) {
    if (Network::SocketInterfaceSingleton::get().ipFamilySupported(AF_INET)) {
      subnets.push_back(
          Network::Address::CidrRange::create(Network::Utility::getIpv4CidrCatchAllAddress()));
    }
    if (Network::SocketInterfaceSingleton::get().ipFamilySupported(AF_INET6)) {
      subnets.push_back(
          Network::Address::CidrRange::create(Network::Utility::getIpv6CidrCatchAllAddress()));
    }
  } else {
    subnets.push_back(Network::Address::CidrRange::create(cidr));
  }
  return std::make_pair<T, std::vector<Network::Address::CidrRange>>(T(data), std::move(subnets));
}

}; // namespace

const Network::FilterChain*
FilterChainManagerImpl::findFilterChain(const Network::ConnectionSocket& socket) const {
  const auto& address = socket.localAddress();

  // Match on destination port (only for IP addresses).
  if (address->type() == Network::Address::Type::Ip) {
    const auto port_match = destination_ports_map_.find(address->ip()->port());
    if (port_match != destination_ports_map_.end()) {
      return findFilterChainForDestinationIP(*port_match->second.second, socket);
    }
  }

  // Match on catch-all port 0.
  const auto port_match = destination_ports_map_.find(0);
  if (port_match != destination_ports_map_.end()) {
    return findFilterChainForDestinationIP(*port_match->second.second, socket);
  }

  return nullptr;
}

const Network::FilterChain* FilterChainManagerImpl::findFilterChainForDestinationIP(
    const DestinationIPsTrie& destination_ips_trie, const Network::ConnectionSocket& socket) const {
  auto address = socket.localAddress();
  if (address->type() != Network::Address::Type::Ip) {
    address = fakeAddress();
  }

  // Match on both: exact IP and wider CIDR ranges using LcTrie.
  const auto& data = destination_ips_trie.getData(address);
  if (!data.empty()) {
    ASSERT(data.size() == 1);
    return findFilterChainForServerName(*data.back(), socket);
  }

  return nullptr;
}

const Network::FilterChain* FilterChainManagerImpl::findFilterChainForServerName(
    const ServerNamesMap& server_names_map, const Network::ConnectionSocket& socket) const {
  const std::string server_name(socket.requestedServerName());

  // Match on exact server name, i.e. "www.example.com" for "www.example.com".
  const auto server_name_exact_match = server_names_map.find(server_name);
  if (server_name_exact_match != server_names_map.end()) {
    return findFilterChainForTransportProtocol(server_name_exact_match->second, socket);
  }

  // Match on all wildcard domains, i.e. ".example.com" and ".com" for "www.example.com".
  size_t pos = server_name.find('.', 1);
  while (pos < server_name.size() - 1 && pos != std::string::npos) {
    const std::string wildcard = server_name.substr(pos);
    const auto server_name_wildcard_match = server_names_map.find(wildcard);
    if (server_name_wildcard_match != server_names_map.end()) {
      return findFilterChainForTransportProtocol(server_name_wildcard_match->second, socket);
    }
    pos = server_name.find('.', pos + 1);
  }

  // Match on a filter chain without server name requirements.
  const auto server_name_catchall_match = server_names_map.find(EMPTY_STRING);
  if (server_name_catchall_match != server_names_map.end()) {
    return findFilterChainForTransportProtocol(server_name_catchall_match->second, socket);
  }

  return nullptr;
}

const Network::FilterChain* FilterChainManagerImpl::findFilterChainForTransportProtocol(
    const TransportProtocolsMap& transport_protocols_map,
    const Network::ConnectionSocket& socket) const {
  const std::string transport_protocol(socket.detectedTransportProtocol());

  // Match on exact transport protocol, e.g. "tls".
  const auto transport_protocol_match = transport_protocols_map.find(transport_protocol);
  if (transport_protocol_match != transport_protocols_map.end()) {
    return findFilterChainForApplicationProtocols(transport_protocol_match->second, socket);
  }

  // Match on a filter chain without transport protocol requirements.
  const auto any_protocol_match = transport_protocols_map.find(EMPTY_STRING);
  if (any_protocol_match != transport_protocols_map.end()) {
    return findFilterChainForApplicationProtocols(any_protocol_match->second, socket);
  }

  return nullptr;
}

const Network::FilterChain* FilterChainManagerImpl::findFilterChainForApplicationProtocols(
    const ApplicationProtocolsMap& application_protocols_map,
    const Network::ConnectionSocket& socket) const {
  // Match on exact application protocol, e.g. "h2" or "http/1.1".
  for (const auto& application_protocol : socket.requestedApplicationProtocols()) {
    const auto application_protocol_match = application_protocols_map.find(application_protocol);
    if (application_protocol_match != application_protocols_map.end()) {
      return findFilterChainForSourceTypes(application_protocol_match->second, socket);
    }
  }

  // Match on a filter chain without application protocol requirements.
  const auto any_protocol_match = application_protocols_map.find(EMPTY_STRING);
  if (any_protocol_match != application_protocols_map.end()) {
    return findFilterChainForSourceTypes(any_protocol_match->second, socket);
  }

  return nullptr;
}

const Network::FilterChain* FilterChainManagerImpl::findFilterChainForSourceTypes(
    const SourceTypesArray& source_types, const Network::ConnectionSocket& socket) const {

  const auto& filter_chain_local =
      source_types[envoy::config::listener::v3::FilterChainMatch::SAME_IP_OR_LOOPBACK];

  const auto& filter_chain_external =
      source_types[envoy::config::listener::v3::FilterChainMatch::EXTERNAL];

  // isSameIpOrLoopback can be expensive. Call it only if LOCAL or EXTERNAL have entries.
  const bool is_local_connection =
      (!filter_chain_local.first.empty() || !filter_chain_external.first.empty())
          ? Network::Utility::isSameIpOrLoopback(socket)
          : false;

  if (is_local_connection) {
    if (!filter_chain_local.first.empty()) {
      return findFilterChainForSourceIpAndPort(*filter_chain_local.second, socket);
    }
  } else {
    if (!filter_chain_external.first.empty()) {
      return findFilterChainForSourceIpAndPort(*filter_chain_external.second, socket);
    }
  }

  const auto& filter_chain_any = source_types[envoy::config::listener::v3::FilterChainMatch::ANY];

  if (!filter_chain_any.first.empty()) {
    return findFilterChainForSourceIpAndPort(*filter_chain_any.second, socket);
  } else {
    return nullptr;
  }
}

const Network::FilterChain* FilterChainManagerImpl::findFilterChainForSourceIpAndPort(
    const SourceIPsTrie& source_ips_trie, const Network::ConnectionSocket& socket) const {
  auto address = socket.remoteAddress();
  if (address->type() != Network::Address::Type::Ip) {
    address = fakeAddress();
  }

  // Match on both: exact IP and wider CIDR ranges using LcTrie.
  const auto& data = source_ips_trie.getData(address);
  if (data.empty()) {
    return nullptr;
  }

  ASSERT(data.size() == 1);
  const auto& source_ports_map = *data.back();
  const uint32_t source_port = address->ip()->port();
  const auto port_match = source_ports_map.find(source_port);

  // Did we get a direct hit on port.
  if (port_match != source_ports_map.end()) {
    return port_match->second.get();
  }

  // Try port 0 if we didn't already try it (UDS).
  if (source_port != 0) {
    const auto any_match = source_ports_map.find(0);
    if (any_match != source_ports_map.end()) {
      return any_match->second.get();
    }
  }

  return nullptr;
}

void FilterChainManagerImpl::convertIPsToTries() {
  for (auto& port : destination_ports_map_) {
    // These variables are used as we build up the destination CIDRs used for the trie.
    auto& destination_ips_pair = port.second;
    auto& destination_ips_map = destination_ips_pair.first;
    std::vector<std::pair<ServerNamesMapSharedPtr, std::vector<Network::Address::CidrRange>>>
        destination_ips_list;
    destination_ips_list.reserve(destination_ips_map.size());

    for (const auto& entry : destination_ips_map) {
      destination_ips_list.push_back(makeCidrListEntry(entry.first, entry.second));

      // This hugely nested for loop greatly pains me, but I'm not sure how to make it better.
      // We need to get access to all of the source IP strings so that we can convert them into
      // a trie like we did for the destination IPs above.
      for (auto& server_names_entry : *entry.second) {
        for (auto& transport_protocols_entry : server_names_entry.second) {
          for (auto& application_protocols_entry : transport_protocols_entry.second) {
            for (auto& source_array_entry : application_protocols_entry.second) {
              auto& source_ips_map = source_array_entry.first;
              std::vector<
                  std::pair<SourcePortsMapSharedPtr, std::vector<Network::Address::CidrRange>>>
                  source_ips_list;
              source_ips_list.reserve(source_ips_map.size());

              for (auto& source_ip : source_ips_map) {
                source_ips_list.push_back(makeCidrListEntry(source_ip.first, source_ip.second));
              }

              source_array_entry.second = std::make_unique<SourceIPsTrie>(source_ips_list, true);
            }
          }
        }
      }
    }

    destination_ips_pair.second = std::make_unique<DestinationIPsTrie>(destination_ips_list, true);
  }
}

Network::DrainableFilterChainSharedPtr FilterChainManagerImpl::findExistingFilterChain(
    const envoy::config::listener::v3::FilterChain& filter_chain_message) {
  // Origin filter chain manager could be empty if the current is the ancestor.
  const auto* origin = getOriginFilterChainManager();
  if (origin == nullptr) {
    return nullptr;
  }
  auto iter = origin->fc_contexts_.find(filter_chain_message);
  if (iter != origin->fc_contexts_.end()) {
    // copy the context to this filter chain manager.
    fc_contexts_.emplace(filter_chain_message, iter->second);
    return iter->second;
  }
  return nullptr;
}

Configuration::FilterChainFactoryContextPtr FilterChainManagerImpl::createFilterChainFactoryContext(
    const ::envoy::config::listener::v3::FilterChain* const filter_chain) {
  // TODO(lambdai): add stats
  UNREFERENCED_PARAMETER(filter_chain);
  return std::make_unique<PerFilterChainFactoryContextImpl>(parent_context_, init_manager_);
}

FactoryContextImpl::FactoryContextImpl(Server::Instance& server,
                                       const envoy::config::listener::v3::Listener& config,
                                       Network::DrainDecision& drain_decision,
                                       Stats::Scope& global_scope, Stats::Scope& listener_scope)
    : server_(server), config_(config), drain_decision_(drain_decision),
      global_scope_(global_scope), listener_scope_(listener_scope) {}

AccessLog::AccessLogManager& FactoryContextImpl::accessLogManager() {
  return server_.accessLogManager();
}
Upstream::ClusterManager& FactoryContextImpl::clusterManager() { return server_.clusterManager(); }
Event::Dispatcher& FactoryContextImpl::dispatcher() { return server_.dispatcher(); }
Grpc::Context& FactoryContextImpl::grpcContext() { return server_.grpcContext(); }
bool FactoryContextImpl::healthCheckFailed() { return server_.healthCheckFailed(); }
Http::Context& FactoryContextImpl::httpContext() { return server_.httpContext(); }
Init::Manager& FactoryContextImpl::initManager() { return server_.initManager(); }
const LocalInfo::LocalInfo& FactoryContextImpl::localInfo() const { return server_.localInfo(); }
Envoy::Runtime::RandomGenerator& FactoryContextImpl::random() { return server_.random(); }
Envoy::Runtime::Loader& FactoryContextImpl::runtime() { return server_.runtime(); }
Stats::Scope& FactoryContextImpl::scope() { return global_scope_; }
Singleton::Manager& FactoryContextImpl::singletonManager() { return server_.singletonManager(); }
OverloadManager& FactoryContextImpl::overloadManager() { return server_.overloadManager(); }
ThreadLocal::SlotAllocator& FactoryContextImpl::threadLocal() { return server_.threadLocal(); }
Admin& FactoryContextImpl::admin() { return server_.admin(); }
TimeSource& FactoryContextImpl::timeSource() { return server_.timeSource(); }
ProtobufMessage::ValidationContext& FactoryContextImpl::messageValidationContext() {
  return server_.messageValidationContext();
}
ProtobufMessage::ValidationVisitor& FactoryContextImpl::messageValidationVisitor() {
  return server_.messageValidationContext().staticValidationVisitor();
}
Api::Api& FactoryContextImpl::api() { return server_.api(); }
ServerLifecycleNotifier& FactoryContextImpl::lifecycleNotifier() {
  return server_.lifecycleNotifier();
}
ProcessContextOptRef FactoryContextImpl::processContext() { return server_.processContext(); }
Configuration::ServerFactoryContext& FactoryContextImpl::getServerFactoryContext() const {
  return server_.serverFactoryContext();
}
Configuration::TransportSocketFactoryContext&
FactoryContextImpl::getTransportSocketFactoryContext() const {
  return server_.transportSocketFactoryContext();
}
const envoy::config::core::v3::Metadata& FactoryContextImpl::listenerMetadata() const {
  return config_.metadata();
}
envoy::config::core::v3::TrafficDirection FactoryContextImpl::direction() const {
  return config_.traffic_direction();
}
Network::DrainDecision& FactoryContextImpl::drainDecision() { return drain_decision_; }
Stats::Scope& FactoryContextImpl::listenerScope() { return listener_scope_; }
} // namespace Server
} // namespace Envoy

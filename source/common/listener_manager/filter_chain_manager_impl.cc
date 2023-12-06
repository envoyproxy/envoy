#include "source/common/listener_manager/filter_chain_manager_impl.h"

#include "envoy/config/listener/v3/listener_components.pb.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "source/common/matcher/matcher.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/network/matching/inputs.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/server/configuration_impl.h"

#include "absl/container/node_hash_map.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Server {

namespace FilterChain {

// Return a fake address for use when either the source or destination is unix domain socket.
// This address will only match the fallback matcher of 0.0.0.0/0, which is the default
// when no IP matcher is configured.
Network::Address::InstanceConstSharedPtr fakeAddress() {
  CONSTRUCT_ON_FIRST_USE(Network::Address::InstanceConstSharedPtr,
                         Network::Utility::parseInternetAddress("255.255.255.255"));
}

struct FilterChainNameAction
    : public Matcher::ActionBase<ProtobufWkt::StringValue, Configuration::FilterChainBaseAction> {
  explicit FilterChainNameAction(const std::string& name) : name_(name) {}
  const Network::FilterChain* get(const FilterChainsByName& filter_chains_by_name,
                                  const StreamInfo::StreamInfo&) const override {
    const auto chain_match = filter_chains_by_name.find(name_);
    if (chain_match != filter_chains_by_name.end()) {
      return chain_match->second.get();
    }
    return nullptr;
  }
  const std::string name_;
};

class FilterChainNameActionFactory : public Matcher::ActionFactory<FilterChainActionFactoryContext>,
                                     Logger::Loggable<Logger::Id::config> {
public:
  std::string name() const override { return "filter-chain-name"; }
  Matcher::ActionFactoryCb createActionFactoryCb(const Protobuf::Message& config,
                                                 FilterChainActionFactoryContext&,
                                                 ProtobufMessage::ValidationVisitor&) override {
    const auto& name = dynamic_cast<const ProtobufWkt::StringValue&>(config);
    return [value = name.value()]() { return std::make_unique<FilterChainNameAction>(value); };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
};

REGISTER_FACTORY(FilterChainNameActionFactory,
                 Matcher::ActionFactory<FilterChainActionFactoryContext>);

class FilterChainNameActionValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Network::MatchingData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Network::MatchingData>&,
                                          absl::string_view) override {
    return absl::OkStatus();
  }
};

} // namespace FilterChain

PerFilterChainFactoryContextImpl::PerFilterChainFactoryContextImpl(
    Configuration::FactoryContext& parent_context, Init::Manager& init_manager)
    : parent_context_(parent_context), scope_(parent_context_.scope().createScope("")),
      filter_chain_scope_(parent_context_.listenerScope().createScope("")),
      init_manager_(init_manager) {}

bool PerFilterChainFactoryContextImpl::drainClose() const {
  return is_draining_.load() || parent_context_.drainDecision().drainClose();
}

Network::DrainDecision& PerFilterChainFactoryContextImpl::drainDecision() { return *this; }

Init::Manager& PerFilterChainFactoryContextImpl::initManager() { return init_manager_; }

ThreadLocal::SlotAllocator& PerFilterChainFactoryContextImpl::threadLocal() {
  return parent_context_.threadLocal();
}

const Network::ListenerInfo& PerFilterChainFactoryContextImpl::listenerInfo() const {
  return parent_context_.listenerInfo();
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

Event::Dispatcher& PerFilterChainFactoryContextImpl::mainThreadDispatcher() {
  return parent_context_.mainThreadDispatcher();
}

const Server::Options& PerFilterChainFactoryContextImpl::options() {
  return parent_context_.options();
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

Router::Context& PerFilterChainFactoryContextImpl::routerContext() {
  return parent_context_.routerContext();
}

const LocalInfo::LocalInfo& PerFilterChainFactoryContextImpl::localInfo() const {
  return parent_context_.localInfo();
}

Envoy::Runtime::Loader& PerFilterChainFactoryContextImpl::runtime() {
  return parent_context_.runtime();
}

Stats::Scope& PerFilterChainFactoryContextImpl::scope() { return *scope_; }

Singleton::Manager& PerFilterChainFactoryContextImpl::singletonManager() {
  return parent_context_.singletonManager();
}

OverloadManager& PerFilterChainFactoryContextImpl::overloadManager() {
  return parent_context_.overloadManager();
}

OptRef<Admin> PerFilterChainFactoryContextImpl::admin() { return parent_context_.admin(); }

TimeSource& PerFilterChainFactoryContextImpl::timeSource() { return api().timeSource(); }

Api::Api& PerFilterChainFactoryContextImpl::api() { return parent_context_.api(); }

ServerLifecycleNotifier& PerFilterChainFactoryContextImpl::lifecycleNotifier() {
  return parent_context_.lifecycleNotifier();
}

ProcessContextOptRef PerFilterChainFactoryContextImpl::processContext() {
  return parent_context_.processContext();
}

Configuration::ServerFactoryContext&
PerFilterChainFactoryContextImpl::serverFactoryContext() const {
  return parent_context_.serverFactoryContext();
}

Configuration::TransportSocketFactoryContext&
PerFilterChainFactoryContextImpl::getTransportSocketFactoryContext() const {
  return parent_context_.getTransportSocketFactoryContext();
}

Stats::Scope& PerFilterChainFactoryContextImpl::listenerScope() { return *filter_chain_scope_; }

FilterChainManagerImpl::FilterChainManagerImpl(
    const std::vector<Network::Address::InstanceConstSharedPtr>& addresses,
    Configuration::FactoryContext& factory_context, Init::Manager& init_manager,
    const FilterChainManagerImpl& parent_manager)
    : addresses_(addresses), parent_context_(factory_context), origin_(&parent_manager),
      init_manager_(init_manager) {}

bool FilterChainManagerImpl::isWildcardServerName(const std::string& name) {
  return absl::StartsWith(name, "*.");
}

void FilterChainManagerImpl::addFilterChains(
    const xds::type::matcher::v3::Matcher* filter_chain_matcher,
    absl::Span<const envoy::config::listener::v3::FilterChain* const> filter_chain_span,
    const envoy::config::listener::v3::FilterChain* default_filter_chain,
    FilterChainFactoryBuilder& filter_chain_factory_builder,
    FilterChainFactoryContextCreator& context_creator) {
  Cleanup cleanup([this]() { origin_ = absl::nullopt; });
  absl::node_hash_map<envoy::config::listener::v3::FilterChainMatch, std::string, MessageUtil,
                      MessageUtil>
      filter_chains;
  uint32_t new_filter_chain_size = 0;
  FilterChainsByName filter_chains_by_name;

  for (const auto& filter_chain : filter_chain_span) {
    const auto& filter_chain_match = filter_chain->filter_chain_match();
    if (!filter_chain_match.address_suffix().empty() || filter_chain_match.has_suffix_len()) {
      throw EnvoyException(fmt::format(
          "error adding listener '{}': filter chain '{}' contains "
          "unimplemented fields",
          absl::StrJoin(addresses_, ",", Network::AddressStrFormatter()), filter_chain->name()));
    }
    if (!filter_chain_matcher) {
      const auto& matching_iter = filter_chains.find(filter_chain_match);
      if (matching_iter != filter_chains.end()) {
        throw EnvoyException(
            fmt::format("error adding listener '{}': filter chain '{}' has "
                        "the same matching rules defined as '{}'",
                        absl::StrJoin(addresses_, ",", Network::AddressStrFormatter()),
                        filter_chain->name(), matching_iter->second));
      }
      filter_chains.insert({filter_chain_match, filter_chain->name()});
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

    // If using the matcher, require usage of "name" field and skip building the index.
    if (filter_chain_matcher) {
      if (filter_chain->name().empty()) {
        throw EnvoyException(fmt::format(
            "error adding listener '{}': \"name\" field is required when using a listener matcher",
            absl::StrJoin(addresses_, ",", Network::AddressStrFormatter())));
      }
      auto [_, inserted] =
          filter_chains_by_name.try_emplace(filter_chain->name(), filter_chain_impl);
      if (!inserted) {
        throw EnvoyException(fmt::format(
            "error adding listener '{}': \"name\" field is duplicated with value '{}'",
            absl::StrJoin(addresses_, ",", Network::AddressStrFormatter()), filter_chain->name()));
      }
      if (filter_chain->has_filter_chain_match()) {
        ENVOY_LOG(debug, "filter chain match in chain '{}' is ignored", filter_chain->name());
      }
    } else {
      auto createAddressVector = [](const auto& prefix_ranges) -> std::vector<std::string> {
        std::vector<std::string> ips;
        ips.reserve(prefix_ranges.size());
        for (const auto& ip : prefix_ranges) {
          const auto& cidr_range = Network::Address::CidrRange::create(ip);
          ips.push_back(cidr_range.asString());
        }
        return ips;
      };

      // Validate IP addresses.
      std::vector<std::string> destination_ips =
          createAddressVector(filter_chain_match.prefix_ranges());
      std::vector<std::string> source_ips =
          createAddressVector(filter_chain_match.source_prefix_ranges());
      std::vector<std::string> direct_source_ips =
          createAddressVector(filter_chain_match.direct_source_prefix_ranges());

      std::vector<std::string> server_names;
      // Reject partial wildcards, we don't match on them.
      for (const auto& server_name : filter_chain_match.server_names()) {
        if (absl::StrContains(server_name, '*') && !isWildcardServerName(server_name)) {
          throw EnvoyException(
              fmt::format("error adding listener '{}': partial wildcards are not supported in "
                          "\"server_names\"",
                          absl::StrJoin(addresses_, ",", Network::AddressStrFormatter())));
        }
        server_names.push_back(absl::AsciiStrToLower(server_name));
      }

      addFilterChainForDestinationPorts(
          destination_ports_map_,
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(filter_chain_match, destination_port, 0), destination_ips,
          server_names, filter_chain_match.transport_protocol(),
          filter_chain_match.application_protocols(), direct_source_ips,
          filter_chain_match.source_type(), source_ips, filter_chain_match.source_ports(),
          filter_chain_impl);
    }

    fc_contexts_[*filter_chain] = filter_chain_impl;
  }
  convertIPsToTries();
  copyOrRebuildDefaultFilterChain(default_filter_chain, filter_chain_factory_builder,
                                  context_creator);
  // Construct matcher if it is present in the listener configuration.
  if (filter_chain_matcher) {
    filter_chains_by_name_ = filter_chains_by_name;
    FilterChain::FilterChainNameActionValidationVisitor validation_visitor;
    Matcher::MatchTreeFactory<Network::MatchingData, FilterChainActionFactoryContext> factory(
        parent_context_.serverFactoryContext(), parent_context_.serverFactoryContext(),
        validation_visitor);
    matcher_ = factory.create(*filter_chain_matcher)();
  }
  ENVOY_LOG(debug, "new fc_contexts has {} filter chains, including {} newly built",
            fc_contexts_.size(), new_filter_chain_size);
}

void FilterChainManagerImpl::copyOrRebuildDefaultFilterChain(
    const envoy::config::listener::v3::FilterChain* default_filter_chain,
    FilterChainFactoryBuilder& filter_chain_factory_builder,
    FilterChainFactoryContextCreator& context_creator) {
  // Default filter chain is built exactly once.
  ASSERT(!default_filter_chain_message_.has_value());

  // Save the default filter chain message. This message could be used in next listener update.
  if (default_filter_chain == nullptr) {
    return;
  }
  default_filter_chain_message_ = absl::make_optional(*default_filter_chain);

  // Origin filter chain manager could be empty if the current is the ancestor.
  const auto* origin = getOriginFilterChainManager();
  if (origin == nullptr) {
    default_filter_chain_ =
        filter_chain_factory_builder.buildFilterChain(*default_filter_chain, context_creator);
    return;
  }

  // Copy from original filter chain manager, or build new filter chain if the default filter chain
  // is not equivalent to the one in the original filter chain manager.
  MessageUtil eq;
  if (origin->default_filter_chain_message_.has_value() &&
      eq(origin->default_filter_chain_message_.value(), *default_filter_chain)) {
    default_filter_chain_ = origin->default_filter_chain_;
  } else {
    default_filter_chain_ =
        filter_chain_factory_builder.buildFilterChain(*default_filter_chain, context_creator);
  }
}

void FilterChainManagerImpl::addFilterChainForDestinationPorts(
    DestinationPortsMap& destination_ports_map, uint16_t destination_port,
    const std::vector<std::string>& destination_ips,
    const absl::Span<const std::string> server_names, const std::string& transport_protocol,
    const absl::Span<const std::string* const> application_protocols,
    const std::vector<std::string>& direct_source_ips,
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
                                  direct_source_ips, source_type, source_ips, source_ports,
                                  filter_chain);
}

void FilterChainManagerImpl::addFilterChainForDestinationIPs(
    DestinationIPsMap& destination_ips_map, const std::vector<std::string>& destination_ips,
    const absl::Span<const std::string> server_names, const std::string& transport_protocol,
    const absl::Span<const std::string* const> application_protocols,
    const std::vector<std::string>& direct_source_ips,
    const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
    const std::vector<std::string>& source_ips,
    const absl::Span<const Protobuf::uint32> source_ports,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (destination_ips.empty()) {
    addFilterChainForServerNames(destination_ips_map[EMPTY_STRING], server_names,
                                 transport_protocol, application_protocols, direct_source_ips,
                                 source_type, source_ips, source_ports, filter_chain);
  } else {
    for (const auto& destination_ip : destination_ips) {
      addFilterChainForServerNames(destination_ips_map[destination_ip], server_names,
                                   transport_protocol, application_protocols, direct_source_ips,
                                   source_type, source_ips, source_ports, filter_chain);
    }
  }
}

void FilterChainManagerImpl::addFilterChainForServerNames(
    ServerNamesMapSharedPtr& server_names_map_ptr, const absl::Span<const std::string> server_names,
    const std::string& transport_protocol,
    const absl::Span<const std::string* const> application_protocols,
    const std::vector<std::string>& direct_source_ips,
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
                                          application_protocols, direct_source_ips, source_type,
                                          source_ips, source_ports, filter_chain);
  } else {
    for (const auto& server_name : server_names) {
      if (isWildcardServerName(server_name)) {
        // Add mapping for the wildcard domain, i.e. ".example.com" for "*.example.com".
        addFilterChainForApplicationProtocols(
            server_names_map[server_name.substr(1)][transport_protocol], application_protocols,
            direct_source_ips, source_type, source_ips, source_ports, filter_chain);
      } else {
        addFilterChainForApplicationProtocols(server_names_map[server_name][transport_protocol],
                                              application_protocols, direct_source_ips, source_type,
                                              source_ips, source_ports, filter_chain);
      }
    }
  }
}

void FilterChainManagerImpl::addFilterChainForApplicationProtocols(
    ApplicationProtocolsMap& application_protocols_map,
    const absl::Span<const std::string* const> application_protocols,
    const std::vector<std::string>& direct_source_ips,
    const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
    const std::vector<std::string>& source_ips,
    const absl::Span<const Protobuf::uint32> source_ports,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (application_protocols.empty()) {
    addFilterChainForDirectSourceIPs(application_protocols_map[EMPTY_STRING].first,
                                     direct_source_ips, source_type, source_ips, source_ports,
                                     filter_chain);
  } else {
    for (const auto& application_protocol_ptr : application_protocols) {
      addFilterChainForDirectSourceIPs(application_protocols_map[*application_protocol_ptr].first,
                                       direct_source_ips, source_type, source_ips, source_ports,
                                       filter_chain);
    }
  }
}

void FilterChainManagerImpl::addFilterChainForDirectSourceIPs(
    DirectSourceIPsMap& direct_source_ips_map, const std::vector<std::string>& direct_source_ips,
    const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
    const std::vector<std::string>& source_ips,
    const absl::Span<const Protobuf::uint32> source_ports,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (direct_source_ips.empty()) {
    addFilterChainForSourceTypes(direct_source_ips_map[EMPTY_STRING], source_type, source_ips,
                                 source_ports, filter_chain);
  } else {
    for (const auto& direct_source_ip : direct_source_ips) {
      addFilterChainForSourceTypes(direct_source_ips_map[direct_source_ip], source_type, source_ips,
                                   source_ports, filter_chain);
    }
  }
}

void FilterChainManagerImpl::addFilterChainForSourceTypes(
    SourceTypesArraySharedPtr& source_types_array_ptr,
    const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
    const std::vector<std::string>& source_ips,
    const absl::Span<const Protobuf::uint32> source_ports,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (source_types_array_ptr == nullptr) {
    source_types_array_ptr = std::make_shared<SourceTypesArray>();
  }

  SourceTypesArray& source_types_array = *source_types_array_ptr;
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
    throw EnvoyException(
        fmt::format("error adding listener '{}': multiple filter chains with "
                    "overlapping matching rules are defined",
                    absl::StrJoin(addresses_, ",", Network::AddressStrFormatter())));
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
FilterChainManagerImpl::findFilterChain(const Network::ConnectionSocket& socket,
                                        const StreamInfo::StreamInfo& info) const {
  if (matcher_) {
    return findFilterChainUsingMatcher(socket, info);
  }

  const auto& address = socket.connectionInfoProvider().localAddress();

  const Network::FilterChain* best_match_filter_chain = nullptr;
  // Match on destination port (only for IP addresses).
  if (address->type() == Network::Address::Type::Ip) {
    const auto port_match = destination_ports_map_.find(address->ip()->port());
    if (port_match != destination_ports_map_.end()) {
      best_match_filter_chain = findFilterChainForDestinationIP(*port_match->second.second, socket);
      if (best_match_filter_chain != nullptr) {
        return best_match_filter_chain;
      } else {
        // There is entry for specific port but none of the filter chain matches. Instead of
        // matching catch-all port 0, the fallback filter chain is returned.
        return default_filter_chain_.get();
      }
    }
  }
  // Match on catch-all port 0 if there is no specific port sub tree.
  const auto port_match = destination_ports_map_.find(0);
  if (port_match != destination_ports_map_.end()) {
    best_match_filter_chain = findFilterChainForDestinationIP(*port_match->second.second, socket);
  }
  return best_match_filter_chain != nullptr
             ? best_match_filter_chain
             // Neither exact port nor catch-all port matches. Use fallback filter chain.
             : default_filter_chain_.get();
}

const Network::FilterChain*
FilterChainManagerImpl::findFilterChainUsingMatcher(const Network::ConnectionSocket& socket,
                                                    const StreamInfo::StreamInfo& info) const {
  Network::Matching::MatchingDataImpl data(socket, info.filterState(), info.dynamicMetadata());
  const auto& match_result = Matcher::evaluateMatch<Network::MatchingData>(*matcher_, data);
  ASSERT(match_result.match_state_ == Matcher::MatchState::MatchComplete,
         "Matching must complete for network streams.");
  if (match_result.result_) {
    const auto result = match_result.result_();
    return result->getTyped<Configuration::FilterChainBaseAction>().get(filter_chains_by_name_,
                                                                        info);
  }
  return default_filter_chain_.get();
}

const Network::FilterChain* FilterChainManagerImpl::findFilterChainForDestinationIP(
    const DestinationIPsTrie& destination_ips_trie, const Network::ConnectionSocket& socket) const {
  auto address = socket.connectionInfoProvider().localAddress();
  if (address->type() != Network::Address::Type::Ip) {
    address = FilterChain::fakeAddress();
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
  ASSERT(absl::AsciiStrToLower(socket.requestedServerName()) == socket.requestedServerName());
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
      return findFilterChainForDirectSourceIP(*application_protocol_match->second.second, socket);
    }
  }

  // Match on a filter chain without application protocol requirements.
  const auto any_protocol_match = application_protocols_map.find(EMPTY_STRING);
  if (any_protocol_match != application_protocols_map.end()) {
    return findFilterChainForDirectSourceIP(*any_protocol_match->second.second, socket);
  }

  return nullptr;
}

const Network::FilterChain* FilterChainManagerImpl::findFilterChainForDirectSourceIP(
    const DirectSourceIPsTrie& direct_source_ips_trie,
    const Network::ConnectionSocket& socket) const {
  auto address = socket.connectionInfoProvider().directRemoteAddress();
  if (address->type() != Network::Address::Type::Ip) {
    address = FilterChain::fakeAddress();
  }

  const auto& data = direct_source_ips_trie.getData(address);
  if (!data.empty()) {
    ASSERT(data.size() == 1);
    return findFilterChainForSourceTypes(*data.back(), socket);
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
          ? Network::Utility::isSameIpOrLoopback(socket.connectionInfoProvider())
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
  auto address = socket.connectionInfoProvider().remoteAddress();
  if (address->type() != Network::Address::Type::Ip) {
    address = FilterChain::fakeAddress();
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
  for (auto& [destination_port, destination_ips_pair] : destination_ports_map_) {
    UNREFERENCED_PARAMETER(destination_port);
    // These variables are used as we build up the destination CIDRs used for the trie.
    auto& [destination_ips_map, destination_ips_trie] = destination_ips_pair;
    std::vector<std::pair<ServerNamesMapSharedPtr, std::vector<Network::Address::CidrRange>>>
        destination_ips_list;
    destination_ips_list.reserve(destination_ips_map.size());

    for (const auto& [destination_ip, server_names_map_ptr] : destination_ips_map) {
      destination_ips_list.push_back(makeCidrListEntry(destination_ip, server_names_map_ptr));

      // This hugely nested for loop greatly pains me, but I'm not sure how to make it better.
      // We need to get access to all of the source IP strings so that we can convert them into
      // a trie like we did for the destination IPs above.
      for (auto& [server_name, transport_protocols_map] : *server_names_map_ptr) {
        UNREFERENCED_PARAMETER(server_name);
        for (auto& [transport_protocol, application_protocols_map] : transport_protocols_map) {
          UNREFERENCED_PARAMETER(transport_protocol);
          for (auto& [application_protocol, direct_source_ips_pair] : application_protocols_map) {
            UNREFERENCED_PARAMETER(application_protocol);
            auto& [direct_source_ips_map, direct_source_ips_trie] = direct_source_ips_pair;

            std::vector<
                std::pair<SourceTypesArraySharedPtr, std::vector<Network::Address::CidrRange>>>
                direct_source_ips_list;
            direct_source_ips_list.reserve(direct_source_ips_map.size());

            for (auto& [direct_source_ip, source_arrays_ptr] : direct_source_ips_map) {
              direct_source_ips_list.push_back(
                  makeCidrListEntry(direct_source_ip, source_arrays_ptr));

              for (auto& [source_ips_map, source_ips_trie] : *source_arrays_ptr) {
                std::vector<
                    std::pair<SourcePortsMapSharedPtr, std::vector<Network::Address::CidrRange>>>
                    source_ips_list;
                source_ips_list.reserve(source_ips_map.size());

                for (auto& [source_ip, source_port_map_ptr] : source_ips_map) {
                  source_ips_list.push_back(makeCidrListEntry(source_ip, source_port_map_ptr));
                }

                source_ips_trie = std::make_unique<SourceIPsTrie>(source_ips_list, true);
              }
            }
            direct_source_ips_trie =
                std::make_unique<DirectSourceIPsTrie>(direct_source_ips_list, true);
          }
        }
      }
    }

    destination_ips_trie = std::make_unique<DestinationIPsTrie>(destination_ips_list, true);
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

} // namespace Server
} // namespace Envoy

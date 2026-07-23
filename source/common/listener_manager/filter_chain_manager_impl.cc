#include "source/common/listener_manager/filter_chain_manager_impl.h"

#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"
#include "envoy/singleton/manager.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "source/common/listener_manager/fcds_api.h"
#include "source/common/matcher/matcher.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/network/matching/inputs.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/server/configuration_impl.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/quic_server_transport_socket_factory.h"
#endif

#include "absl/container/node_hash_map.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Server {

SINGLETON_MANAGER_REGISTRATION(fcds_shared_filter_chain_manager);

namespace FilterChain {

// Return a fake address for use when either the source or destination is unix domain socket.
// This address will only match the fallback matcher of 0.0.0.0/0, which is the default
// when no IP matcher is configured.
Network::Address::InstanceConstSharedPtr fakeAddress() {
  CONSTRUCT_ON_FIRST_USE(Network::Address::InstanceConstSharedPtr,
                         Network::Utility::parseInternetAddressNoThrow("255.255.255.255"));
}

struct StaticFilterChainAction
    : public Matcher::ActionBase<Protobuf::StringValue, Configuration::FilterChainBaseAction> {
  explicit StaticFilterChainAction(const Network::FilterChain* chain) : chain_(chain) {
    ASSERT(chain != nullptr);
  }
  const Network::FilterChain* get(const FilterChainsByName&,
                                  const StreamInfo::StreamInfo&) const override {
    return chain_;
  }
  const Network::FilterChain* chain_;
};

struct DynamicFilterChainAction
    : public Matcher::ActionBase<Protobuf::StringValue, Configuration::FilterChainBaseAction> {
  explicit DynamicFilterChainAction(FcdsSubscriptionHandleSharedPtr&& fcds_handle)
      : fcds_handle_(std::move(fcds_handle)) {}
  const Network::FilterChain* get(const FilterChainsByName&,
                                  const StreamInfo::StreamInfo&) const override {
    return fcds_handle_->filterChain();
  }
  FcdsSubscriptionHandleSharedPtr fcds_handle_;
};

struct NoopFilterChainAction
    : public Matcher::ActionBase<Protobuf::StringValue, Configuration::FilterChainBaseAction> {
  const Network::FilterChain* get(const FilterChainsByName&,
                                  const StreamInfo::StreamInfo&) const override {
    return nullptr;
  }
};

class FilterChainNameActionFactory : public Matcher::ActionFactory<FilterChainActionFactoryContext>,
                                     Logger::Loggable<Logger::Id::config> {
public:
  std::string name() const override { return "filter-chain-name"; }
  Matcher::ActionConstSharedPtr createNewAction(const std::string& name,
                                                FilterChainActionFactoryContext& context) {
    if (auto it = context.filter_chains_by_name_.find(name);
        it != context.filter_chains_by_name_.end()) {
      return std::make_shared<StaticFilterChainAction>(it->second.get());
    } else if (auto& fcds_manager = context.fcds_manager_; fcds_manager != nullptr) {
      auto handle_or_error = fcds_manager->subscribe(
          context.fcds_config_source_, name, context.fcds_callbacks_, context.init_manager_);
      THROW_IF_NOT_OK(handle_or_error.status());
      return std::make_shared<DynamicFilterChainAction>(std::move(handle_or_error).value());
    }
    return std::make_shared<NoopFilterChainAction>();
  }
  Matcher::ActionConstSharedPtr createAction(const Protobuf::Message& config,
                                             FilterChainActionFactoryContext& context,
                                             ProtobufMessage::ValidationVisitor&) override {
    const std::string& name =
        Envoy::Protobuf::DynamicCastMessage<Protobuf::StringValue>(config).value();
    auto action_it = context.actions_by_name_.find(name);
    if (action_it == context.actions_by_name_.end()) {
      action_it = context.actions_by_name_.emplace(name, createNewAction(name, context)).first;
    }
    return action_it->second;
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::StringValue>();
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
      prefixed_scope_(parent_context_.prefixedScope().createScope("")),
      init_manager_(init_manager) {}

bool PerFilterChainFactoryContextImpl::drainClose(Network::DrainDirection scope) const {
  return is_draining_.load() || parent_context_.drainDecision().drainClose(scope);
}

Network::DrainDecision& PerFilterChainFactoryContextImpl::drainDecision() { return *this; }

Init::Manager& PerFilterChainFactoryContextImpl::initManager() { return init_manager_; }

ProtobufMessage::ValidationVisitor& PerFilterChainFactoryContextImpl::messageValidationVisitor() {
  return parent_context_.messageValidationVisitor();
}

Stats::Scope& PerFilterChainFactoryContextImpl::scope() { return *scope_; }
Stats::Scope& PerFilterChainFactoryContextImpl::prefixedScope() { return *prefixed_scope_; }

Configuration::ServerFactoryContext& PerFilterChainFactoryContextImpl::serverFactoryContext() {
  return parent_context_.serverFactoryContext();
}

envoy::config::core::v3::TrafficDirection PerFilterChainFactoryContextImpl::direction() const {
  return parent_context_.direction();
}
bool PerFilterChainFactoryContextImpl::isQuic() const { return parent_context_.isQuic(); }
bool PerFilterChainFactoryContextImpl::shouldBypassOverloadManager() const {
  return parent_context_.shouldBypassOverloadManager();
}

FilterChainManagerImpl::FilterChainManagerImpl(
    const std::vector<Network::Address::InstanceConstSharedPtr>& addresses,
    Configuration::FactoryContext& factory_context, Init::Manager& init_manager,
    const FilterChainManagerImpl& parent_manager)
    : addresses_(addresses), parent_context_(factory_context), origin_(&parent_manager),
      init_manager_(init_manager) {}

bool FilterChainManagerImpl::isWildcardServerName(const std::string& name) {
  return absl::StartsWith(name, "*.");
}

absl::Status FilterChainManagerImpl::addFilterChains(
    const xds::type::matcher::v3::Matcher* filter_chain_matcher,
    absl::Span<const envoy::config::listener::v3::FilterChain* const> filter_chain_span,
    const envoy::config::listener::v3::FilterChain* default_filter_chain,
    FilterChainFactoryBuilder& filter_chain_factory_builder,
    FilterChainFactoryContextCreator& context_creator,
    std::shared_ptr<FcdsSharedFilterChainManager> fcds_manager,
    const envoy::config::core::v3::ConfigSource& fcds_config_source,
    FcdsClientCallbacks& fcds_callbacks) {
  Cleanup cleanup([this]() { origin_ = std::nullopt; });
  FilterChainsByMatcher filter_chains;
  uint32_t new_filter_chain_size = 0;
  FilterChainsByName filter_chains_by_name;

  for (const auto& filter_chain : filter_chain_span) {
    RETURN_IF_NOT_OK(verifyNoDuplicateMatchers(filter_chain_matcher, filter_chains, *filter_chain));

    // Reuse created filter chain if possible.
    // FilterChainManager maintains the lifetime of FilterChainFactoryContext
    // ListenerImpl maintains the dependencies of FilterChainFactoryContext
    auto filter_chain_impl = findExistingFilterChain(*filter_chain);
    if (filter_chain_impl == nullptr) {
      auto filter_chain_or_error =
          filter_chain_factory_builder.buildFilterChain(*filter_chain, context_creator, false);
      RETURN_IF_NOT_OK(filter_chain_or_error.status());
      filter_chain_impl = filter_chain_or_error.value();
      ++new_filter_chain_size;
    }

    RETURN_IF_NOT_OK(setupFilterChainMatcher(filter_chain_matcher, filter_chains_by_name,
                                             *filter_chain, filter_chain_impl));
    fc_contexts_[*filter_chain] = filter_chain_impl;
  }
  RETURN_IF_NOT_OK(convertIPsToTries());
  RETURN_IF_NOT_OK(copyOrRebuildDefaultFilterChain(default_filter_chain,
                                                   filter_chain_factory_builder, context_creator));
  RETURN_IF_NOT_OK(maybeConstructMatcher(filter_chain_matcher, std::move(filter_chains_by_name),
                                         parent_context_, fcds_manager, fcds_config_source,
                                         fcds_callbacks));

  const auto* origin = getOriginFilterChainManager();
  if (origin != nullptr) {
    for (const auto& message_and_filter_chain : origin->fc_contexts_) {
      if (fc_contexts_.find(message_and_filter_chain.first) == fc_contexts_.end()) {
        origin->draining_filter_chains_.push_back(message_and_filter_chain.second);
      }
    }
  }

  ENVOY_LOG(debug, "new fc_contexts has {} filter chains, including {} newly built",
            fc_contexts_.size(), new_filter_chain_size);
  return absl::OkStatus();
}

absl::Status FilterChainManagerImpl::verifyNoDuplicateMatchers(
    const xds::type::matcher::v3::Matcher* filter_chain_matcher,
    absl::node_hash_map<envoy::config::listener::v3::FilterChainMatch, std::string, MessageUtil,
                        MessageUtil>& filter_chains,
    const envoy::config::listener::v3::FilterChain& filter_chain) {
  const auto& filter_chain_match = filter_chain.filter_chain_match();
  if (!filter_chain_match.address_suffix().empty() || filter_chain_match.has_suffix_len()) {
    return absl::InvalidArgumentError(fmt::format(
        "error adding listener '{}': filter chain '{}' contains "
        "unimplemented fields",
        absl::StrJoin(addresses_, ",", Network::AddressStrFormatter()), filter_chain.name()));
  }
  if (!filter_chain_matcher) {
    const auto& matching_iter = filter_chains.find(filter_chain_match);
    if (matching_iter != filter_chains.end()) {
      std::string error_msg =
          fmt::format("error adding listener '{}': filter chain '{}' has "
                      "the same matching rules defined as '{}'",
                      absl::StrJoin(addresses_, ",", Network::AddressStrFormatter()),
                      filter_chain.name(), matching_iter->second);
#ifdef ENVOY_ENABLE_YAML
      absl::StrAppend(&error_msg, ". duplicate matcher is: ",
                      MessageUtil::getJsonStringFromMessageOrError(filter_chain_match, false));
#endif

      return absl::InvalidArgumentError(error_msg);
    }

    filter_chains.insert({filter_chain_match, filter_chain.name()});
  }

  return absl::OkStatus();
}

absl::Status FilterChainManagerImpl::setupFilterChainMatcher(
    const xds::type::matcher::v3::Matcher* filter_chain_matcher,
    FilterChainsByName& filter_chains_by_name,
    const envoy::config::listener::v3::FilterChain& filter_chain,
    const Network::DrainableFilterChainSharedPtr& filter_chain_impl) {
  const auto& filter_chain_match = filter_chain.filter_chain_match();

  // If using the matcher, require usage of "name" field and skip building the index.
  if (filter_chain_matcher) {
    if (filter_chain.name().empty()) {
      return absl::InvalidArgumentError(fmt::format(
          "error adding listener '{}': \"name\" field is required when using a listener matcher",
          absl::StrJoin(addresses_, ",", Network::AddressStrFormatter())));
    }
    auto [_, inserted] = filter_chains_by_name.try_emplace(filter_chain.name(), filter_chain_impl);
    if (!inserted) {
      return absl::InvalidArgumentError(fmt::format(
          "error adding listener '{}': \"name\" field is duplicated with value '{}'",
          absl::StrJoin(addresses_, ",", Network::AddressStrFormatter()), filter_chain.name()));
    }
    if (filter_chain.has_filter_chain_match()) {
      ENVOY_LOG(debug, "filter chain match in chain '{}' is ignored", filter_chain.name());
    }
  } else {
    auto createAddressVector = [](const auto& prefix_ranges,
                                  absl::Status& creation_status) -> std::vector<std::string> {
      std::vector<std::string> ips;
      ips.reserve(prefix_ranges.size());
      for (const auto& ip : prefix_ranges) {
        auto cidr_range_or_error = Network::Address::CidrRange::create(ip);
        if (cidr_range_or_error.status().ok()) {
          ips.push_back(cidr_range_or_error->asString());
        } else {
          creation_status = cidr_range_or_error.status();
        }
      }
      return ips;
    };

    absl::Status creation_status = absl::OkStatus();
    // Validate IP addresses.
    std::vector<std::string> destination_ips =
        createAddressVector(filter_chain_match.prefix_ranges(), creation_status);
    RETURN_IF_NOT_OK(creation_status);
    std::vector<std::string> source_ips =
        createAddressVector(filter_chain_match.source_prefix_ranges(), creation_status);
    RETURN_IF_NOT_OK(creation_status);
    std::vector<std::string> direct_source_ips =
        createAddressVector(filter_chain_match.direct_source_prefix_ranges(), creation_status);
    RETURN_IF_NOT_OK(creation_status);

    std::vector<std::string> server_names;
    // Reject partial wildcards, we don't match on them.
    for (const auto& server_name : filter_chain_match.server_names()) {
      if (absl::StrContains(server_name, '*') && !isWildcardServerName(server_name)) {
        return absl::InvalidArgumentError(
            fmt::format("error adding listener '{}': partial wildcards are not supported in "
                        "\"server_names\"",
                        absl::StrJoin(addresses_, ",", Network::AddressStrFormatter())));
      }
      server_names.push_back(absl::AsciiStrToLower(server_name));
    }

    RETURN_IF_NOT_OK(addFilterChainForDestinationPorts(
        destination_ports_map_,
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(filter_chain_match, destination_port, 0), destination_ips,
        server_names, filter_chain_match.transport_protocol(),
        filter_chain_match.application_protocols(), direct_source_ips,
        filter_chain_match.source_type(), source_ips, filter_chain_match.source_ports(),
        filter_chain_impl));
  }

  return absl::OkStatus();
}

absl::Status FilterChainManagerImpl::maybeConstructMatcher(
    const xds::type::matcher::v3::Matcher* filter_chain_matcher,
    FilterChainsByName&& filter_chains_by_name, Configuration::FactoryContext& parent_context,
    std::shared_ptr<FcdsSharedFilterChainManager> fcds_manager,
    const envoy::config::core::v3::ConfigSource& fcds_config_source,
    FcdsClientCallbacks& fcds_callbacks) {
  if (fcds_manager) {
    if (!filter_chain_matcher) {
      return absl::InvalidArgumentError("FCDS requires a filter chain matcher.");
    }
    if (parent_context.isQuic()) {
      return absl::InvalidArgumentError("FCDS does not support QUIC filter chains");
    }
  }
  // Construct matcher if it is present in the listener configuration.
  // Discover FCDS filter chain names by filtering inlined names from the actions.
  if (filter_chain_matcher) {
    filter_chains_by_name_ = std::move(filter_chains_by_name);
    FilterChain::FilterChainNameActionValidationVisitor validation_visitor;
    FilterChainActionFactoryContext action_factory_context{
        .server_ = parent_context.serverFactoryContext(),
        .filter_chains_by_name_ = filter_chains_by_name_,
        // Below fields are used for FCDS subscription construction.
        .fcds_manager_ = fcds_manager,
        .fcds_callbacks_ = fcds_callbacks,
        .fcds_config_source_ = fcds_config_source,
        .init_manager_ = init_manager_,
    };
    // MatchTreeFactory::create doesn't have an exception-free variant.
    TRY_NEEDS_AUDIT {
      Matcher::MatchTreeFactory<Network::MatchingData, FilterChainActionFactoryContext> factory(
          action_factory_context, parent_context.serverFactoryContext(), validation_visitor);
      matcher_ = factory.create(*filter_chain_matcher)();
    }
    END_TRY CATCH(const EnvoyException& e, {
      return absl::InvalidArgumentError(
          absl::StrCat("cannot create a filter chain matcher: ", e.what()));
    });
  }
  return absl::OkStatus();
}

absl::Status FilterChainManagerImpl::copyOrRebuildDefaultFilterChain(
    const envoy::config::listener::v3::FilterChain* default_filter_chain,
    FilterChainFactoryBuilder& filter_chain_factory_builder,
    FilterChainFactoryContextCreator& context_creator) {
  // Default filter chain is built exactly once.
  ASSERT(!default_filter_chain_message_.has_value());

  // Save the default filter chain message. This message could be used in next listener update.
  if (default_filter_chain == nullptr) {
    return absl::OkStatus();
  }
  default_filter_chain_message_ = std::make_optional(*default_filter_chain);

  // Origin filter chain manager could be empty if the current is the ancestor.
  const auto* origin = getOriginFilterChainManager();
  if (origin == nullptr) {
    auto filter_chain_or_error = filter_chain_factory_builder.buildFilterChain(
        *default_filter_chain, context_creator, false);
    RETURN_IF_NOT_OK(filter_chain_or_error.status());
    default_filter_chain_ = *filter_chain_or_error;
    return absl::OkStatus();
  }

  // Copy from original filter chain manager, or build new filter chain if the default filter chain
  // is not equivalent to the one in the original filter chain manager.
  MessageUtil eq;
  if (origin->default_filter_chain_message_.has_value() &&
      eq(origin->default_filter_chain_message_.value(), *default_filter_chain)) {
    default_filter_chain_ = origin->default_filter_chain_;
  } else {
    auto filter_chain_or_error = filter_chain_factory_builder.buildFilterChain(
        *default_filter_chain, context_creator, false);
    RETURN_IF_NOT_OK(filter_chain_or_error.status());
    default_filter_chain_ = *filter_chain_or_error;
  }
  return absl::OkStatus();
}

absl::Status FilterChainManagerImpl::addFilterChainForDestinationPorts(
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
  return addFilterChainForDestinationIPs(destination_ports_map[destination_port].first,
                                         destination_ips, server_names, transport_protocol,
                                         application_protocols, direct_source_ips, source_type,
                                         source_ips, source_ports, filter_chain);
}

absl::Status FilterChainManagerImpl::addFilterChainForDestinationIPs(
    DestinationIPsMap& destination_ips_map, const std::vector<std::string>& destination_ips,
    const absl::Span<const std::string> server_names, const std::string& transport_protocol,
    const absl::Span<const std::string* const> application_protocols,
    const std::vector<std::string>& direct_source_ips,
    const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
    const std::vector<std::string>& source_ips,
    const absl::Span<const Protobuf::uint32> source_ports,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (destination_ips.empty()) {
    RETURN_IF_NOT_OK(addFilterChainForServerNames(
        destination_ips_map[EMPTY_STRING], server_names, transport_protocol, application_protocols,
        direct_source_ips, source_type, source_ips, source_ports, filter_chain));
  } else {
    for (const auto& destination_ip : destination_ips) {
      RETURN_IF_NOT_OK(
          addFilterChainForServerNames(destination_ips_map[destination_ip], server_names,
                                       transport_protocol, application_protocols, direct_source_ips,
                                       source_type, source_ips, source_ports, filter_chain));
    }
  }
  return absl::OkStatus();
}

absl::Status FilterChainManagerImpl::addFilterChainForServerNames(
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
    RETURN_IF_NOT_OK(addFilterChainForApplicationProtocols(
        server_names_map[EMPTY_STRING][transport_protocol], application_protocols,
        direct_source_ips, source_type, source_ips, source_ports, filter_chain));
  } else {
    for (const auto& server_name : server_names) {
      if (isWildcardServerName(server_name)) {
        // Add mapping for the wildcard domain, i.e. ".example.com" for "*.example.com".
        RETURN_IF_NOT_OK(addFilterChainForApplicationProtocols(
            server_names_map[server_name.substr(1)][transport_protocol], application_protocols,
            direct_source_ips, source_type, source_ips, source_ports, filter_chain));
      } else {
        RETURN_IF_NOT_OK(addFilterChainForApplicationProtocols(
            server_names_map[server_name][transport_protocol], application_protocols,
            direct_source_ips, source_type, source_ips, source_ports, filter_chain));
      }
    }
  }
  return absl::OkStatus();
}

absl::Status FilterChainManagerImpl::addFilterChainForApplicationProtocols(
    ApplicationProtocolsMap& application_protocols_map,
    const absl::Span<const std::string* const> application_protocols,
    const std::vector<std::string>& direct_source_ips,
    const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
    const std::vector<std::string>& source_ips,
    const absl::Span<const Protobuf::uint32> source_ports,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (application_protocols.empty()) {
    RETURN_IF_NOT_OK(addFilterChainForDirectSourceIPs(application_protocols_map[EMPTY_STRING].first,
                                                      direct_source_ips, source_type, source_ips,
                                                      source_ports, filter_chain));
  } else {
    for (const auto& application_protocol_ptr : application_protocols) {
      RETURN_IF_NOT_OK(addFilterChainForDirectSourceIPs(
          application_protocols_map[*application_protocol_ptr].first, direct_source_ips,
          source_type, source_ips, source_ports, filter_chain));
    }
  }
  return absl::OkStatus();
}

absl::Status FilterChainManagerImpl::addFilterChainForDirectSourceIPs(
    DirectSourceIPsMap& direct_source_ips_map, const std::vector<std::string>& direct_source_ips,
    const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
    const std::vector<std::string>& source_ips,
    const absl::Span<const Protobuf::uint32> source_ports,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (direct_source_ips.empty()) {
    RETURN_IF_NOT_OK(addFilterChainForSourceTypes(direct_source_ips_map[EMPTY_STRING], source_type,
                                                  source_ips, source_ports, filter_chain));
  } else {
    for (const auto& direct_source_ip : direct_source_ips) {
      RETURN_IF_NOT_OK(addFilterChainForSourceTypes(direct_source_ips_map[direct_source_ip],
                                                    source_type, source_ips, source_ports,
                                                    filter_chain));
    }
  }
  return absl::OkStatus();
}

absl::Status FilterChainManagerImpl::addFilterChainForSourceTypes(
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
    RETURN_IF_NOT_OK(addFilterChainForSourceIPs(source_types_array[source_type].first, EMPTY_STRING,
                                                source_ports, filter_chain));
  } else {
    for (const auto& source_ip : source_ips) {
      RETURN_IF_NOT_OK(addFilterChainForSourceIPs(source_types_array[source_type].first, source_ip,
                                                  source_ports, filter_chain));
    }
  }
  return absl::OkStatus();
}

absl::Status FilterChainManagerImpl::addFilterChainForSourceIPs(
    SourceIPsMap& source_ips_map, const std::string& source_ip,
    const absl::Span<const Protobuf::uint32> source_ports,
    const Network::FilterChainSharedPtr& filter_chain) {
  if (source_ports.empty()) {
    RETURN_IF_NOT_OK(addFilterChainForSourcePorts(source_ips_map[source_ip], 0, filter_chain));
  } else {
    for (auto source_port : source_ports) {
      RETURN_IF_NOT_OK(
          addFilterChainForSourcePorts(source_ips_map[source_ip], source_port, filter_chain));
    }
  }
  return absl::OkStatus();
}

absl::Status FilterChainManagerImpl::addFilterChainForSourcePorts(
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
    return absl::InvalidArgumentError(
        fmt::format("error adding listener '{}': multiple filter chains with "
                    "overlapping matching rules are defined",
                    absl::StrJoin(addresses_, ",", Network::AddressStrFormatter())));
  }
  return absl::OkStatus();
}

namespace {

// Template function for creating a CIDR list entry for either source or destination address.
template <class T>
std::pair<T, std::vector<Network::Address::CidrRange>>
makeCidrListEntry(const std::string& cidr, const T& data, absl::Status& creation_status) {
  std::vector<Network::Address::CidrRange> subnets;
  absl::StatusOr<Network::Address::CidrRange> range;
  if (cidr == EMPTY_STRING) {
    if (Network::SocketInterfaceSingleton::get().ipFamilySupported(AF_INET)) {
      range = Network::Address::CidrRange::create(Network::Utility::getIpv4CidrCatchAllAddress());
      creation_status = range.status();
      if (range.status().ok()) {
        subnets.push_back(*range);
      }
    }
    if (Network::SocketInterfaceSingleton::get().ipFamilySupported(AF_INET6)) {
      range = Network::Address::CidrRange::create(Network::Utility::getIpv6CidrCatchAllAddress());
      creation_status = range.status();
      if (range.status().ok()) {
        subnets.push_back(*range);
      }
    }
  } else {
    range = Network::Address::CidrRange::create(cidr);
    creation_status = range.status();
    if (range.status().ok()) {
      subnets.push_back(*range);
    }
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
  const Matcher::ActionMatchResult match_result =
      Matcher::evaluateMatch<Network::MatchingData>(*matcher_, data);
  ASSERT(match_result.isComplete(), "Matching must complete for network streams.");
  if (match_result.isMatch()) {
    return match_result.action()->getTyped<Configuration::FilterChainBaseAction>().get(
        filter_chains_by_name_, info);
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

absl::Status FilterChainManagerImpl::convertIPsToTries() {
  for (auto& [destination_port, destination_ips_pair] : destination_ports_map_) {
    UNREFERENCED_PARAMETER(destination_port);
    // These variables are used as we build up the destination CIDRs used for the trie.
    auto& [destination_ips_map, destination_ips_trie] = destination_ips_pair;
    std::vector<std::pair<ServerNamesMapSharedPtr, std::vector<Network::Address::CidrRange>>>
        destination_ips_list;
    destination_ips_list.reserve(destination_ips_map.size());

    for (const auto& [destination_ip, server_names_map_ptr] : destination_ips_map) {
      absl::Status creation_status = absl::OkStatus();
      destination_ips_list.push_back(
          makeCidrListEntry(destination_ip, server_names_map_ptr, creation_status));
      RETURN_IF_NOT_OK(creation_status);

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
                  makeCidrListEntry(direct_source_ip, source_arrays_ptr, creation_status));
              RETURN_IF_NOT_OK(creation_status);

              for (auto& [source_ips_map, source_ips_trie] : *source_arrays_ptr) {
                std::vector<
                    std::pair<SourcePortsMapSharedPtr, std::vector<Network::Address::CidrRange>>>
                    source_ips_list;
                source_ips_list.reserve(source_ips_map.size());

                for (auto& [source_ip, source_port_map_ptr] : source_ips_map) {
                  source_ips_list.push_back(
                      makeCidrListEntry(source_ip, source_port_map_ptr, creation_status));
                  RETURN_IF_NOT_OK(creation_status);
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
  return absl::OkStatus();
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

ListenerFilterChainFactoryBuilder::ListenerFilterChainFactoryBuilder(
    bool is_quic, ProtobufMessage::ValidationVisitor& validator,
    ListenerComponentFactory& listener_component_factory,
    Server::Configuration::TransportSocketFactoryContext& factory_context)
    : is_quic_(is_quic), validator_(validator),
      listener_component_factory_(listener_component_factory), factory_context_(factory_context) {}

absl::StatusOr<Network::DrainableFilterChainSharedPtr>
ListenerFilterChainFactoryBuilder::buildFilterChain(
    const envoy::config::listener::v3::FilterChain& filter_chain,
    FilterChainFactoryContextCreator& context_creator, bool added_via_api) const {
  return buildFilterChainInternal(
      filter_chain, context_creator.createFilterChainFactoryContext(&filter_chain), added_via_api);
}

absl::StatusOr<Network::DrainableFilterChainSharedPtr>
ListenerFilterChainFactoryBuilder::buildFilterChainInternal(
    const envoy::config::listener::v3::FilterChain& filter_chain,
    Configuration::FilterChainFactoryContextPtr&& filter_chain_factory_context,
    bool added_via_api) const {
  // If the cluster doesn't have transport socket configured, then use the default "raw_buffer"
  // transport socket or BoringSSL-based "tls" transport socket if TLS settings are configured.
  // We copy by value first then override if necessary.
  auto transport_socket = filter_chain.transport_socket();
  if (!filter_chain.has_transport_socket()) {
    envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer raw_buffer;
    std::ignore = transport_socket.mutable_typed_config()->PackFrom(raw_buffer);
    transport_socket.set_name("envoy.transport_sockets.raw_buffer");
  }

  auto& config_factory = Config::Utility::getAndCheckFactory<
      Server::Configuration::DownstreamTransportSocketConfigFactory>(transport_socket);
#if defined(ENVOY_ENABLE_QUIC)
  if (is_quic_ &&
      dynamic_cast<Quic::QuicServerTransportSocketConfigFactory*>(&config_factory) == nullptr) {
    return absl::InvalidArgumentError(
        fmt::format("error building filter chain for quic listener: wrong "
                    "transport socket config specified for quic transport socket: "
                    "{}. \nUse QuicDownstreamTransport instead.",
                    transport_socket.DebugString()));
  }
  const std::string hcm_str =
      "type.googleapis.com/"
      "envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager";
  if (is_quic_ &&
      (filter_chain.filters().empty() ||
       filter_chain.filters(filter_chain.filters().size() - 1).typed_config().type_url() !=
           hcm_str)) {
    return absl::InvalidArgumentError(
        fmt::format("error building network filter chain for quic listener: requires "
                    "http_connection_manager filter to be last in the chain."));
  }
#else
  // When QUIC is compiled out it should not be possible to configure either the QUIC transport
  // socket or the QUIC listener and get to this point.
  ASSERT(!is_quic_);
#endif
  ProtobufTypes::MessagePtr message =
      Config::Utility::translateToFactoryConfig(transport_socket, validator_, config_factory);

  std::vector<std::string> server_names(filter_chain.filter_chain_match().server_names().begin(),
                                        filter_chain.filter_chain_match().server_names().end());

  auto factory_or_error = config_factory.createTransportSocketFactory(*message, factory_context_,
                                                                      std::move(server_names));
  RETURN_IF_NOT_OK(factory_or_error.status());
  auto factory_list_or_error = listener_component_factory_.createNetworkFilterFactoryList(
      filter_chain.filters(), *filter_chain_factory_context);
  RETURN_IF_NOT_OK(factory_list_or_error.status());

  auto filter_chain_res = std::make_shared<FilterChainImpl>(
      std::move(factory_or_error.value()), std::move(*factory_list_or_error),
      std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(filter_chain, transport_socket_connect_timeout, 0)),
      added_via_api, filter_chain);

  filter_chain_res->setFilterChainFactoryContext(std::move(filter_chain_factory_context));
  return filter_chain_res;
}

const Network::FilterChain*
FcdsSharedFilterChainManager::findThreadLocalFilterChain(const std::string& name) const {
  auto state = tls_slot_->get();
  if (state.has_value()) {
    auto iter = state->filter_chains_.find(name);
    if (iter != state->filter_chains_.end()) {
      return iter->second.get();
    }
  }
  return nullptr;
}

FcdsContextCreator::FcdsContextCreator(
    Server::Configuration::ServerFactoryContext& server_context,
    const ::envoy::config::listener::v3::FilterChain* const filter_chain)
    : context_(std::make_unique<FcdsFilterChainFactoryContextImpl>(server_context, *filter_chain)),
      saved_context_(context_.get()) {}

Configuration::FilterChainFactoryContextPtr FcdsContextCreator::createFilterChainFactoryContext(
    const ::envoy::config::listener::v3::FilterChain* const) {
  return std::move(context_);
}

void FcdsContextCreator::initialize(std::function<void()> completion) {
  saved_context_->initialize(completion);
}

FcdsSharedFilterChainManager::FcdsSharedFilterChainManager(
    Server::Configuration::ServerFactoryContext& server_context,
    ListenerComponentFactory& listener_component_factory)
    : server_context_(server_context), listener_component_factory_(listener_component_factory),
      tls_slot_(ThreadLocal::TypedSlot<ThreadLocalState>::makeUnique(server_context.threadLocal())),
      transport_factory_context_(
          std::make_unique<Server::Configuration::TransportSocketFactoryContextImpl>(
              server_context,
              server_context.messageValidationContext().dynamicValidationVisitor())),
      scope_(server_context_.scope().createScope("filter_chain_manager.")) {
  tls_slot_->set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalState>(); });
}

class FcdsSubscriptionHandleImpl : public FcdsSubscriptionHandle {
public:
  FcdsSubscriptionHandleImpl(std::shared_ptr<FcdsSharedFilterChainManager> shared_manager,
                             const std::string& filter_chain_name, FcdsClientCallbacks& callbacks)
      : shared_manager_(std::move(shared_manager)), filter_chain_name_(filter_chain_name),
        callbacks_(callbacks) {}

  const Network::FilterChain* filterChain() override {
    return shared_manager_->findThreadLocalFilterChain(filter_chain_name_);
  }

  ~FcdsSubscriptionHandleImpl() override {
    shared_manager_->unsubscribe(filter_chain_name_, *this);
  }

  FcdsClientCallbacks& callbacks() override { return callbacks_; }

private:
  const std::shared_ptr<FcdsSharedFilterChainManager> shared_manager_;
  const std::string filter_chain_name_;
  FcdsClientCallbacks& callbacks_;
};

absl::StatusOr<FcdsSubscriptionHandleSharedPtr>
FcdsSharedFilterChainManager::subscribe(const envoy::config::core::v3::ConfigSource& config_source,
                                        const std::string& filter_chain_name,
                                        FcdsClientCallbacks& callbacks,
                                        Init::Manager& init_manager) {
  auto iter = subscriptions_.find(filter_chain_name);
  // Assume config_source is the same for the same filter chain name. This would be the case for
  // xdstp names.
  if (iter == subscriptions_.end()) {
    absl::Status creation_status;
    auto api = std::make_unique<FcdsApiImpl>(
        config_source, filter_chain_name, *this, server_context_.clusterManager(), *scope_,
        server_context_.messageValidationContext().dynamicValidationVisitor(), creation_status);
    RETURN_IF_NOT_OK(creation_status);
    auto state = std::make_unique<SubscriptionState>();
    state->api_ = std::move(api);
    iter = subscriptions_.emplace(filter_chain_name, std::move(state)).first;
  }
  SubscriptionState& state = *iter->second;
  init_manager.add(state.api_->initTarget());
  auto result = std::make_shared<FcdsSubscriptionHandleImpl>(shared_from_this(), filter_chain_name,
                                                             callbacks);
  state.handles_.insert(result.get());
  return result;
}

void FcdsSharedFilterChainManager::unsubscribe(const std::string& filter_chain_name,
                                               FcdsSubscriptionHandle& handle) {
  auto iter = subscriptions_.find(filter_chain_name);
  if (iter != subscriptions_.end()) {
    SubscriptionState& state = *iter->second;
    state.handles_.erase(&handle);
    if (state.handles_.empty()) {
      subscriptions_.erase(iter);
    }
  }
}

absl::Status FcdsSharedFilterChainManager::onFilterChainUpdated(const FilterChainProto& proto) {
  auto state_iter = subscriptions_.find(proto.name());
  if (state_iter == subscriptions_.end()) {
    return absl::NotFoundError(
        fmt::format("no subscription found for filter chain {}", proto.name()));
  }
  SubscriptionState& state = *state_iter->second;
  ListenerFilterChainFactoryBuilder builder(false, server_context_.messageValidationVisitor(),
                                            listener_component_factory_,
                                            *transport_factory_context_);
  FcdsContextCreator context(server_context_, &proto);
  auto filter_chain_or_error = builder.buildFilterChain(proto, context, true);
  RETURN_IF_NOT_OK(filter_chain_or_error.status());
  Network::DrainableFilterChainSharedPtr filter_chain = std::move(filter_chain_or_error).value();
  state.warming_filter_chain_ = filter_chain;

  context.initialize(
      [weak_that = std::weak_ptr<FcdsSharedFilterChainManager>(shared_from_this()),
       weak_filter_chain = std::weak_ptr<Network::DrainableFilterChain>(filter_chain)]() {
        auto that = weak_that.lock();
        auto filter_chain = weak_filter_chain.lock();
        if (that && filter_chain) {
          that->onFilterChainWarmed(filter_chain);
        }
      });
  return absl::OkStatus();
}

void FcdsSharedFilterChainManager::onFilterChainWarmed(
    Network::DrainableFilterChainSharedPtr filter_chain) {
  auto iter = subscriptions_.find(filter_chain->name());
  if (iter == subscriptions_.end()) {
    return;
  }
  SubscriptionState& state = *iter->second;
  if (state.warming_filter_chain_ != filter_chain) {
    return;
  }

  Network::DrainableFilterChainSharedPtr draining = state.api_->filterChain();
  ENVOY_LOG(debug, "FCDS: updating warmed shared filter chain name={}", filter_chain->name());

  state.api_->setFilterChain(std::move(filter_chain));
  updateTlsState();

  if (draining) {
    for (auto* handle : state.handles_) {
      handle->callbacks().drainFilterChain(draining);
    }
  }
  state.warming_filter_chain_ = nullptr;
}

void FcdsSharedFilterChainManager::onFilterChainRemoved(
    Network::DrainableFilterChainSharedPtr&& draining) {
  auto state_iter = subscriptions_.find(draining->name());
  if (state_iter == subscriptions_.end()) {
    return;
  }
  SubscriptionState& state = *state_iter->second;
  ENVOY_LOG(debug, "FCDS: removing shared filter chain name={}", draining->name());

  updateTlsState();

  for (auto* handle : state.handles_) {
    handle->callbacks().drainFilterChain(draining);
  }
}

void FcdsSharedFilterChainManager::updateTlsState() {
  auto filter_chains = std::make_shared<ThreadLocalState>();
  for (const auto& name_and_state : subscriptions_) {
    auto active_chain = name_and_state.second->api_->filterChain();
    if (active_chain != nullptr) {
      filter_chains->filter_chains_[name_and_state.first] = active_chain;
    }
  }
  tls_slot_->set([filter_chains](Event::Dispatcher&) { return filter_chains; });
}

} // namespace Server
} // namespace Envoy

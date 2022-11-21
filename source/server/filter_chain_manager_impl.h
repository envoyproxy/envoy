#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/matcher/matcher.h"
#include "envoy/network/drain_decision.h"
#include "envoy/network/filter.h"
#include "envoy/server/filter_config.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/common/init/manager_impl.h"
#include "source/common/network/cidr_range.h"
#include "source/common/network/lc_trie.h"
#include "source/server/filter_chain_factory_context_callback.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Server {

class FilterChainFactoryBuilder {
public:
  virtual ~FilterChainFactoryBuilder() = default;
  /**
   * @return Shared filter chain where builder is allowed to determine and reuse duplicated filter
   * chain. Throw exception if failed.
   */
  virtual Network::DrainableFilterChainSharedPtr
  buildFilterChain(const envoy::config::listener::v3::FilterChain& filter_chain,
                   FilterChainFactoryContextCreator& context_creator) const PURE;
};

// PerFilterChainFactoryContextImpl is supposed to be used by network filter chain.
// Its lifetime must cover the created network filter chain.
// Its lifetime should be covered by the owned listeners so as to support replacing the active
// filter chains in the listener.
class PerFilterChainFactoryContextImpl : public Configuration::FilterChainFactoryContext,
                                         public Network::DrainDecision {
public:
  explicit PerFilterChainFactoryContextImpl(Configuration::FactoryContext& parent_context,
                                            Init::Manager& init_manager);

  // DrainDecision
  bool drainClose() const override;
  Common::CallbackHandlePtr addOnDrainCloseCb(DrainCloseCb) const override {
    IS_ENVOY_BUG("Unexpected function call");
    return nullptr;
  }

  // Configuration::FactoryContext
  AccessLog::AccessLogManager& accessLogManager() override;
  Upstream::ClusterManager& clusterManager() override;
  Event::Dispatcher& mainThreadDispatcher() override;
  const Server::Options& options() override;
  Network::DrainDecision& drainDecision() override;
  Grpc::Context& grpcContext() override;
  Router::Context& routerContext() override;
  bool healthCheckFailed() override;
  Http::Context& httpContext() override;
  Init::Manager& initManager() override;
  const LocalInfo::LocalInfo& localInfo() const override;
  Envoy::Runtime::Loader& runtime() override;
  Stats::Scope& scope() override;
  Stats::Scope& serverScope() override { return parent_context_.serverScope(); }
  Singleton::Manager& singletonManager() override;
  OverloadManager& overloadManager() override;
  ThreadLocal::SlotAllocator& threadLocal() override;
  OptRef<Admin> admin() override;
  const envoy::config::core::v3::Metadata& listenerMetadata() const override;
  const Envoy::Config::TypedMetadata& listenerTypedMetadata() const override;
  envoy::config::core::v3::TrafficDirection direction() const override;
  TimeSource& timeSource() override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override;
  ProtobufMessage::ValidationContext& messageValidationContext() override;
  Api::Api& api() override;
  ServerLifecycleNotifier& lifecycleNotifier() override;
  ProcessContextOptRef processContext() override;
  Configuration::ServerFactoryContext& getServerFactoryContext() const override;
  Configuration::TransportSocketFactoryContext& getTransportSocketFactoryContext() const override;
  Stats::Scope& listenerScope() override;
  bool isQuicListener() const override;

  void startDraining() override { is_draining_.store(true); }

private:
  Configuration::FactoryContext& parent_context_;
  // The scope that has empty prefix.
  Stats::ScopeSharedPtr scope_;
  // filter_chain_scope_ has the same prefix as listener owners scope.
  Stats::ScopeSharedPtr filter_chain_scope_;
  Init::Manager& init_manager_;
  std::atomic<bool> is_draining_{false};
};

using FilterChainActionFactoryContext = Configuration::ServerFactoryContext;
using FilterChainsByName = absl::flat_hash_map<std::string, Network::DrainableFilterChainSharedPtr>;

class FilterChainBaseAction : public Matcher::Action {
public:
  virtual const Network::FilterChain* get(const FilterChainsByName& filter_chains_by_name,
                                          const StreamInfo::StreamInfo& info) const PURE;
};

class FilterChainImpl : public Network::DrainableFilterChain {
public:
  FilterChainImpl(Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory,
                  std::vector<Network::FilterFactoryCb>&& filters_factory,
                  std::chrono::milliseconds transport_socket_connect_timeout,
                  absl::string_view name)
      : transport_socket_factory_(std::move(transport_socket_factory)),
        filters_factory_(std::move(filters_factory)),
        transport_socket_connect_timeout_(transport_socket_connect_timeout), name_(name) {}

  // Network::FilterChain
  const Network::DownstreamTransportSocketFactory& transportSocketFactory() const override {
    return *transport_socket_factory_;
  }
  std::chrono::milliseconds transportSocketConnectTimeout() const override {
    return transport_socket_connect_timeout_;
  }
  const std::vector<Network::FilterFactoryCb>& networkFilterFactories() const override {
    return filters_factory_;
  }
  void startDraining() override { factory_context_->startDraining(); }

  void setFilterChainFactoryContext(
      Configuration::FilterChainFactoryContextPtr filter_chain_factory_context) {
    ASSERT(factory_context_ == nullptr);
    factory_context_ = std::move(filter_chain_factory_context);
  }

  absl::string_view name() const override { return name_; }

private:
  Configuration::FilterChainFactoryContextPtr factory_context_;
  const Network::DownstreamTransportSocketFactoryPtr transport_socket_factory_;
  const std::vector<Network::FilterFactoryCb> filters_factory_;
  const std::chrono::milliseconds transport_socket_connect_timeout_;
  const std::string name_;
};

/**
 * Implementation of FactoryContext wrapping a Server::Instance and some listener components.
 */
class FactoryContextImpl : public Configuration::FactoryContext {
public:
  FactoryContextImpl(Server::Instance& server, const envoy::config::listener::v3::Listener& config,
                     Network::DrainDecision& drain_decision, Stats::Scope& global_scope,
                     Stats::Scope& listener_scope, bool is_quic);

  // Configuration::FactoryContext
  AccessLog::AccessLogManager& accessLogManager() override;
  Upstream::ClusterManager& clusterManager() override;
  Event::Dispatcher& mainThreadDispatcher() override;
  const Server::Options& options() override;
  Grpc::Context& grpcContext() override;
  Router::Context& routerContext() override;
  bool healthCheckFailed() override;
  Http::Context& httpContext() override;
  Init::Manager& initManager() override;
  const LocalInfo::LocalInfo& localInfo() const override;
  Envoy::Runtime::Loader& runtime() override;
  Stats::Scope& scope() override;
  Stats::Scope& serverScope() override { return server_.stats(); }
  Singleton::Manager& singletonManager() override;
  OverloadManager& overloadManager() override;
  ThreadLocal::SlotAllocator& threadLocal() override;
  OptRef<Admin> admin() override;
  TimeSource& timeSource() override;
  ProtobufMessage::ValidationContext& messageValidationContext() override;
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override;
  Api::Api& api() override;
  ServerLifecycleNotifier& lifecycleNotifier() override;
  ProcessContextOptRef processContext() override;
  Configuration::ServerFactoryContext& getServerFactoryContext() const override;
  Configuration::TransportSocketFactoryContext& getTransportSocketFactoryContext() const override;
  const envoy::config::core::v3::Metadata& listenerMetadata() const override;
  const Envoy::Config::TypedMetadata& listenerTypedMetadata() const override;
  envoy::config::core::v3::TrafficDirection direction() const override;
  Network::DrainDecision& drainDecision() override;
  Stats::Scope& listenerScope() override;
  bool isQuicListener() const override;

private:
  Server::Instance& server_;
  const envoy::config::listener::v3::Listener& config_;
  Network::DrainDecision& drain_decision_;
  Stats::Scope& global_scope_;
  Stats::Scope& listener_scope_;
  bool is_quic_;
};

/**
 * Implementation of FilterChainManager. It owns and exchange filter chains.
 */
class FilterChainManagerImpl : public Network::FilterChainManager,
                               public FilterChainFactoryContextCreator,
                               Logger::Loggable<Logger::Id::config> {
public:
  using FcContextMap =
      absl::flat_hash_map<envoy::config::listener::v3::FilterChain,
                          Network::DrainableFilterChainSharedPtr, MessageUtil, MessageUtil>;
  FilterChainManagerImpl(const std::vector<Network::Address::InstanceConstSharedPtr>& addresses,
                         Configuration::FactoryContext& factory_context,
                         Init::Manager& init_manager)
      : addresses_(addresses), parent_context_(factory_context), init_manager_(init_manager) {}

  FilterChainManagerImpl(const std::vector<Network::Address::InstanceConstSharedPtr>& addresses,
                         Configuration::FactoryContext& factory_context,
                         Init::Manager& init_manager, const FilterChainManagerImpl& parent_manager);

  // FilterChainFactoryContextCreator
  Configuration::FilterChainFactoryContextPtr createFilterChainFactoryContext(
      const ::envoy::config::listener::v3::FilterChain* const filter_chain) override;

  // Network::FilterChainManager
  const Network::FilterChain* findFilterChain(const Network::ConnectionSocket& socket,
                                              const StreamInfo::StreamInfo& info) const override;

  // Add all filter chains into this manager. During the lifetime of FilterChainManagerImpl this
  // should be called at most once.
  void addFilterChains(
      const xds::type::matcher::v3::Matcher* filter_chain_matcher,
      absl::Span<const envoy::config::listener::v3::FilterChain* const> filter_chain_span,
      const envoy::config::listener::v3::FilterChain* default_filter_chain,
      FilterChainFactoryBuilder& filter_chain_factory_builder,
      FilterChainFactoryContextCreator& context_creator);

  static bool isWildcardServerName(const std::string& name);

  // Return the current view of filter chains, keyed by filter chain message. Used by the owning
  // listener to calculate the intersection of filter chains with another listener.
  const FcContextMap& filterChainsByMessage() const { return fc_contexts_; }
  const absl::optional<envoy::config::listener::v3::FilterChain>&
  defaultFilterChainMessage() const {
    return default_filter_chain_message_;
  }
  const Network::DrainableFilterChainSharedPtr& defaultFilterChain() const {
    return default_filter_chain_;
  }

private:
  void convertIPsToTries();
  const Network::FilterChain* findFilterChainUsingMatcher(const Network::ConnectionSocket& socket,
                                                          const StreamInfo::StreamInfo& info) const;

  // Build default filter chain from filter chain message. Skip the build but copy from original
  // filter chain manager if the default filter chain message duplicates the message in origin
  // filter chain manager. Called by addFilterChains().
  void copyOrRebuildDefaultFilterChain(
      const envoy::config::listener::v3::FilterChain* default_filter_chain,
      FilterChainFactoryBuilder& filter_chain_factory_builder,
      FilterChainFactoryContextCreator& context_creator);

  using SourcePortsMap = absl::flat_hash_map<uint16_t, Network::FilterChainSharedPtr>;
  using SourcePortsMapSharedPtr = std::shared_ptr<SourcePortsMap>;
  using SourceIPsMap = absl::flat_hash_map<std::string, SourcePortsMapSharedPtr>;
  using SourceIPsTrie = Network::LcTrie::LcTrie<SourcePortsMapSharedPtr>;
  using SourceIPsTriePtr = std::unique_ptr<SourceIPsTrie>;
  using SourceTypesArray = std::array<std::pair<SourceIPsMap, SourceIPsTriePtr>, 3>;
  using SourceTypesArraySharedPtr = std::shared_ptr<SourceTypesArray>;
  using DirectSourceIPsMap = absl::flat_hash_map<std::string, SourceTypesArraySharedPtr>;
  using DirectSourceIPsTrie = Network::LcTrie::LcTrie<SourceTypesArraySharedPtr>;
  using DirectSourceIPsTriePtr = std::unique_ptr<DirectSourceIPsTrie>;

  // This would nominally be a `std::pair`, but that version crashes the Windows clang_cl compiler
  // for unknown reasons. This variation, which is equivalent, does not crash the compiler.
  // The `std::pair` version was confirmed to crash both clang 11 and clang 12.
  struct DirectSourceIPsPair {
    DirectSourceIPsMap first;
    DirectSourceIPsTriePtr second;
  };

  using ApplicationProtocolsMap = absl::flat_hash_map<std::string, DirectSourceIPsPair>;
  using TransportProtocolsMap = absl::flat_hash_map<std::string, ApplicationProtocolsMap>;
  // Both exact server names and wildcard domains are part of the same map, in which wildcard
  // domains are prefixed with "." (i.e. ".example.com" for "*.example.com") to differentiate
  // between exact and wildcard entries.
  using ServerNamesMap = absl::flat_hash_map<std::string, TransportProtocolsMap>;
  using ServerNamesMapSharedPtr = std::shared_ptr<ServerNamesMap>;
  using DestinationIPsMap = absl::flat_hash_map<std::string, ServerNamesMapSharedPtr>;
  using DestinationIPsTrie = Network::LcTrie::LcTrie<ServerNamesMapSharedPtr>;
  using DestinationIPsTriePtr = std::unique_ptr<DestinationIPsTrie>;
  using DestinationPortsMap =
      absl::flat_hash_map<uint16_t, std::pair<DestinationIPsMap, DestinationIPsTriePtr>>;

  void addFilterChainForDestinationPorts(
      DestinationPortsMap& destination_ports_map, uint16_t destination_port,
      const std::vector<std::string>& destination_ips,
      const absl::Span<const std::string> server_names, const std::string& transport_protocol,
      const absl::Span<const std::string* const> application_protocols,
      const std::vector<std::string>& direct_source_ips,
      const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForDestinationIPs(
      DestinationIPsMap& destination_ips_map, const std::vector<std::string>& destination_ips,
      const absl::Span<const std::string> server_names, const std::string& transport_protocol,
      const absl::Span<const std::string* const> application_protocols,
      const std::vector<std::string>& direct_source_ips,
      const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForServerNames(
      ServerNamesMapSharedPtr& server_names_map_ptr,
      const absl::Span<const std::string> server_names, const std::string& transport_protocol,
      const absl::Span<const std::string* const> application_protocols,
      const std::vector<std::string>& direct_source_ips,
      const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForApplicationProtocols(
      ApplicationProtocolsMap& application_protocol_map,
      const absl::Span<const std::string* const> application_protocols,
      const std::vector<std::string>& direct_source_ips,
      const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForDirectSourceIPs(
      DirectSourceIPsMap& direct_source_ips_map, const std::vector<std::string>& direct_source_ips,
      const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForSourceTypes(
      SourceTypesArraySharedPtr& source_types_array_ptr,
      const envoy::config::listener::v3::FilterChainMatch::ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForSourceIPs(SourceIPsMap& source_ips_map, const std::string& source_ip,
                                  const absl::Span<const Protobuf::uint32> source_ports,
                                  const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForSourcePorts(SourcePortsMapSharedPtr& source_ports_map_ptr,
                                    uint32_t source_port,
                                    const Network::FilterChainSharedPtr& filter_chain);

  const Network::FilterChain*
  findFilterChainForDestinationIP(const DestinationIPsTrie& destination_ips_trie,
                                  const Network::ConnectionSocket& socket) const;
  const Network::FilterChain*
  findFilterChainForServerName(const ServerNamesMap& server_names_map,
                               const Network::ConnectionSocket& socket) const;
  const Network::FilterChain*
  findFilterChainForTransportProtocol(const TransportProtocolsMap& transport_protocols_map,
                                      const Network::ConnectionSocket& socket) const;
  const Network::FilterChain*
  findFilterChainForApplicationProtocols(const ApplicationProtocolsMap& application_protocols_map,
                                         const Network::ConnectionSocket& socket) const;
  const Network::FilterChain*
  findFilterChainForDirectSourceIP(const DirectSourceIPsTrie& direct_source_ips_trie,
                                   const Network::ConnectionSocket& socket) const;
  const Network::FilterChain*
  findFilterChainForSourceTypes(const SourceTypesArray& source_types,
                                const Network::ConnectionSocket& socket) const;

  const Network::FilterChain*
  findFilterChainForSourceIpAndPort(const SourceIPsTrie& source_ips_trie,
                                    const Network::ConnectionSocket& socket) const;

  const FilterChainManagerImpl* getOriginFilterChainManager() { return origin_.value(); }
  // Duplicate the inherent factory context if any.
  Network::DrainableFilterChainSharedPtr
  findExistingFilterChain(const envoy::config::listener::v3::FilterChain& filter_chain_message);

  // Mapping from filter chain message to filter chain. This is used by LDS response handler to
  // detect the filter chains in the intersection of existing listener and new listener.
  FcContextMap fc_contexts_;

  absl::optional<envoy::config::listener::v3::FilterChain> default_filter_chain_message_;
  // The optional fallback filter chain if destination_ports_map_ does not find a matched filter
  // chain.
  Network::DrainableFilterChainSharedPtr default_filter_chain_;

  // Mapping of FilterChain's configured destination ports, IPs, server names, transport protocols
  // and application protocols, using structures defined above.
  DestinationPortsMap destination_ports_map_;

  const std::vector<Network::Address::InstanceConstSharedPtr>& addresses_;
  // This is the reference to a factory context which all the generations of listener share.
  Configuration::FactoryContext& parent_context_;
  std::list<std::shared_ptr<Configuration::FilterChainFactoryContext>> factory_contexts_;

  // Reference to the previous generation of filter chain manager to share the filter chains.
  // Caution: only during warm up could the optional have value.
  absl::optional<const FilterChainManagerImpl*> origin_{nullptr};

  // For FilterChainFactoryContextCreator
  // init manager owned by the corresponding listener. The reference is valid when building the
  // filter chain.
  Init::Manager& init_manager_;

  // Matcher selecting the filter chain name.
  Matcher::MatchTreePtr<Network::MatchingData> matcher_;

  // Index filter chains by name, used by the matcher actions.
  FilterChainsByName filter_chains_by_name_;
};
} // namespace Server
} // namespace Envoy

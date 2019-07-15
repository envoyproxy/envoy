#pragma once

#include <memory>

#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/server/transport_socket_config.h"

#include "common/common/logger.h"
#include "common/init/manager_impl.h"
#include "common/network/cidr_range.h"
#include "common/network/lc_trie.h"

#include "server/lds_api.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Server {

class FilterChainImpl;

class FilterChainFactoryBuilder {
public:
  virtual ~FilterChainFactoryBuilder() = default;
  virtual void setInitManager(Init::Manager& init_manager) PURE;
  virtual std::unique_ptr<Network::FilterChain>
  buildFilterChain(const ::envoy::api::v2::listener::FilterChain& filter_chain) PURE;
};

/**
 * Implementation of FilterChainManager.
 * Encapsulating the subscription from FCDS Api.
 */
class FilterChainManagerImpl : public Network::FilterChainManager,
                               Logger::Loggable<Logger::Id::config> {
public:
  FilterChainManagerImpl(Init::Manager& init_manager,
                         Network::Address::InstanceConstSharedPtr address);

  // Network::FilterChainManager
  const Network::FilterChain*
  findFilterChain(const Network::ConnectionSocket& socket) const override;

  void
  addFilterChain(absl::Span<const ::envoy::api::v2::listener::FilterChain* const> filter_chain_span,
                 std::unique_ptr<FilterChainFactoryBuilder> filter_chain_factory_builder);

  void addFilterChainInternalForFcds(
      absl::Span<const ::envoy::api::v2::listener::FilterChain* const> filter_chain_span,
      std::unique_ptr<FilterChainFactoryBuilder> filter_chain_factory_builder);

  static bool isWildcardServerName(const std::string& name);

  // In order to share between internal class
  using SourcePortsMap = absl::flat_hash_map<uint16_t, Network::FilterChainSharedPtr>;
  using SourcePortsMapSharedPtr = std::shared_ptr<SourcePortsMap>;
  using SourceIPsMap = absl::flat_hash_map<std::string, SourcePortsMapSharedPtr>;
  using SourceIPsTrie = Network::LcTrie::LcTrie<SourcePortsMapSharedPtr>;
  using SourceIPsTriePtr = std::unique_ptr<SourceIPsTrie>;
  using SourceTypesArray = std::array<std::pair<SourceIPsMap, SourceIPsTriePtr>, 3>;
  using ApplicationProtocolsMap = absl::flat_hash_map<std::string, SourceTypesArray>;
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
  static void convertIPsToTries(DestinationPortsMap& destination_ports_map);

private:
  void addFilterChainForDestinationPorts(
      DestinationPortsMap& destination_ports_map, uint16_t destination_port,
      const std::vector<std::string>& destination_ips,
      const absl::Span<const std::string* const> server_names,
      const std::string& transport_protocol,
      const absl::Span<const std::string* const> application_protocols,
      const envoy::api::v2::listener::FilterChainMatch_ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForDestinationIPs(
      DestinationIPsMap& destination_ips_map, const std::vector<std::string>& destination_ips,
      const absl::Span<const std::string* const> server_names,
      const std::string& transport_protocol,
      const absl::Span<const std::string* const> application_protocols,
      const envoy::api::v2::listener::FilterChainMatch_ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForServerNames(
      ServerNamesMapSharedPtr& server_names_map_ptr,
      const absl::Span<const std::string* const> server_names,
      const std::string& transport_protocol,
      const absl::Span<const std::string* const> application_protocols,
      const envoy::api::v2::listener::FilterChainMatch_ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForApplicationProtocols(
      ApplicationProtocolsMap& application_protocol_map,
      const absl::Span<const std::string* const> application_protocols,
      const envoy::api::v2::listener::FilterChainMatch_ConnectionSourceType source_type,
      const std::vector<std::string>& source_ips,
      const absl::Span<const Protobuf::uint32> source_ports,
      const Network::FilterChainSharedPtr& filter_chain);
  void addFilterChainForSourceTypes(
      SourceTypesArray& source_types_array,
      const envoy::api::v2::listener::FilterChainMatch_ConnectionSourceType source_type,
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
  findFilterChainForSourceTypes(const SourceTypesArray& source_types,
                                const Network::ConnectionSocket& socket) const;

  const Network::FilterChain*
  findFilterChainForSourceIpAndPort(const SourceIPsTrie& source_ips_trie,
                                    const Network::ConnectionSocket& socket) const;

  struct FilterChainLookup {
    ~FilterChainLookup() {
      // to abandon all the pending registered targets
      init_watcher_.reset();
    }
    // Mapping of FilterChain's configured destination ports, IPs, server names, transport protocols
    // and application protocols, using structures defined above.
    DestinationPortsMap destination_ports_map_;

    std::unique_ptr<FilterChainFactoryBuilder> filter_chain_builder_;

    std::unordered_map<::envoy::api::v2::listener::FilterChain,
                       std::shared_ptr<Network::FilterChain>, MessageUtil, MessageUtil>
        existing_active_filter_chains_;

    // Used during warm up, notified by dependencies, and notify parent that the index is ready.
    Init::ManagerImpl dynamic_init_manager_{"lookup_init_manager"};

    // `has_active_lookup_` is tricky here. It seems the condition is mutable. However, if we
    // maintain the invariable below, in each lifetime of `FilterChainLookup` the value never
    // change. To `FilterChainLookup` user Only transform the current `warming_lookup_` to
    // `active_lookup_` Access `has_active_lookup_` at the beginning of `FilterChainLookup`. The
    // value is stale soon after. Consider the case
    //   another FCDS update request comes and new warming lookup is created.
    // Access `has_active_lookup_` from main thread.
    bool has_active_lookup_{false};

    std::unique_ptr<Init::Watcher> init_watcher_;

    Init::Manager& getInitManager() { return dynamic_init_manager_; }
    void initialize();
  };

  std::unique_ptr<FilterChainLookup> createFilterChainLookup();

  void lookupWarmed(FilterChainLookup* warming_lookup);

  // The invariant:
  // Once the active one is ready, there is always an active one until shutdown.
  // Warming one could be replaced by another warming lookup. The user is responsible for not
  // overriding a new warming one by elder one. The warming lookup may replace the active_lookup
  // atomically, or replaced by another warming up. If warming one is replaced by another warming
  // one, the former one should have no side effect. If the warming eventually replaces the active
  // one, the warming one could assume the active one never changed during the warm up.
  std::shared_ptr<FilterChainLookup> active_lookup_;
  std::shared_ptr<FilterChainLookup> warming_lookup_;

  Init::Manager& init_manager_;
  bool target_ready_{false};
  Init::TargetImpl init_target_{"filter_chain_manager", [this]() {
                                  ENVOY_LOG(debug, "initializing filter chain manager");
                                  // It's possible that the dependency is completed before adding to
                                  // init manager.
                                  if (target_ready_) {
                                    init_target_.ready();
                                  }
                                }};

  // The address should never change during the lifetime of the filter chain manager.
  const Network::Address::InstanceConstSharedPtr address_;
};

class FilterChainImpl : public Network::FilterChain {
public:
  FilterChainImpl(Network::TransportSocketFactoryPtr&& transport_socket_factory,
                  std::vector<Network::FilterFactoryCb>&& filters_factory)
      : transport_socket_factory_(std::move(transport_socket_factory)),
        filters_factory_(std::move(filters_factory)) {}

  // Network::FilterChain
  const Network::TransportSocketFactory& transportSocketFactory() const override {
    return *transport_socket_factory_;
  }

  const std::vector<Network::FilterFactoryCb>& networkFilterFactories() const override {
    return filters_factory_;
  }

private:
  const Network::TransportSocketFactoryPtr transport_socket_factory_;
  const std::vector<Network::FilterFactoryCb> filters_factory_;
};

} // namespace Server
} // namespace Envoy
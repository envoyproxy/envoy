#pragma once

#include <memory>

#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/server/transport_socket_config.h"

#include "common/common/logger.h"
#include "common/network/cidr_range.h"
#include "common/network/lc_trie.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Server {

class FilterChainFactoryBuilder {
public:
  virtual ~FilterChainFactoryBuilder() = default;
  virtual std::unique_ptr<Network::FilterChain>
  buildFilterChain(const ::envoy::api::v2::listener::FilterChain& filter_chain) const PURE;
};

/**
 * Implementation of FilterChainManager.
 */
class FilterChainManagerImpl : public Network::FilterChainManager,
                               Logger::Loggable<Logger::Id::config> {
public:
  explicit FilterChainManagerImpl(const Network::Address::InstanceConstSharedPtr& address)
      : address_(address) {}

  // Network::FilterChainManager
  const Network::FilterChain*
  findFilterChain(const Network::ConnectionSocket& socket) const override;

  void
  addFilterChain(absl::Span<const ::envoy::api::v2::listener::FilterChain* const> filter_chain_span,
                 FilterChainFactoryBuilder& b);
  static bool isWildcardServerName(const std::string& name);

private:
  void convertIPsToTries();
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

  // Mapping of FilterChain's configured destination ports, IPs, server names, transport protocols
  // and application protocols, using structures defined above.
  DestinationPortsMap destination_ports_map_;
  const Network::Address::InstanceConstSharedPtr address_;
};

/**
 * Base implementation for TCP and QUIC filter chain.
 */
class FilterChainImplBase : public Network::FilterChain {
public:
  FilterChainImplBase(std::vector<Network::FilterFactoryCb>&& filters_factory)
      : filters_factory_(std::move(filters_factory)) {}

  // Network::FilterChain
  const std::vector<Network::FilterFactoryCb>& networkFilterFactories() const override {
    return filters_factory_;
  }

private:
  const std::vector<Network::FilterFactoryCb> filters_factory_;
};

class TcpFilterChainImpl : public FilterChainImplBase {
public:
  TcpFilterChainImpl(Network::TransportSocketFactoryPtr&& transport_socket_factory,
                     std::vector<Network::FilterFactoryCb>&& filters_factory)
      : FilterChainImplBase(std::move(filters_factory)),
        transport_socket_factory_(std::move(transport_socket_factory)) {}

  // Network::FilterChain
  const Network::TransportSocketFactory* transportSocketFactory() const override {
    return transport_socket_factory_.get();
  }

private:
  const Network::TransportSocketFactoryPtr transport_socket_factory_;
};

class QuicFilterChainImpl : public FilterChainImplBase {
public:
  QuicFilterChainImpl(std::unique_ptr<Ssl::ContextConfig> tls_context_config,
                      std::vector<Network::FilterFactoryCb>&& filters_factory)
      : FilterChainImplBase(std::move(filters_factory)),
        tls_context_config_(std::move(tls_context_config)) {}

  // Network::FilterChain
  const Network::TransportSocketFactory* transportSocketFactory() const override { return nullptr; }

  const Ssl::ContextConfig& tlsContextConfig() const { return tls_context_config_.get(); }

private:
  std::unique_ptr<const Ssl::ContextConfig> tls_context_config_;
};

} // namespace Server
} // namespace Envoy

#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "contrib/reverse_connection/bootstrap/source/reverse_conn_global_registry.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_conn_thread_local_registry.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_handler.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_manager.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_initiator.h"
#include "contrib/reverse_connection/bootstrap/source/reversed_connection_impl.h"
#include "contrib/reverse_connection/bootstrap/source/conn_pool.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/network/mocks.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Mock class for ReverseConnRegistry
class MockReverseConnRegistry : public ReverseConnRegistry {
public:
  MOCK_METHOD(Bootstrap::ReverseConnection::RCThreadLocalRegistry*, getLocalRegistry, ());
  MOCK_METHOD(absl::StatusOr<Network::ReverseConnectionListenerConfigPtr>, fromAnyConfig,
              (const google::protobuf::Any& config));
};

// Mock class for RCThreadLocalRegistry
class MockRCThreadLocalRegistry : public RCThreadLocalRegistry {
public:
  MockRCThreadLocalRegistry(Event::Dispatcher& dispatcher, Stats::Scope& scope,
                            std::string stat_prefix, Upstream::ClusterManager& cluster_manager)
      : RCThreadLocalRegistry(dispatcher, scope, stat_prefix, cluster_manager) {}

  MOCK_METHOD(Network::ReverseConnectionListenerPtr, createActiveReverseConnectionListener,
              (Network::ConnectionHandler& conn_handler, Event::Dispatcher& dispatcher,
               Network::ListenerConfig& config),
              (override));
  MOCK_METHOD(ReverseConnectionManager&, getRCManager, ());
  MOCK_METHOD(ReverseConnectionHandler&, getRCHandler, ());
};

// Mock class for ReverseConnectionHandler
class MockReverseConnectionHandler : public ReverseConnectionHandler {
public:
  MockReverseConnectionHandler(Event::Dispatcher* dispatcher) : ReverseConnectionHandler(dispatcher) {}

  MOCK_METHOD(void, addConnectionSocket,
              (const std::string& node_id, const std::string& cluster_id,
               Network::ConnectionSocketPtr socket, bool expects_proxy_protocol,
               const std::chrono::seconds& ping_interval, bool rebalanced));
  MOCK_METHOD(void, post,
              (const std::string& node_id, const std::string& cluster_id,
               Network::ConnectionSocketPtr socket, bool expects_proxy_protocol,
               const std::chrono::seconds& ping_interval));
  MOCK_METHOD(RCSocketPair, getConnectionSocket, (const std::string& node_id, bool rebalanced));
  MOCK_METHOD(void, rebalanceGetConnectionSocket,
              (const std::string& key, bool rebalanced,
               std::shared_ptr<std::promise<RCSocketPair>> socket_promise));
  MOCK_METHOD(size_t, getNumberOfSocketsByNode, (const std::string& node_id));
  MOCK_METHOD(size_t, getNumberOfSocketsByCluster, (const std::string& cluster));
  MOCK_METHOD(SocketCountMap, getSocketCountMap, ());
  MOCK_METHOD(void, markSocketDead, (const int fd, const bool used));
  MOCK_METHOD((absl::flat_hash_map<std::string, size_t>), getConnectionStats, ());
  MOCK_METHOD(void, initializeStats, (Stats::Scope& scope));
};

// Mock class for ReverseConnectionManager
class MockReverseConnectionManager : public ReverseConnectionManager {
public:
  MockReverseConnectionManager()
      : ReverseConnectionManager(dispatcher_, cluster_manager_) {}

  MOCK_METHOD(void, initializeStats, (Stats::Scope& scope));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (const));
  MOCK_METHOD(Network::ConnectionHandler*, connectionHandler, (), (const));
  MOCK_METHOD(Upstream::ClusterManager&, clusterManager, (), (const));
  MOCK_METHOD(void, findOrCreateRCInitiator,
              (const Network::ListenerConfig& listener_ref, const std::string& src_node_id,
               const std::string& src_cluster_id, const std::string& src_tenant_id,
              (const absl::flat_hash_map<std::string, uint32_t>& remote_cluster_to_conns)));
  MOCK_METHOD(void, registerRCInitiators, (Network::ConnectionHandler& conn_handler,
                                           const Network::ListenerConfig& listener_ref));
  MOCK_METHOD(void, unregisterRCInitiator, (const Network::ListenerConfig& listener_ref));
  MOCK_METHOD(void, registerConnection, (const std::string& connectionKey,
                                         ReverseConnectionInitiator* rc_inititator));
  MOCK_METHOD(int, unregisterConnection, (const std::string& connectionKey));
  MOCK_METHOD(void, notifyConnectionClose, (const std::string& connectionKey, bool is_used));
  MOCK_METHOD(void, markConnUsed, (const std::string& connectionKey));
  MOCK_METHOD(uint64_t, getNumberOfSockets, (const std::string& key));
  MOCK_METHOD((absl::flat_hash_map<std::string, size_t>), getSocketCountMap, ());
  MOCK_METHOD(ReverseConnectionInitiator*, getRCInitiatorPtr, (const Network::ListenerConfig& listener_ref));
  MOCK_METHOD(void, createRCInitiatorDone, (ReverseConnectionInitiator* initiator));

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
};

// Mock class for ReverseConnectionInitiator
class MockReverseConnectionInitiator : public ReverseConnectionInitiator {
public:
  MockReverseConnectionInitiator(const Network::ListenerConfig& listener_ref,
                                 const ReverseConnectionOptions& options,
                                 ReverseConnectionManager& rc_manager, Stats::Scope& scope)
      : ReverseConnectionInitiator(listener_ref, options, rc_manager, scope) {}

  MOCK_METHOD(void, removeStaleHostAndCloseConnections, (const std::string& host));
  MOCK_METHOD(void, maybeUpdateHostsMappingsAndConnections,
              (const std::string& cluster_id, const std::vector<std::string>& hosts));
  MOCK_METHOD(bool, maintainConnCount, ());
  MOCK_METHOD(bool, initiateOneReverseConnection, (const std::string& remote_cluster_id, const std::string& host));
  MOCK_METHOD(std::string, getRemoteClusterForConn, (const std::string& connectionKey));
  MOCK_METHOD(void, notifyConnectionClose, (const std::string& connectionKey, const bool is_used));
  MOCK_METHOD(void, initializeStats, (Stats::Scope& scope));
  MOCK_METHOD(void, addStatshandlerForCluster, (const std::string& cluster_name));
  MOCK_METHOD(void, markConnUsed, (const std::string& connectionKey));
  MOCK_METHOD(uint64_t, getNumberOfSockets, (const std::string& key));
  MOCK_METHOD(void, getSocketCountMap, ((absl::flat_hash_map<std::string, size_t>& response)));
  MOCK_METHOD(uint64_t, getID, ());
};

// Mock class for ReversedClientConnectionImpl
class MockReversedClientConnectionImpl : public ReversedClientConnectionImpl {
public:
  MockReversedClientConnectionImpl(
      Network::Address::InstanceConstSharedPtr address,
      Network::Address::InstanceConstSharedPtr source_address,
      Event::Dispatcher& dispatcher,
      Network::TransportSocketPtr&& transport_socket,
      Network::ConnectionSocketPtr&& downstream_socket,
      Envoy::Extensions::Bootstrap::ReverseConnection::RCThreadLocalRegistry& registry,
      bool expects_proxy_protocol)
      : ReversedClientConnectionImpl(address, source_address, dispatcher, std::move(transport_socket),
                                     std::move(downstream_socket), registry, expects_proxy_protocol) {}

  MOCK_METHOD(void, connect, ());
  MOCK_METHOD(void, close, (Network::ConnectionCloseType type, absl::string_view details));
  MOCK_METHOD(void, SendProxyProtocolHeader, ());
};

// Mock class for ActiveClient
class MockActiveClient : public ActiveClient {
public:
  MockActiveClient(Envoy::Http::HttpConnPoolImplBase& parent,
                   OptRef<Upstream::Host::CreateConnectionData> data,
                   Http::CreateConnectionDataFn connection_fn = nullptr)
      : ActiveClient(parent, data, connection_fn) {}
};

// Mock class for ReverseConnPoolFactoryImpl
class MockReverseConnPoolFactoryImpl : public ReverseConnPoolFactoryImpl {
public:
  MOCK_METHOD(Http::ConnectionPool::InstancePtr, allocateConnPool,
              (Event::Dispatcher& dispatcher, Random::RandomGenerator& random_generator,
               Singleton::Manager& singleton_manager, Upstream::HostConstSharedPtr host,
               Upstream::ResourcePriority priority, const Network::ConnectionSocket::OptionsSharedPtr& options,
               const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
               Upstream::ClusterConnectivityState& state,
               absl::optional<Http::HttpServerPropertiesCache::Origin> origin,
               Http::HttpServerPropertiesCacheSharedPtr http_server_properties_cache));
  MOCK_METHOD(std::string, name, (), (const));
};

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
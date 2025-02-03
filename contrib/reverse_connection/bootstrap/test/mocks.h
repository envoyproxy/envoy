#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "contrib/reverse_connection/bootstrap/source/reverse_conn_global_registry.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_conn_thread_local_registry.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_handler.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_manager.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_initiator.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/network/mocks.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Mock class for ReverseConnectionManager
class MockReverseConnectionManager : public ReverseConnectionManager {
public:
  MockReverseConnectionManager();

  MOCK_METHOD(void, initializeStats, (Stats::Scope& scope));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (const));
  MOCK_METHOD(Network::ConnectionHandler*, connectionHandler, (), (const));
  MOCK_METHOD(void, setConnectionHandler, (Network::ConnectionHandler& conn_handler));
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

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
};

// // Mock class for ReverseConnectionHandler
// class MockReverseConnectionHandler : public ReverseConnectionHandler {
// public:
//   MockReverseConnectionHandler();

//   MOCK_METHOD(void, addConnectionSocket,
//               (const std::string& node_id, const std::string& cluster_id,
//                Network::ConnectionSocketPtr socket, bool expects_proxy_protocol,
//                const std::chrono::seconds& ping_interval, bool rebalanced));
//   MOCK_METHOD(void, post,
//               (const std::string& node_id, const std::string& cluster_id,
//                Network::ConnectionSocketPtr socket, bool expects_proxy_protocol,
//                const std::chrono::seconds& ping_interval));
//   MOCK_METHOD(RCSocketPair, getConnectionSocket, (const std::string& node_id, bool rebalanced));
//   MOCK_METHOD(void, rebalanceGetConnectionSocket,
//               (const std::string& key, bool rebalanced,
//                std::shared_ptr<std::promise<RCSocketPair>> socket_promise));
//   MOCK_METHOD(size_t, getNumberOfSocketsByNode, (const std::string& node_id));
//   MOCK_METHOD(size_t, getNumberOfSocketsByCluster, (const std::string& cluster));
//   MOCK_METHOD(SocketCountMap, getSocketCountMap, ());
//   MOCK_METHOD(void, markSocketDead, (const int fd, const bool used));
//   MOCK_METHOD((absl::flat_hash_map<std::string, size_t>), getConnectionStats, ());
//   MOCK_METHOD(void, initializeStats, (Stats::Scope& scope));
// };

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
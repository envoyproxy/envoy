#include <cstdint>
#include "contrib/reverse_connection/bootstrap/test/mocks.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/network/address_impl.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::NiceMock;
using testing::ReturnRef;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseConnectionInitiatorTest : public testing::Test {
public:
  ReverseConnectionInitiatorTest() {
     // Set up listener properties
    static const std::string listener_name = "test_listener";
    const uint64_t listener_tag = 1;
    static const std::string version_info = "v1";
  
    ON_CALL(listener_config_, name()).WillByDefault(ReturnRef(listener_name));
    ON_CALL(listener_config_, listenerTag()).WillByDefault(Return(listener_tag));
    ON_CALL(listener_config_, versionInfo()).WillByDefault(ReturnRef(version_info));
  }

  void SetUp(){
    // Initialize ReverseConnectionOptions
    rc_options_.src_node_id_ = "test_node_id";
    rc_options_.src_cluster_id_ = "test_cluster_id";
    rc_options_.src_tenant_id_ = "test_tenant_id";
    rc_options_.remote_cluster_to_conns_ = {{"remote_cluster_1", 1}};
    // Initialize ReverseConnectionInitiator
    rc_manager_ = std::make_shared<ReverseConnectionManager>(dispatcher_, cluster_manager_);
    base_scope_ = Stats::ScopeSharedPtr(stats_store.createScope("rc_initiator."));
    rc_initiator_ = std::make_unique<ReverseConnectionInitiator>(
        listener_config_, rc_options_, *rc_manager_, *base_scope_);
  }

  struct MockConnectionSetup {
    std::unique_ptr<NiceMock<Network::MockClientConnection>> connection;
    std::shared_ptr<NiceMock<Network::MockConnectionSocket>> socket;
    Network::ConnectionInfoProviderSharedPtr provider;
    Upstream::MockHost::MockCreateConnectionData conn_info;
    ReverseConnectionInitiator::RCConnectionManagerPtr rcManagerPtr;
    std::unique_ptr<NiceMock<Network::MockIoHandle>> io_handle;
  };

  MockConnectionSetup createMockConnection(const std::string& remote_cluster, 
                                          const std::string& remote_address, 
                                          const std::string& local_address, 
                                          uint32_t port) {
    MockConnectionSetup setup;

    // Create the mock connection
    setup.connection = std::make_unique<NiceMock<Network::MockClientConnection>>();
    // Create the ConnectionInfoProvider
    setup.provider = std::make_shared<Network::ConnectionInfoSetterImpl>(
        std::make_shared<Network::Address::Ipv4Instance>(local_address, port, nullptr),
        std::make_shared<Network::Address::Ipv4Instance>(remote_address, port, nullptr));

    // Create the mock socket
    setup.socket = std::make_shared<NiceMock<Network::MockConnectionSocket>>();
    setup.socket->connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>(local_address, port));
    setup.socket->connection_info_provider_->setRemoteAddress(
        std::make_shared<Network::Address::Ipv4Instance>(remote_address, port));

    EXPECT_EQ(setup.socket->connection_info_provider_->localAddress()->asString(),
              local_address + ":" + std::to_string(port));


    // setup.socket->io_handle_ = std::make_unique<Network::Test::IoSocketHandlePlatformImpl>();
    // EXPECT_CALL(*setup.socket, ioHandle()).WillRepeatedly(ReturnRef(*setup.socket->io_handle_));
    // setup.socket->io_handle_->initializeFileEvent(
    //   dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
    //   Event::FileReadyType::Read | Event::FileReadyType::Closed);
    setup.io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    EXPECT_CALL(*setup.io_handle, isOpen()).WillRepeatedly(Return(true));
    // EXPECT_CALL(*setup.socket, ioHandle()).WillRepeatedly(ReturnRef(*setup.io_handle));
    // EXPECT_CALL(*setup.io_handle, resetFileEvents());
    // EXPECT_CALL(*setup.socket, isOpen()).WillRepeatedly(Return(true));
    // EXPECT_CALL(*setup.connection, setConnectionReused(_));

    // Tie the mock connection to the provider and socket
    EXPECT_CALL(*setup.connection, connectionInfoProvider()).WillRepeatedly(ReturnRef(*setup.provider));
    EXPECT_CALL(*setup.connection, getSocket()).WillRepeatedly(ReturnRef(setup.socket));
    

    // Mock cluster information
    setup.conn_info.connection_ = setup.connection.get();
    const std::string url = "tcp://" + remote_address + ":" + std::to_string(port);
    setup.conn_info.host_description_ =
        Upstream::makeTestHost(std::make_unique<NiceMock<Upstream::MockClusterInfo>>(),
                              url, dispatcher_.timeSource());

    cluster_manager_.initializeThreadLocalClusters({remote_cluster});
    EXPECT_CALL(cluster_manager_, getThreadLocalCluster(remote_cluster))
        .WillOnce(Return(&thread_local_cluster_));
    EXPECT_CALL(thread_local_cluster_, tcpConn_(_)).WillOnce(Return(setup.conn_info));
    EXPECT_CALL(*setup.connection, addConnectionCallbacks(_));
    EXPECT_CALL(*setup.connection, addReadFilter(_));
    EXPECT_CALL(*setup.connection, connect());
    EXPECT_CALL(*setup.connection, write(_, false));
    return setup;
  }

  void createRCManager(MockConnectionSetup& setup) {
    setup.rcManagerPtr = std::make_unique<ReverseConnectionInitiator::RCConnectionManager>(
        *rc_manager_, listener_config_, std::move(setup.connection));
  }

  void reverseConnectionDone(const std::string& error, MockConnectionSetup& setup,
                             bool connectionClosed) {
    rc_initiator_->reverseConnectionDone(error, std::move(setup.rcManagerPtr), connectionClosed);
  }

  absl::flat_hash_map<std::string, std::string>& getConnToHostMap() {
    return rc_initiator_->rc_conn_to_host_map_;
  }

  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>& getClusterToHostMap() {
    return rc_initiator_->cluster_to_resolved_hosts_map_;
  }

  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>& getHostToConnMap() {
    return rc_initiator_->host_to_rc_conns_map_;
  }

  absl::flat_hash_map<std::string, std::string>& getHostToClusterMap() {
    return rc_initiator_->host_to_cluster_map_;
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
  NiceMock<Network::MockConnectionHandler> connection_handler_;
  NiceMock<Network::MockIoHandle> io_handle_;
  NiceMock<Stats::MockStore> store_;
  Stats::IsolatedStoreImpl stats_store;
  // Stats::MockScope& base_scope_{store_.mockScope()};
  Stats::ScopeSharedPtr base_scope_;
  Stats::MockScope& reverse_conn_scope_{store_.mockScope()};
  NiceMock<Network::MockListenerConfig> listener_config_;
  ReverseConnectionInitiator::ReverseConnectionOptions rc_options_;
  std::unique_ptr<ReverseConnectionInitiator> rc_initiator_;
  std::shared_ptr<ReverseConnectionManager> rc_manager_;
};

TEST_F(ReverseConnectionInitiatorTest, ReverseConnFailureEmptyParams) {
  // Empty host param should result in failed reverse conn initiation.
  const std::string remote_cluster = "remote_cluster_1";
  const std::string remote_host = "";

  // Initiate the reverse connection.
  bool success = rc_initiator_->initiateOneReverseConnection(remote_cluster, remote_host);
  EXPECT_FALSE(success);
}

TEST_F(ReverseConnectionInitiatorTest, ReverseConnFailureUnknownCluster) {
  const std::string remote_cluster = "remote_cluster_1";
  const std::string remote_address = "127.0.0.1";
  const uint32_t port = 80;

  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(remote_cluster)).WillOnce(Return(nullptr));

  // Initiate the reverse connection.
  bool success = rc_initiator_->initiateOneReverseConnection(remote_cluster, remote_address + ":" + std::to_string(port));
  EXPECT_FALSE(success);
}

TEST_F(ReverseConnectionInitiatorTest, InitiateReverseConnForCluster) {
  // Add cluster -> host mapping.
  const std::string remote_cluster = "remote_cluster_1";
  const std::vector<std::string> remote_hosts = {"127.0.0.2:80", "127.0.0.3:80"};
  rc_initiator_->maybeUpdateHostsMappingsAndConnections(remote_cluster, remote_hosts);

  EXPECT_EQ(getClusterToHostMap().size(), 1);
  EXPECT_EQ(getClusterToHostMap()[remote_cluster].size(), 2);
  EXPECT_EQ(getHostToClusterMap().size(), 2);

  // Initiate reverse connection from 127.0.0.1 -> 127.0.0.2
  const std::string remote_address = "127.0.0.2";
  const std::string local_address = "127.0.0.1";
  const uint32_t port = 80;
  const std::string expected_conn_key = local_address + ":" + std::to_string(port);
  const std::string remote_host = remote_address + ":" + std::to_string(port);

  // RCInitiator should not have any connection key -> remote cluster mapping since reverse conn has not been initiated yet.
  EXPECT_EQ(rc_initiator_->getRemoteClusterForConn(expected_conn_key), "");

  // Call the helper to set up the connection.
  MockConnectionSetup setup = createMockConnection(remote_cluster, remote_address, local_address, port);

  // Initiate the reverse connection.
  bool success = rc_initiator_->initiateOneReverseConnection(remote_cluster, remote_host);
  EXPECT_TRUE(success);

  // RCInitiator should have the connection key -> remote cluster mapping.
  EXPECT_EQ(rc_initiator_->getRemoteClusterForConn(expected_conn_key), remote_cluster);

  EXPECT_EQ(getConnToHostMap().size(), 1);
  EXPECT_EQ(getConnToHostMap()[expected_conn_key], remote_host);
}

TEST_F(ReverseConnectionInitiatorTest, HostRemovalAfterReverseConnInitiation) {
  // Add cluster -> host mapping.
  const std::string remote_cluster = "remote_cluster_1";
  const std::vector<std::string> remote_hosts = {"127.0.0.2:80", "127.0.0.3:80"};
  rc_initiator_->maybeUpdateHostsMappingsAndConnections(remote_cluster, remote_hosts);
  EXPECT_EQ(getClusterToHostMap().size(), 1);
  EXPECT_EQ(getClusterToHostMap()[remote_cluster].size(), 2);
  EXPECT_EQ(getHostToClusterMap().size(), 2);

  // Initiate reverse connection from 127.0.0.1 -> 127.0.0.2
  const std::string remote_address = "127.0.0.2";
  const std::string local_address = "127.0.0.1";
  const uint32_t port = 80;
  const std::string expected_conn_key = local_address + ":" + std::to_string(port);
  const std::string remote_host = remote_address + ":" + std::to_string(port);

  // RCInitiator should not have any connection key -> remote cluster mapping since reverse conn has not been initiated yet.
  EXPECT_EQ(rc_initiator_->getRemoteClusterForConn(expected_conn_key), "");

  // Call the helper to set up the connection.
  MockConnectionSetup setup = createMockConnection(remote_cluster, remote_address, local_address, port);

  // Initiate the reverse connection.
  bool success = rc_initiator_->initiateOneReverseConnection(remote_cluster, remote_host);
  EXPECT_TRUE(success);

  // RCInitiator should have the connection key -> remote cluster mapping.
  EXPECT_EQ(rc_initiator_->getRemoteClusterForConn(expected_conn_key), remote_cluster);
  EXPECT_EQ(getConnToHostMap().size(), 1);
  EXPECT_EQ(getConnToHostMap()[expected_conn_key], remote_host);

  createRCManager(setup);

  // Remove host 127.0.0.2 for remote_cluster.
  const std::vector<std::string> changed_hosts = {"127.0.0.3:80"};
  rc_initiator_->maybeUpdateHostsMappingsAndConnections(remote_cluster, changed_hosts);
  EXPECT_EQ(getClusterToHostMap()[remote_cluster].size(), 1);
  EXPECT_EQ(getHostToClusterMap().size(), 1);

  // The connection to host 127.0.0.2 should have been removed from the map.
  EXPECT_EQ(rc_initiator_->getRemoteClusterForConn(expected_conn_key), "");

  // The connection mapping should be removed from rc_conn_to_host_map_ because
  // a conn -> remote cluster will not be found.
  reverseConnectionDone("", setup, false);
  EXPECT_EQ(getConnToHostMap().size(), 0);
}

TEST_F(ReverseConnectionInitiatorTest, ReverseConnCloseAfterInitiation) {
  // Add cluster -> host mapping.
  const std::string remote_cluster = "remote_cluster_1";
  const std::vector<std::string> remote_hosts = {"127.0.0.2:80"};
  rc_initiator_->maybeUpdateHostsMappingsAndConnections(remote_cluster, remote_hosts);
  EXPECT_EQ(getClusterToHostMap()[remote_cluster].size(), 1);
  EXPECT_EQ(getHostToClusterMap().size(), 1);

  // Initiate reverse connection from 127.0.0.1 -> 127.0.0.2
  const std::string remote_address = "127.0.0.2";
  const std::string local_address = "127.0.0.1";
  const uint32_t port = 80;
  const std::string expected_conn_key = local_address + ":" + std::to_string(port);
  const std::string remote_host = remote_address + ":" + std::to_string(port);

  // Call the helper to set up the connection and initiate the reverse connection.
  MockConnectionSetup setup = createMockConnection(remote_cluster, remote_address, local_address, port);
  bool success = rc_initiator_->initiateOneReverseConnection(remote_cluster, remote_host);
  EXPECT_TRUE(success);
  // RCInitiator should have the connection key -> remote cluster mapping.
  EXPECT_EQ(rc_initiator_->getRemoteClusterForConn(expected_conn_key), remote_cluster);  
  createRCManager(setup);

  // Trigger a remote close event on the connection.
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(Invoke([this, &setup]() -> void {
    reverseConnectionDone("", setup, true);
  }));
  setup.rcManagerPtr->onEvent(Network::ConnectionEvent::RemoteClose);
  // No entry should be added to host_to_rc_conns_map_ map since the connection is closed.
  EXPECT_EQ(getHostToConnMap().size(), 0);                  
}

// TEST_F(ReverseConnectionInitiatorTest, ReverseConnEstablished) {
//   // Add cluster -> host mapping.
//   const std::string remote_cluster = "remote_cluster_1";
//   const std::vector<std::string> remote_hosts = {"127.0.0.2:80"};
//   rc_initiator_->maybeUpdateHostsMappingsAndConnections(remote_cluster, remote_hosts);
//   EXPECT_EQ(getClusterToHostMap()[remote_cluster].size(), 1);
//   EXPECT_EQ(getHostToClusterMap().size(), 1);

//   // Initiate reverse connection from 127.0.0.1 -> 127.0.0.2
//   const std::string remote_address = "127.0.0.2";
//   const std::string local_address = "127.0.0.1";
//   const uint32_t port = 80;
//   const std::string expected_conn_key = local_address + ":" + std::to_string(port);
//   const std::string remote_host = remote_address + ":" + std::to_string(port);

//   // Call the helper to set up the connection and initiate the reverse connection.
//   MockConnectionSetup setup = createMockConnection(remote_cluster, remote_address, local_address, port);
//   bool success = rc_initiator_->initiateOneReverseConnection(remote_cluster, remote_host);
//   EXPECT_TRUE(success);
//   // RCInitiator should have the connection key -> remote cluster mapping.
//   EXPECT_EQ(rc_initiator_->getRemoteClusterForConn(expected_conn_key), remote_cluster);

//   rc_initiator_->addStatshandlerForCluster(remote_cluster);

//   rc_manager_->setConnectionHandler(connection_handler_);
//   EXPECT_CALL(connection_handler_, saveUpstreamConnection(_, _));

//   EXPECT_CALL(*setup.connection, setConnectionReused(_));
//   EXPECT_NE(setup.io_handle, nullptr);
//   EXPECT_CALL(*setup.socket, ioHandle()).WillRepeatedly(ReturnRef(*setup.io_handle));
//   EXPECT_CALL(*setup.io_handle, resetFileEvents()).WillOnce(Invoke([]() {
//         ;
//     }));
//   EXPECT_CALL(*setup.socket, isOpen()).WillRepeatedly(Return(true));

//   createRCManager(setup);
//   reverseConnectionDone("", setup, false);
//   EXPECT_EQ(getHostToConnMap().size(), 1);
//   EXPECT_EQ(*getHostToConnMap()[remote_host].begin(), expected_conn_key);
// }

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
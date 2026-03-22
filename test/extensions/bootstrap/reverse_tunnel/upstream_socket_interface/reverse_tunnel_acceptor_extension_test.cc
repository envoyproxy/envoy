#include <memory>

#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"

#include "source/common/network/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/reverse_tunnel_reporting_service/reporter.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseTunnelAcceptorExtensionTest : public testing::Test {
protected:
  ReverseTunnelAcceptorExtensionTest() {
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
    config_.set_stat_prefix("reverse_connections");
    // Enable detailed stats for tests that need per-node/cluster stats.
    config_.set_enable_detailed_stats(true);
    socket_interface_ = std::make_unique<ReverseTunnelAcceptor>(context_);
    extension_ =
        std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, config_);
  }

  void setupThreadLocalSlot() {
    thread_local_registry_ =
        std::make_shared<UpstreamSocketThreadLocal>(dispatcher_, extension_.get());
    tls_slot_ = ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>::makeUnique(thread_local_);
    thread_local_.setDispatcher(&dispatcher_);
    tls_slot_->set([registry = thread_local_registry_](Event::Dispatcher&) { return registry; });
    extension_->tls_slot_ = std::move(tls_slot_);
    extension_->socket_interface_->extension_ = extension_.get();
  }

  void setupAnotherThreadLocalSlot() {
    another_thread_local_registry_ =
        std::make_shared<UpstreamSocketThreadLocal>(another_dispatcher_, extension_.get());
  }

  void TearDown() override {
    tls_slot_.reset();
    thread_local_registry_.reset();
    extension_.reset();
    socket_interface_.reset();
  }

  auto getConfigWithReporter() {
    envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
        UpstreamReverseConnectionSocketInterface custom_config;

    auto* reporter_config = custom_config.mutable_reporter_config();
    reporter_config->set_name(MOCK_REPORTER);
    reporter_config->mutable_typed_config()->PackFrom(Protobuf::StringValue{});

    return custom_config;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_{"worker_0"};

  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<ReverseTunnelAcceptorExtension> extension_;

  std::unique_ptr<ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>> tls_slot_;
  std::shared_ptr<UpstreamSocketThreadLocal> thread_local_registry_;

  NiceMock<Event::MockDispatcher> another_dispatcher_{"worker_1"};
  std::shared_ptr<UpstreamSocketThreadLocal> another_thread_local_registry_;

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);
};

TEST_F(ReverseTunnelAcceptorExtensionTest, InitializeWithDefaultStatPrefix) {
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface empty_config;

  auto extension_with_default =
      std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, empty_config);

  EXPECT_EQ(extension_with_default->statPrefix(), "reverse_tunnel_acceptor");
}

TEST_F(ReverseTunnelAcceptorExtensionTest, InitializeWithCustomStatPrefix) {
  EXPECT_EQ(extension_->statPrefix(), "reverse_connections");
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetStatsScope) {
  EXPECT_EQ(&extension_->getStatsScope(), stats_scope_.get());
}

TEST_F(ReverseTunnelAcceptorExtensionTest, OnWorkerThreadInitialized) {
  extension_->onWorkerThreadInitialized();
}

TEST_F(ReverseTunnelAcceptorExtensionTest, OnServerInitializedSetsExtensionReference) {
  extension_->onServerInitialized();
  EXPECT_EQ(socket_interface_->getExtension(), extension_.get());
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetLocalRegistryBeforeInitialization) {
  EXPECT_EQ(extension_->getLocalRegistry(), nullptr);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetLocalRegistryAfterInitialization) {
  setupThreadLocalSlot();

  auto* registry = extension_->getLocalRegistry();
  EXPECT_NE(registry, nullptr);

  auto* socket_manager = registry->socketManager();
  EXPECT_NE(socket_manager, nullptr);
  EXPECT_EQ(socket_manager->getUpstreamExtension(), extension_.get());

  const auto* const_registry = extension_->getLocalRegistry();
  EXPECT_NE(const_registry, nullptr);

  const auto* const_socket_manager = const_registry->socketManager();
  EXPECT_NE(const_socket_manager, nullptr);
  EXPECT_EQ(const_socket_manager->getUpstreamExtension(), extension_.get());
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetPerWorkerStatMapSingleThread) {
  setupThreadLocalSlot();

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node2", "cluster2", true);
  extension_->updateConnectionStats("node2", "cluster2", true);

  auto stat_map = extension_->getPerWorkerStatMap();

  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node1"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node2"], 2);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster1"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster2"], 2);

  for (const auto& [stat_name, value] : stat_map) {
    EXPECT_TRUE(stat_name.find("worker_0") != std::string::npos);
  }

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node1", "cluster1", true);

  stat_map = extension_->getPerWorkerStatMap();

  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node1"], 3);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster1"], 3);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node2"], 2);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster2"], 2);

  extension_->updateConnectionStats("node1", "cluster1", false);
  extension_->updateConnectionStats("node2", "cluster2", false);
  extension_->updateConnectionStats("node2", "cluster2", false);

  stat_map = extension_->getPerWorkerStatMap();

  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node1"], 2);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster1"], 2);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node2"], 0);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster2"], 0);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, UpdateConnectionStatsWithDetailedStatsDisabled) {
  // Create an extension with detailed stats disabled.
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface no_stats_config;
  no_stats_config.set_stat_prefix("reverse_connections");
  no_stats_config.set_enable_detailed_stats(false);

  auto no_stats_extension = std::make_unique<ReverseTunnelAcceptorExtension>(
      *socket_interface_, context_, no_stats_config);

  // Update connection stats - should not create any stats.
  no_stats_extension->updateConnectionStats("node1", "cluster1", true);
  no_stats_extension->updateConnectionStats("node2", "cluster2", true);
  no_stats_extension->updateConnectionStats("node3", "cluster3", true);

  // Verify no stats were created by checking cross-worker stat map.
  auto cross_worker_stat_map = no_stats_extension->getCrossWorkerStatMap();
  EXPECT_TRUE(cross_worker_stat_map.empty());

  // Verify no per-worker stats were created by checking per-worker stat map.
  auto per_worker_stat_map = no_stats_extension->getPerWorkerStatMap();
  EXPECT_TRUE(per_worker_stat_map.empty());

  // Verify that the stats store doesn't have any gauges with our stat prefix
  // (except potentially the aggregate that we removed).
  auto& stats_store = no_stats_extension->getStatsScope();
  bool found_detailed_stats = false;
  Stats::IterateFn<Stats::Gauge> gauge_callback =
      [&found_detailed_stats](const Stats::RefcountPtr<Stats::Gauge>& gauge) -> bool {
    const std::string& gauge_name = gauge->name();
    // Check if any detailed stats were created (nodes. or clusters. or worker_).
    if ((gauge_name.find(".nodes.") != std::string::npos ||
         gauge_name.find(".clusters.") != std::string::npos ||
         gauge_name.find(".worker_") != std::string::npos) &&
        gauge->used()) {
      found_detailed_stats = true;
      return false; // Stop iteration
    }
    return true;
  };
  stats_store.iterate(gauge_callback);
  EXPECT_FALSE(found_detailed_stats);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetCrossWorkerStatMapMultiThread) {
  setupThreadLocalSlot();
  setupAnotherThreadLocalSlot();

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node2", "cluster2", true);

  auto original_registry = thread_local_registry_;
  thread_local_registry_ = another_thread_local_registry_;

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node3", "cluster3", true);

  thread_local_registry_ = original_registry;

  auto stat_map = extension_->getCrossWorkerStatMap();

  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node1"], 3);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node2"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node3"], 1);

  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster1"], 3);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster2"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster3"], 1);

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node2", "cluster2", false);

  stat_map = extension_->getCrossWorkerStatMap();

  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node1"], 4);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster1"], 4);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node2"], 0);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster2"], 0);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node3"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster3"], 1);

  extension_->updateConnectionStats("node1", "cluster1", false);

  auto per_worker_stat_map = extension_->getPerWorkerStatMap();

  EXPECT_EQ(per_worker_stat_map["test_scope.reverse_connections.worker_0.node.node1"], 3);
  EXPECT_EQ(per_worker_stat_map["test_scope.reverse_connections.worker_0.cluster.cluster1"], 3);

  extension_->updateConnectionStats("node2", "cluster2", false);

  auto cross_worker_stat_map = extension_->getCrossWorkerStatMap();

  EXPECT_EQ(cross_worker_stat_map["test_scope.reverse_connections.clusters.cluster2"], 0);

  per_worker_stat_map = extension_->getPerWorkerStatMap();

  EXPECT_EQ(per_worker_stat_map["test_scope.reverse_connections.worker_0.node.node2"], 0);
  EXPECT_EQ(per_worker_stat_map["test_scope.reverse_connections.worker_0.cluster.cluster2"], 0);

  thread_local_registry_ = another_thread_local_registry_;

  extension_->updateConnectionStats("node1", "cluster1", false);
  extension_->updateConnectionStats("node3", "cluster3", false);

  auto worker1_stat_map = extension_->getPerWorkerStatMap();

  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.node.node1"], 0);
  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.cluster.cluster1"], 0);
  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.node.node3"], 0);
  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.cluster.cluster3"], 0);

  thread_local_registry_ = original_registry;
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetConnectionStatsSyncMultiThread) {
  setupThreadLocalSlot();
  setupAnotherThreadLocalSlot();

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node2", "cluster2", true);

  auto original_registry = thread_local_registry_;
  thread_local_registry_ = another_thread_local_registry_;

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node3", "cluster3", true);

  thread_local_registry_ = original_registry;

  auto result = extension_->getConnectionStatsSync();
  auto& [connected_nodes, accepted_connections] = result;

  EXPECT_FALSE(connected_nodes.empty() || accepted_connections.empty());

  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "node1") !=
              connected_nodes.end());
  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "node2") !=
              connected_nodes.end());
  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "node3") !=
              connected_nodes.end());

  EXPECT_TRUE(std::find(accepted_connections.begin(), accepted_connections.end(), "cluster1") !=
              accepted_connections.end());
  EXPECT_TRUE(std::find(accepted_connections.begin(), accepted_connections.end(), "cluster2") !=
              accepted_connections.end());
  EXPECT_TRUE(std::find(accepted_connections.begin(), accepted_connections.end(), "cluster3") !=
              accepted_connections.end());

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node2", "cluster2", false);

  result = extension_->getConnectionStatsSync();
  auto& [updated_connected_nodes, updated_accepted_connections] = result;

  EXPECT_TRUE(std::find(updated_connected_nodes.begin(), updated_connected_nodes.end(), "node2") ==
              updated_connected_nodes.end());
  EXPECT_TRUE(std::find(updated_accepted_connections.begin(), updated_accepted_connections.end(),
                        "cluster2") == updated_accepted_connections.end());

  EXPECT_TRUE(std::find(updated_connected_nodes.begin(), updated_connected_nodes.end(), "node1") !=
              updated_connected_nodes.end());
  EXPECT_TRUE(std::find(updated_connected_nodes.begin(), updated_connected_nodes.end(), "node3") !=
              updated_connected_nodes.end());
  EXPECT_TRUE(std::find(updated_accepted_connections.begin(), updated_accepted_connections.end(),
                        "cluster1") != updated_accepted_connections.end());
  EXPECT_TRUE(std::find(updated_accepted_connections.begin(), updated_accepted_connections.end(),
                        "cluster3") != updated_accepted_connections.end());
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetConnectionStatsSyncTimeout) {
  auto result = extension_->getConnectionStatsSync(std::chrono::milliseconds(1));

  auto& [connected_nodes, accepted_connections] = result;
  EXPECT_TRUE(connected_nodes.empty());
  EXPECT_TRUE(accepted_connections.empty());
}

TEST_F(ReverseTunnelAcceptorExtensionTest, IpFamilySupportIPv4) {
  EXPECT_TRUE(socket_interface_->ipFamilySupported(AF_INET));
}

TEST_F(ReverseTunnelAcceptorExtensionTest, IpFamilySupportIPv6) {
  EXPECT_TRUE(socket_interface_->ipFamilySupported(AF_INET6));
}

TEST_F(ReverseTunnelAcceptorExtensionTest, IpFamilySupportUnknown) {
  EXPECT_FALSE(socket_interface_->ipFamilySupported(AF_UNIX));
  EXPECT_FALSE(socket_interface_->ipFamilySupported(-1));
}

TEST_F(ReverseTunnelAcceptorExtensionTest, ExtensionNotInitialized) {
  ReverseTunnelAcceptor acceptor(context_);
  auto registry = acceptor.getLocalRegistry();
  EXPECT_EQ(registry, nullptr);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, CreateEmptyConfigProto) {
  auto proto = socket_interface_->createEmptyConfigProto();
  EXPECT_NE(proto, nullptr);

  auto* typed_proto =
      dynamic_cast<envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
                       UpstreamReverseConnectionSocketInterface*>(proto.get());
  EXPECT_NE(typed_proto, nullptr);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, MissThresholdOneMarksDeadOnFirstInvalidPing) {
  // Recreate extension_ with threshold = 1.
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface cfg;
  cfg.set_stat_prefix("test_prefix");
  cfg.mutable_ping_failure_threshold()->set_value(1);
  extension_.reset(new ReverseTunnelAcceptorExtension(*socket_interface_, context_, cfg));

  // Provide dispatcher to thread local and set expectations for timers/file events.
  thread_local_.setDispatcher(&dispatcher_);
  EXPECT_CALL(dispatcher_, createTimer_(_))
      .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockTimer>>());
  EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _))
      .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockFileEvent>>());

  // Use helper to install TLS registry for the (recreated) extension_.
  setupThreadLocalSlot();

  // Get the registry and socket manager back through the API and apply threshold.
  auto* registry = extension_->getLocalRegistry();
  ASSERT_NE(registry, nullptr);
  auto* socket_manager = registry->socketManager();
  ASSERT_NE(socket_manager, nullptr);
  socket_manager->setMissThreshold(extension_->pingFailureThreshold());

  // Create a mock socket with FD and addresses.
  auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
  auto io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*io_handle, fdDoNotUse()).WillRepeatedly(testing::Return(123));
  EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(testing::ReturnRef(*io_handle));
  socket->io_handle_ = std::move(io_handle);

  auto local_address = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 10000);
  auto remote_address = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 10001);
  socket->connection_info_provider_->setLocalAddress(local_address);
  socket->connection_info_provider_->setRemoteAddress(remote_address);

  const std::string node_id = "n1";
  const std::string cluster_id = "c1";
  socket_manager->addConnectionSocket(node_id, cluster_id, std::move(socket),
                                      std::chrono::seconds(30), false);

  // Simulate an invalid ping response (not RPING). With threshold=1, one miss should kill it.
  NiceMock<Network::MockIoHandle> mock_read_handle;
  EXPECT_CALL(mock_read_handle, fdDoNotUse()).WillRepeatedly(testing::Return(123));
  EXPECT_CALL(mock_read_handle, read(testing::_, testing::_))
      .WillOnce(testing::Invoke([](Buffer::Instance& buffer, absl::optional<uint64_t>) {
        buffer.add("XXXXX"); // 5 bytes, not RPING
        return Api::IoCallUint64Result{5, Api::IoError::none()};
      }));

  socket_manager->onPingResponse(mock_read_handle);

  // With threshold=1, the socket should be marked dead immediately.
  auto retrieved = socket_manager->getConnectionSocket(node_id);
  EXPECT_EQ(retrieved, nullptr);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, PingFailureThresholdConfiguration) {
  // Test default threshold value
  EXPECT_EQ(extension_->pingFailureThreshold(), 3); // Default threshold should be 3.

  // Create extension with custom threshold = 5
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface custom_config;
  custom_config.set_stat_prefix("test_custom");
  custom_config.mutable_ping_failure_threshold()->set_value(5);

  auto custom_extension =
      std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, custom_config);

  EXPECT_EQ(custom_extension->pingFailureThreshold(), 5);

  // Test threshold = 1 (minimum value)
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface min_config;
  min_config.set_stat_prefix("test_min");
  min_config.mutable_ping_failure_threshold()->set_value(1);

  auto min_extension =
      std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, min_config);

  EXPECT_EQ(min_extension->pingFailureThreshold(), 1);

  // Test very high threshold
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface max_config;
  max_config.set_stat_prefix("test_max");
  max_config.mutable_ping_failure_threshold()->set_value(100);

  auto max_extension =
      std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, max_config);

  EXPECT_EQ(max_extension->pingFailureThreshold(), 100);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, FactoryName) {
  EXPECT_EQ(socket_interface_->name(), "envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
}

TEST_F(ReverseTunnelAcceptorExtensionTest, InitializeWithReporterConfig) {
  auto config = getConfigWithReporter();
  NiceMock<MockReporterFactory> reporter_factory;

  Registry::InjectFactory<ReverseTunnelReporterFactory> reporter_injector(reporter_factory);

  EXPECT_CALL(context_, messageValidationVisitor())
      .WillRepeatedly(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));

  EXPECT_CALL(reporter_factory, createReporter()).WillOnce(Invoke([]() {
    auto reporter = std::make_unique<NiceMock<MockReverseTunnelReporter>>();
    EXPECT_CALL(*reporter, onServerInitialized());
    return reporter;
  }));

  extension_ =
      std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, config);
  extension_->onServerInitialized();
}

TEST_F(ReverseTunnelAcceptorExtensionTest, InvalidReverseTunnelReporter) {
  auto config = getConfigWithReporter();
  NiceMock<MockReporterFactory> reporter_factory;

  EXPECT_THROW(ReverseTunnelAcceptorExtension(*socket_interface_, context_, config),
               EnvoyException);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, ValidateConnectionReporting) {
  auto config = getConfigWithReporter();

  std::string node_id = "node";
  std::string cluster_id = "cluster";
  std::string tenant_id = "tenant";

  NiceMock<MockReporterFactory> reporter_factory;

  Registry::InjectFactory<ReverseTunnelReporterFactory> reporter_injector(reporter_factory);

  EXPECT_CALL(context_, messageValidationVisitor())
      .WillRepeatedly(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));

  EXPECT_CALL(reporter_factory, createReporter())
      .Times(1)
      .WillOnce(Invoke([&node_id, &cluster_id, &tenant_id]() {
        auto reporter = std::make_unique<NiceMock<MockReverseTunnelReporter>>();

        EXPECT_CALL(*reporter, reportConnectionEvent(testing::Eq(node_id), testing::Eq(cluster_id),
                                                     testing::Eq(tenant_id)));

        return reporter;
      }));

  extension_ =
      std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, config);
  extension_->reportConnection(node_id, cluster_id, tenant_id);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, ValidateDisconnectionReporting) {
  auto config = getConfigWithReporter();

  std::string node_id = "node";
  std::string cluster_id = "cluster";

  NiceMock<MockReporterFactory> reporter_factory;

  Registry::InjectFactory<ReverseTunnelReporterFactory> reporter_injector(reporter_factory);

  EXPECT_CALL(context_, messageValidationVisitor())
      .WillRepeatedly(ReturnRef(ProtobufMessage::getStrictValidationVisitor()));

  EXPECT_CALL(reporter_factory, createReporter())
      .Times(1)
      .WillOnce(Invoke([&node_id, &cluster_id]() {
        auto reporter = std::make_unique<NiceMock<MockReverseTunnelReporter>>();

        EXPECT_CALL(*reporter,
                    reportDisconnectionEvent(testing::Eq(node_id), testing::Eq(cluster_id)));

        return reporter;
      }));

  extension_ =
      std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, config);
  extension_->reportDisconnection(node_id, cluster_id);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy

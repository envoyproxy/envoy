#include "source/extensions/upstreams/http/dynamic_modules/conn_pool.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

class DynamicModuleTcpConnPoolTest : public ::testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
        Envoy::Extensions::DynamicModules::testSharedObjectPath("http_tcp_bridge_no_op", "c"),
        false);
    ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto config_result = DynamicModuleHttpTcpBridgeConfig::create(
        "test_bridge", "test_config", std::move(dynamic_module.value()));
    ASSERT_TRUE(config_result.ok()) << config_result.status().message();
    config_ = std::move(config_result.value());

    host_ = std::make_shared<NiceMock<Upstream::MockHost>>();
    cm_.initializeThreadLocalClusters({"fake_cluster"});
    EXPECT_CALL(cm_.thread_local_cluster_, tcpConnPool(_, _))
        .WillOnce(Return(Upstream::TcpPoolData([]() {}, &mock_pool_)));

    conn_pool_ = std::make_unique<DynamicModuleTcpConnPool>(
        config_, cm_.thread_local_cluster_, Upstream::ResourcePriority::Default, nullptr);

    // Set up mock route for callbacks.
    route_ = std::make_shared<NiceMock<Router::MockRoute>>();
    EXPECT_CALL(mock_upstream_to_downstream_, route())
        .Times(AnyNumber())
        .WillRepeatedly(ReturnRef(*route_));
    EXPECT_CALL(mock_callbacks_, upstreamToDownstream())
        .Times(AnyNumber())
        .WillRepeatedly(ReturnRef(mock_upstream_to_downstream_));
  }

  DynamicModuleHttpTcpBridgeConfigSharedPtr config_;
  std::shared_ptr<NiceMock<Upstream::MockHost>> host_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Envoy::Tcp::ConnectionPool::MockInstance> mock_pool_;
  NiceMock<Envoy::ConnectionPool::MockCancellable> cancellable_;
  NiceMock<Router::MockGenericConnectionPoolCallbacks> mock_callbacks_;
  NiceMock<Router::MockUpstreamToDownstream> mock_upstream_to_downstream_;
  std::shared_ptr<NiceMock<Router::MockRoute>> route_;
  std::unique_ptr<DynamicModuleTcpConnPool> conn_pool_;
  NiceMock<Network::MockClientConnection> connection_;
};

TEST_F(DynamicModuleTcpConnPoolTest, Valid) { EXPECT_TRUE(conn_pool_->valid()); }

TEST_F(DynamicModuleTcpConnPoolTest, NewStreamAndOnPoolReady) {
  EXPECT_CALL(mock_pool_, newConnection(_)).WillOnce(Return(&cancellable_));
  conn_pool_->newStream(&mock_callbacks_);

  EXPECT_CALL(mock_callbacks_, onPoolReady(_, _, _, _, _));

  auto data = std::make_unique<NiceMock<Envoy::Tcp::ConnectionPool::MockConnectionData>>();
  EXPECT_CALL(*data, connection()).Times(AnyNumber()).WillRepeatedly(ReturnRef(connection_));
  conn_pool_->onPoolReady(std::move(data), host_);
}

TEST_F(DynamicModuleTcpConnPoolTest, OnPoolFailure) {
  EXPECT_CALL(mock_pool_, newConnection(_)).WillOnce(Return(&cancellable_));
  conn_pool_->newStream(&mock_callbacks_);

  EXPECT_CALL(mock_callbacks_, onPoolFailure(_, "connection_failure", _));
  conn_pool_->onPoolFailure(Envoy::Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                            "connection_failure", host_);

  // After pool failure, canceling should return false.
  EXPECT_FALSE(conn_pool_->cancelAnyPendingStream());
}

TEST_F(DynamicModuleTcpConnPoolTest, Cancel) {
  // Initially cancel should fail as there is no pending request.
  EXPECT_FALSE(conn_pool_->cancelAnyPendingStream());

  EXPECT_CALL(mock_pool_, newConnection(_)).WillOnce(Return(&cancellable_));
  conn_pool_->newStream(&mock_callbacks_);

  // Canceling should now return true as there was an active request.
  EXPECT_TRUE(conn_pool_->cancelAnyPendingStream());

  // A second cancel should return false as there is no pending request.
  EXPECT_FALSE(conn_pool_->cancelAnyPendingStream());
}

TEST_F(DynamicModuleTcpConnPoolTest, Host) { EXPECT_NE(conn_pool_->host(), nullptr); }

class DynamicModuleTcpConnPoolInvalidTest : public ::testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = Envoy::Extensions::DynamicModules::newDynamicModule(
        Envoy::Extensions::DynamicModules::testSharedObjectPath("http_tcp_bridge_no_op", "c"),
        false);
    ASSERT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto config_result = DynamicModuleHttpTcpBridgeConfig::create(
        "test_bridge", "test_config", std::move(dynamic_module.value()));
    ASSERT_TRUE(config_result.ok()) << config_result.status().message();
    config_ = std::move(config_result.value());

    cm_.initializeThreadLocalClusters({"fake_cluster"});
    // Return nullopt to simulate no TCP pool available.
    EXPECT_CALL(cm_.thread_local_cluster_, tcpConnPool(_, _)).WillOnce(Return(absl::nullopt));

    conn_pool_ = std::make_unique<DynamicModuleTcpConnPool>(
        config_, cm_.thread_local_cluster_, Upstream::ResourcePriority::Default, nullptr);
  }

  DynamicModuleHttpTcpBridgeConfigSharedPtr config_;
  NiceMock<Upstream::MockClusterManager> cm_;
  std::unique_ptr<DynamicModuleTcpConnPool> conn_pool_;
};

TEST_F(DynamicModuleTcpConnPoolInvalidTest, Invalid) { EXPECT_FALSE(conn_pool_->valid()); }

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy

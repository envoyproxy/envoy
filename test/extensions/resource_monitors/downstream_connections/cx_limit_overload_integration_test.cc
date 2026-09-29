#include <algorithm>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/network/connection_balancer.h"
#include "envoy/registry/registry.h"

#include "test/integration/integration.h"
#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"

using testing::Eq;
namespace Envoy {

namespace {

// A connection balancer that always hands a connection off to a *different* registered handler
// than the one that accepted it, guaranteeing every connection actually exercises
// ActiveTcpListener::onAcceptWorker()'s post()-based cross-worker rebalance path, rather than
// relying on ExactConnectionBalancerImpl's load-based picking to do so by chance.
class AlwaysRebalanceConnectionBalancerImpl : public Network::ConnectionBalancer {
public:
  void registerHandler(Network::BalancedConnectionHandler& handler) override {
    absl::MutexLock lock(&lock_);
    handlers_.push_back(&handler);
  }

  void unregisterHandler(Network::BalancedConnectionHandler& handler) override {
    absl::MutexLock lock(&lock_);
    handlers_.erase(std::remove(handlers_.begin(), handlers_.end(), &handler), handlers_.end());
  }

  Network::BalancedConnectionHandler&
  pickTargetHandler(Network::BalancedConnectionHandler& current_handler) override {
    absl::MutexLock lock(&lock_);
    for (auto* handler : handlers_) {
      if (handler != &current_handler) {
        handler->preIncNumConnections();
        handler->postIncNumConnections();
        return *handler;
      }
    }
    current_handler.preIncNumConnections();
    current_handler.postIncNumConnections();
    return current_handler;
  }

private:
  absl::Mutex lock_;
  std::vector<Network::BalancedConnectionHandler*> handlers_ ABSL_GUARDED_BY(lock_);
};

class AlwaysRebalanceConnectionBalanceFactory : public Network::ConnectionBalanceFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom empty config proto. This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::Protobuf::Struct()};
  }
  Network::ConnectionBalancerSharedPtr
  createConnectionBalancerFromProto(const Protobuf::Message&,
                                    Server::Configuration::FactoryContext&) override {
    return std::make_shared<AlwaysRebalanceConnectionBalancerImpl>();
  }
  std::string name() const override {
    return "envoy.network.connection_balance.always_rebalance_test";
  }
};

envoy::config::overload::v3::OverloadManager
generateMaxDownstreamConnectionsOverloadConfig(uint32_t max_cx) {
  return TestUtility::parseYaml<envoy::config::overload::v3::OverloadManager>(
      fmt::format(R"EOF(
          resource_monitors:
            - name: "envoy.resource_monitors.global_downstream_max_connections"
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.resource_monitors.downstream_connections.v3.DownstreamConnectionsConfig
                max_active_downstream_connections: {}
        )EOF",
                  std::to_string(max_cx)));
}

class GlobalDownstreamCxLimitIntegrationTest : public testing::Test, public BaseIntegrationTest {
protected:
  GlobalDownstreamCxLimitIntegrationTest()
      : BaseIntegrationTest(Network::Address::IpVersion::v4, ConfigHelper::tcpProxyConfig()) {}

  void initializeOverloadManager(uint32_t max_cx) {
    overload_manager_config_ = generateMaxDownstreamConnectionsOverloadConfig(max_cx);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      *bootstrap.mutable_overload_manager() = this->overload_manager_config_;
    });
    initialize();
  }

  AssertionResult waitForConnections(uint32_t expected_connections) {
    absl::Notification num_downstream_conns_reached;

    test_server_->server().dispatcher().post([this, &num_downstream_conns_reached,
                                              expected_connections]() {
      auto& overload_state = test_server_->server().overloadManager().getThreadLocalOverloadState();
      const auto& monitor = overload_state.getProactiveResourceMonitorForTest(
          Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections);
      ASSERT_TRUE(monitor.has_value());
      test_server_->waitForProactiveOverloadResourceUsageEq(
          Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections,
          expected_connections, test_server_->server().dispatcher(),
          std::chrono::milliseconds(500));
      num_downstream_conns_reached.Notify();
    });
    if (num_downstream_conns_reached.WaitForNotificationWithTimeout(absl::Milliseconds(550))) {
      return AssertionSuccess();
    } else {
      return AssertionFailure();
    }
  }

private:
  envoy::config::overload::v3::OverloadManager overload_manager_config_;
};

TEST_F(GlobalDownstreamCxLimitIntegrationTest, GlobalLimitInOverloadManager) {
  initializeOverloadManager(6);
  std::vector<IntegrationTcpClientPtr> tcp_clients;
  std::vector<FakeRawConnectionPtr> raw_conns;
  for (int i = 0; i < 6; ++i) {
    tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
    FakeRawConnectionPtr conn;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(conn));
    raw_conns.push_back(std::move(conn));
    ASSERT_TRUE(tcp_clients.back()->connected());
  }
  test_server_->waitForCounter("listener.127.0.0.1_0.downstream_global_cx_overflow", Eq(0));
  // 7th connection should fail because we have hit the configured limit for
  // `max_active_downstream_connections`.
  tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
  FakeRawConnectionPtr conn;
  ASSERT_FALSE(fake_upstreams_[0]->waitForRawConnection(conn, std::chrono::milliseconds(500)));
  tcp_clients.back()->waitForDisconnect();
  test_server_->waitForCounter("listener.127.0.0.1_0.downstream_global_cx_overflow", Eq(1));
  // Get rid of the client that failed to connect.
  tcp_clients.back()->close();
  tcp_clients.pop_back();
  // Get rid of the first successfully connected client to be able to establish new connection.
  tcp_clients.front()->close();
  ASSERT_TRUE(raw_conns.front()->waitForDisconnect());
  // test_server_->waitForProactiveOverloadResourceUsageEq(Server::OverloadProactiveResourceName::GlobalDownstreamMaxConnections,
  // 5, test_server_->server().dispatcher(), std::chrono::milliseconds(5000));
  ASSERT_TRUE(waitForConnections(5));
  // As 6th client disconnected, we can again establish a connection.
  tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
  FakeRawConnectionPtr conn1;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(conn1));
  raw_conns.push_back(std::move(conn1));
  ASSERT_TRUE(tcp_clients.back()->connected());
  for (auto& tcp_client : tcp_clients) {
    tcp_client->close();
  }
}

TEST_F(GlobalDownstreamCxLimitIntegrationTest, GlobalLimitSetViaRuntimeKeyAndOverloadManager) {
  // Configure global connections limit via deprecated runtime key.
  config_helper_.addRuntimeOverride("overload.global_downstream_max_connections", "3");
  initializeOverloadManager(2);
  const std::string log_line =
      "Global downstream connections limits is configured via deprecated runtime key "
      "overload.global_downstream_max_connections and in "
      "envoy.resource_monitors.global_downstream_max_connections. Using overload manager "
      "config.";
  std::vector<IntegrationTcpClientPtr> tcp_clients;
  std::vector<FakeRawConnectionPtr> raw_conns;
  std::function<void()> establish_conns = [this, &tcp_clients, &raw_conns]() {
    for (int i = 0; i < 2; ++i) {
      tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
      FakeRawConnectionPtr conn;
      ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(conn));
      raw_conns.push_back(std::move(conn));
      ASSERT_TRUE(tcp_clients.back()->connected());
    }
  };
  EXPECT_LOG_CONTAINS_N_TIMES("warn", log_line, 1, { establish_conns(); });
  // Third connection should fail because we have hit the configured limit in overload manager.
  tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
  FakeRawConnectionPtr conn1;
  ASSERT_FALSE(fake_upstreams_[0]->waitForRawConnection(conn1, std::chrono::milliseconds(500)));
  tcp_clients.back()->waitForDisconnect();
  test_server_->waitForCounter("listener.127.0.0.1_0.downstream_global_cx_overflow", Eq(1));
  for (auto& tcp_client : tcp_clients) {
    tcp_client->close();
  }
}

TEST_F(GlobalDownstreamCxLimitIntegrationTest, GlobalLimitOptOutRespected) {
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
    listener->set_ignore_global_conn_limit(true);
  });
  initializeOverloadManager(2);
  std::vector<IntegrationTcpClientPtr> tcp_clients;
  std::vector<FakeRawConnectionPtr> raw_conns;
  // All clients succeed to connect due to global conn limit opt out set.
  for (int i = 0; i <= 5; ++i) {
    tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
    FakeRawConnectionPtr conn;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(conn));
    raw_conns.push_back(std::move(conn));
    ASSERT_TRUE(tcp_clients.back()->connected());
  }
  test_server_->waitForCounter("listener.127.0.0.1_0.downstream_global_cx_overflow", Eq(0));
  for (auto& tcp_client : tcp_clients) {
    tcp_client->close();
  }
}

TEST_F(GlobalDownstreamCxLimitIntegrationTest, PerListenerLimitAndGlobalLimitInOverloadManager) {
  config_helper_.addRuntimeOverride("envoy.resource_limits.listener.listener_0.connection_limit",
                                    "2");
  initializeOverloadManager(5);
  std::vector<IntegrationTcpClientPtr> tcp_clients;
  std::vector<FakeRawConnectionPtr> raw_conns;
  for (int i = 0; i < 2; ++i) {
    tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
    FakeRawConnectionPtr conn;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(conn));
    raw_conns.push_back(std::move(conn));
    ASSERT_TRUE(tcp_clients.back()->connected());
  }
  // Third connection should fail because we have hit the configured limit per listener.
  tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
  FakeRawConnectionPtr conn1;
  ASSERT_FALSE(fake_upstreams_[0]->waitForRawConnection(conn1, std::chrono::milliseconds(500)));
  tcp_clients.back()->waitForDisconnect();
  test_server_->waitForCounter("listener.127.0.0.1_0.downstream_cx_overflow", Eq(1));
  test_server_->waitForCounter("listener.127.0.0.1_0.downstream_global_cx_overflow", Eq(0));
  for (auto& tcp_client : tcp_clients) {
    tcp_client->close();
  }
}

// Regression test for a cross-thread use-after-free: with a real (not mocked) OverloadManager
// and multiple worker threads, force every connection to be handed off to a different worker
// than the one that accepted it (mirroring connection_balance_config.exact_balance), and verify
// that closing those connections -- which runs AcceptedSocketImpl::~AcceptedSocketImpl()'s
// tryDeallocateResource() against the real ThreadLocalOverloadStateImpl -- completes cleanly.
// ThreadLocalOverloadStateImpl asserts dispatcher_.isThreadSafe() on every access, so this test
// would fail immediately (by aborting the test server) if the rebind in
// ActiveTcpListener::onAcceptWorker() ever regressed and let a stale, cross-worker handle reach
// this code path again.
TEST_F(GlobalDownstreamCxLimitIntegrationTest, RebalancedConnectionUsesRealThreadLocalOverloadState) {
  concurrency_ = 2;

  AlwaysRebalanceConnectionBalanceFactory factory;
  Registry::InjectFactory<Envoy::Network::ConnectionBalanceFactory> registered(factory);

  auto overload_manager_config = generateMaxDownstreamConnectionsOverloadConfig(100);
  config_helper_.addConfigModifier(
      [overload_manager_config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
        *bootstrap.mutable_overload_manager() = overload_manager_config;
        auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
        auto* connection_balance_config = listener->mutable_connection_balance_config();
        auto* extend_balance_config = connection_balance_config->mutable_extend_balance();
        extend_balance_config->set_name("envoy.network.connection_balance.always_rebalance_test");
        extend_balance_config->mutable_typed_config()->set_type_url(
            "type.googleapis.com/google.protobuf.Struct");
      });
  initialize();

  std::vector<IntegrationTcpClientPtr> tcp_clients;
  std::vector<FakeRawConnectionPtr> raw_conns;
  for (int i = 0; i < 4; ++i) {
    tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
    FakeRawConnectionPtr conn;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(conn));
    raw_conns.push_back(std::move(conn));
    ASSERT_TRUE(tcp_clients.back()->connected());
  }
  ASSERT_TRUE(waitForConnections(4));

  for (auto& tcp_client : tcp_clients) {
    tcp_client->close();
  }
  ASSERT_TRUE(waitForConnections(0));
}

} // namespace
} // namespace Envoy

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/network/filter.h"
#include "envoy/registry/registry.h"

#include "common/network/utility.h"

#include "test/config/utility.h"
#include "test/integration/integration.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class ConnectionLimitIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                       public Event::TestUsingSimulatedTime,
                                       public BaseIntegrationTest {
public:
  ConnectionLimitIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}

  void setEmptyListenerLimit() {
    config_helper_.addRuntimeOverride("envoy.resource_limits.listener.listener_0.connection_limit",
                                      "");
  }

  void setListenerLimit(const uint32_t num_conns) {
    config_helper_.addRuntimeOverride("envoy.resource_limits.listener.listener_0.connection_limit",
                                      std::to_string(num_conns));
  }

  void setGlobalLimit(std::string&& num_conns) {
    config_helper_.addRuntimeOverride("overload.global_downstream_max_connections", num_conns);
  }

  void initialize() override { BaseIntegrationTest::initialize(); }

  AssertionResult waitForConnections(uint32_t envoy_downstream_connections) {
    // The multiplier of 2 is because both Envoy's downstream connections and
    // the test server's downstream connections are counted by the global
    // counter.
    uint32_t expected_connections = envoy_downstream_connections * 2;

    for (int i = 0; i < 10; ++i) {
      if (Network::AcceptedSocketImpl::acceptedSocketCount() == expected_connections) {
        return AssertionSuccess();
      }
      timeSystem().advanceTimeWait(std::chrono::milliseconds(500));
    }
    if (Network::AcceptedSocketImpl::acceptedSocketCount() == expected_connections) {
      return AssertionSuccess();
    }
    return AssertionFailure();
  }

  // Assumes a limit of 2 connections.
  void doTest(std::function<void()> init_func, std::string&& check_stat) {
    init_func();

    std::vector<IntegrationTcpClientPtr> tcp_clients;
    std::vector<FakeRawConnectionPtr> raw_conns;
    tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
    raw_conns.emplace_back();
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(raw_conns.back()));
    ASSERT_TRUE(tcp_clients.back()->connected());

    tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
    raw_conns.emplace_back();
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(raw_conns.back()));
    ASSERT_TRUE(tcp_clients.back()->connected());

    tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
    raw_conns.emplace_back();
    ASSERT_FALSE(
        fake_upstreams_[0]->waitForRawConnection(raw_conns.back(), std::chrono::milliseconds(500)));
    tcp_clients.back()->waitForDisconnect();

    // Get rid of the client that failed to connect.
    tcp_clients.back()->close();
    tcp_clients.pop_back();

    // Close the first connection that was successful so that we can open a new successful
    // connection.
    tcp_clients.front()->close();
    ASSERT_TRUE(raw_conns.front()->waitForDisconnect());

    // Make sure to not try to connect again until the acceptedSocketCount is updated.
    ASSERT_TRUE(waitForConnections(1));
    tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
    raw_conns.emplace_back();
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(raw_conns.back()));
    ASSERT_TRUE(tcp_clients.back()->connected());

    const bool isV4 = (version_ == Network::Address::IpVersion::v4);
    auto local_address = isV4 ? Network::Utility::getCanonicalIpv4LoopbackAddress()
                              : Network::Utility::getIpv6LoopbackAddress();

    const std::string counter_prefix = (isV4 ? "listener.127.0.0.1_0." : "listener.[__1]_0.");

    test_server_->waitForCounterEq(counter_prefix + check_stat, 1);

    for (auto& tcp_client : tcp_clients) {
      tcp_client->close();
    }

    tcp_clients.clear();
    raw_conns.clear();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ConnectionLimitIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ConnectionLimitIntegrationTest, TestListenerLimit) {
  std::function<void()> init_func = [this]() {
    setListenerLimit(2);
    initialize();
  };

  doTest(init_func, "downstream_cx_overflow");
}

TEST_P(ConnectionLimitIntegrationTest, TestEmptyGlobalCxRuntimeLimit) {
  const std::string log_line = "no configured limit to the number of allowed active connections.";
  EXPECT_LOG_CONTAINS("warn", log_line, { initialize(); });
}

TEST_P(ConnectionLimitIntegrationTest, TestEmptyListenerRuntimeLimit) {
  const std::string log_line =
      "Listener connection limit runtime key "
      "envoy.resource_limits.listener.listener_0.connection_limit is empty. There are currently "
      "no limitations on the number of accepted connections for listener listener_0.";
  EXPECT_LOG_CONTAINS("warn", log_line, {
    setEmptyListenerLimit();
    initialize();
  });
}

TEST_P(ConnectionLimitIntegrationTest, TestGlobalLimit) {
  std::function<void()> init_func = [this]() {
    // Includes twice the number of connections expected because the tracking is performed via a
    // static variable and the fake upstream has a listener. This causes upstream connections to the
    // fake upstream to also be tracked as part of the global downstream connection tracking.
    setGlobalLimit("4");
    initialize();
  };

  doTest(init_func, "downstream_global_cx_overflow");
}

TEST_P(ConnectionLimitIntegrationTest, TestBothLimits) {
  std::function<void()> init_func = [this]() {
    // Setting the listener limit to a much higher value and making sure the right stat gets
    // incremented when both limits are set.
    setGlobalLimit("4");
    setListenerLimit(100);
    initialize();
  };

  doTest(init_func, "downstream_global_cx_overflow");
}

} // namespace
} // namespace Envoy

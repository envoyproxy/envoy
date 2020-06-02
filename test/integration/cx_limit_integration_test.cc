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

  void initialize() override { BaseIntegrationTest::initialize(); }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ConnectionLimitIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ConnectionLimitIntegrationTest, TestListenerLimit) {
  setListenerLimit(2);
  initialize();

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
  ASSERT_FALSE(fake_upstreams_[0]->waitForRawConnection(raw_conns.back()));
  tcp_clients.back()->waitForDisconnect();

  // Get rid of the client that failed to connect.
  tcp_clients.back()->close();
  tcp_clients.pop_back();

  // Close the first connection that was successful so that we can open a new successful connection.
  tcp_clients.front()->close();
  ASSERT_TRUE(raw_conns.front()->close());
  ASSERT_TRUE(raw_conns.front()->waitForDisconnect());

  tcp_clients.emplace_back(makeTcpConnection(lookupPort("listener_0")));
  raw_conns.emplace_back();
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(raw_conns.back()));
  ASSERT_TRUE(tcp_clients.back()->connected());

  const bool isV4 = (version_ == Network::Address::IpVersion::v4);
  auto local_address = isV4 ? Network::Utility::getCanonicalIpv4LoopbackAddress()
                            : Network::Utility::getIpv6LoopbackAddress();

  const std::string counter_name = isV4 ? ("listener.127.0.0.1_0.downstream_cx_overflow")
                                        : ("listener.[__1]_0.downstream_cx_overflow");

  test_server_->waitForCounterEq(counter_name, 1);

  for (auto& tcp_client : tcp_clients) {
    tcp_client->close();
  }

  tcp_clients.clear();
  raw_conns.clear();
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

} // namespace
} // namespace Envoy

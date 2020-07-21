#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_switch_resp.h"

#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mysql_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

constexpr int SESSIONS = 5;

class MySQLIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                             public MySQLTestUtils,
                             public BaseIntegrationTest {
  std::string mysqlConfig() {
    return fmt::format(TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
                           "test/extensions/filters/network/mysql_proxy/mysql_test_config.yaml")),
                       Network::Test::getLoopbackAddressString(GetParam()),
                       Network::Test::getLoopbackAddressString(GetParam()),
                       Network::Test::getAnyAddressString(GetParam()));
  }

public:
  MySQLIntegrationTest() : BaseIntegrationTest(GetParam(), mysqlConfig()){};

  void SetUp() override { BaseIntegrationTest::initialize(); }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MySQLIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

/**
 * NewSession Test:
 * Attempt a New Session and verify it is received by mysql onNewConnection.
 */
TEST_P(MySQLIntegrationTest, MySQLStatsNewSessionTest) {
  for (int idx = 0; idx < SESSIONS; idx++) {
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

    tcp_client->close();
    ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  }

  test_server_->waitForCounterGe("mysql.mysql_stats.sessions", SESSIONS);
}

/**
 * Login Test:
 * Attempt a mysql login and verify it is processed by the filter:
 * Verify counters:
 * - correct number of attempts
 * - no failures
 */
TEST_P(MySQLIntegrationTest, MySQLLoginTest) {
  std::string str;
  std::string rcvd_data;
  std::string user = "user1";

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // greeting
  std::string greeting = encodeServerGreeting(MYSQL_PROTOCOL_10);
  ASSERT_TRUE(fake_upstream_connection->write(greeting));

  str.append(greeting);
  tcp_client->waitForData(str, true);

  // Client username/password and capabilities
  std::string login = encodeClientLogin(MYSQL_CLIENT_CAPAB_41VS320, user, CHALLENGE_SEQ_NUM);
  ASSERT_TRUE(tcp_client->write(login));
  ASSERT_TRUE(fake_upstream_connection->waitForData(login.length(), &rcvd_data));
  EXPECT_EQ(login, rcvd_data);

  // Server response OK to username/password
  std::string loginok = encodeClientLoginResp(MYSQL_RESP_OK);
  ASSERT_TRUE(fake_upstream_connection->write(loginok));

  str.append(loginok);
  tcp_client->waitForData(str, true);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  test_server_->waitForCounterGe("mysql.mysql_stats.login_attempts", 1);
  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.login_failures")->value(), 0);
}

/**
 * Multiple Connections Login Test:
 * Attempt a mysql login and verify it is processed by the filter:
 * Verify counters:
 * - correct number of attempts
 * - no failures
 */
TEST_P(MySQLIntegrationTest, MySQLUnitTestMultiClientsLoop) {
  int idx;
  std::string rcvd_data;

  for (idx = 0; idx < CLIENT_NUM; idx++) {
    std::string str;
    std::string user("user");
    user.append(std::to_string(idx));

    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

    // greeting
    std::string greeting = encodeServerGreeting(MYSQL_PROTOCOL_10);
    ASSERT_TRUE(fake_upstream_connection->write(greeting));

    str.append(greeting);
    tcp_client->waitForData(str, true);

    // Client username/password and capabilities
    std::string login = encodeClientLogin(MYSQL_CLIENT_CAPAB_41VS320, user, CHALLENGE_SEQ_NUM);
    ASSERT_TRUE(tcp_client->write(login));
    ASSERT_TRUE(fake_upstream_connection->waitForData(login.length(), &rcvd_data));
    EXPECT_EQ(login, rcvd_data);

    // Server response OK to username/password
    std::string loginok = encodeClientLoginResp(MYSQL_RESP_OK);
    ASSERT_TRUE(fake_upstream_connection->write(loginok));

    str.append(loginok);
    tcp_client->waitForData(str, true);

    tcp_client->close();
    ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  }

  // Verify counters: CLIENT_NUM login attempts, no failures
  test_server_->waitForCounterGe("mysql.mysql_stats.login_attempts", CLIENT_NUM);
  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.login_attempts")->value(), CLIENT_NUM);
  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.login_failures")->value(), 0);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

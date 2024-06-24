#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin_resp.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_greeting.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_switch_resp.h"
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
    return fmt::format(
        fmt::runtime(TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
            "contrib/mysql_proxy/filters/network/test/mysql_test_config.yaml"))),
        Platform::null_device_path, Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getAnyAddressString(GetParam()));
  }

public:
  MySQLIntegrationTest() : BaseIntegrationTest(GetParam(), mysqlConfig()) {
    skip_tag_extraction_rule_check_ = true;
  };

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
  std::string login = encodeClientLogin(CLIENT_PROTOCOL_41, user, CHALLENGE_SEQ_NUM);
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
// TODO(https://github.com/envoyproxy/envoy/issues/30852) enable
TEST_P(MySQLIntegrationTest, DISABLED_MySQLUnitTestMultiClientsLoop) {
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
    std::string login = encodeClientLogin(CLIENT_PROTOCOL_41, user, CHALLENGE_SEQ_NUM);
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

/**
 * Login Test:
 * Attempt a mysql login and verify it is processed by the filter:
 * Verify counters:
 * - correct number of attempts
 * - correct number of failures
 */
TEST_P(MySQLIntegrationTest, MySQLLoginFailTest) {
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
  std::string login = encodeClientLogin(CLIENT_PROTOCOL_41, user, CHALLENGE_SEQ_NUM);
  ASSERT_TRUE(tcp_client->write(login));
  ASSERT_TRUE(fake_upstream_connection->waitForData(login.length(), &rcvd_data));
  EXPECT_EQ(login, rcvd_data);

  // Server response Error to username/password
  std::string loginerr = encodeClientLoginResp(MYSQL_RESP_ERR);
  ASSERT_TRUE(fake_upstream_connection->write(loginerr));

  str.append(loginerr);
  tcp_client->waitForData(str, true);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  test_server_->waitForCounterGe("mysql.mysql_stats.login_attempts", 1);
  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.login_failures")->value(), 1);
}

/**
 * Login Test:
 * Attempt a mysql login and verify it is processed by the filter:
 * Verify counters:
 * - correct number of attempts
 * - correct number of upgraded_to_ssl
 */
TEST_P(MySQLIntegrationTest, MySQLLoginSslTest) {
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

  // Client ssl upgrade request
  std::string login = encodeClientLogin(CLIENT_SSL, user, CHALLENGE_SEQ_NUM);
  ASSERT_TRUE(tcp_client->write(login));
  ASSERT_TRUE(fake_upstream_connection->waitForData(login.length(), &rcvd_data));
  EXPECT_EQ(login, rcvd_data);

  // after ssl upgrade request, the decoder will stop parse.

  // Server response Error to username/password
  std::string loginerr = encodeClientLoginResp(MYSQL_RESP_ERR);
  ASSERT_TRUE(fake_upstream_connection->write(loginerr));

  str.append(loginerr);
  tcp_client->waitForData(str, true);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.login_attempts")->value(), 1);
  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.upgraded_to_ssl")->value(), 1);
  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.login_failures")->value(), 0);
}

/**
 * Login Test:
 * Attempt a mysql login and verify it is processed by the filter:
 * Verify counters:
 * - correct number of attempts
 * - correct number of failures
 * - correct number of auth switch requests
 */
TEST_P(MySQLIntegrationTest, MySQLLoginAuthSwitchTest) {
  std::string downstream_buffer;
  std::string upstream_buffer;
  std::string rcvd_data;
  std::string user = "user1";

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  // greeting
  std::string greeting = encodeServerGreeting(MYSQL_PROTOCOL_10);
  ASSERT_TRUE(fake_upstream_connection->write(greeting));

  downstream_buffer.append(greeting);
  tcp_client->waitForData(downstream_buffer, true);

  // Client username/password and capabilities
  std::string login = encodeClientLogin(CLIENT_PROTOCOL_41, user, CHALLENGE_SEQ_NUM);
  upstream_buffer.append(login);
  ASSERT_TRUE(tcp_client->write(login));
  ASSERT_TRUE(fake_upstream_connection->waitForData(login.length(), &rcvd_data));
  EXPECT_EQ(upstream_buffer, rcvd_data);

  // Server response Auth Switch
  std::string auth_switch = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  ASSERT_TRUE(fake_upstream_connection->write(auth_switch));

  downstream_buffer.append(auth_switch);
  tcp_client->waitForData(downstream_buffer, true);

  // Client auth switch resp
  std::string auth_switch_resp = encodeAuthSwitchResp();
  upstream_buffer.append(auth_switch_resp);
  ASSERT_TRUE(tcp_client->write(auth_switch_resp));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(upstream_buffer.length(), &data));
  EXPECT_EQ(upstream_buffer, data);

  // Server response OK to username/password
  std::string loginok = encodeClientLoginResp(MYSQL_RESP_OK);
  ASSERT_TRUE(fake_upstream_connection->write(loginok));

  downstream_buffer.append(loginok);
  tcp_client->waitForData(downstream_buffer, true);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.login_attempts")->value(), 1);
  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.login_failures")->value(), 0);
  EXPECT_EQ(test_server_->counter("mysql.mysql_stats.auth_switch_request")->value(), 1);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

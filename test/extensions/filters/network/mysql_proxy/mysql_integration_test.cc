#include <pthread.h>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/network_utility.h"
#include "mysql_test_utils.h"

using namespace rapidjson;

namespace Envoy {
  namespace Extensions {
namespace NetworkFilters {
namespace MysqlProxy {

#define NEW_SESSIONS 5

class MysqlIntegrationTest : public MysqlTestUtils,
                             public BaseIntegrationTest,
                             public testing::TestWithParam<Network::Address::IpVersion> {
  std::string mysqlConfig() {
    return TestEnvironment::readFileToStringForTest(
        TestEnvironment::runfilesPath("mysql_test_config.yaml"));
  }

public:
  MysqlIntegrationTest() : BaseIntegrationTest(GetParam(), mysqlConfig()) {}
  /**
   * Initializer for an individual integration test.
   */
  void SetUp() override { BaseIntegrationTest::initialize(); }

  /**
   * Destructor for an individual integration test.
   */
  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }
};

int mysqlGetCounterValueFromStats(std::string msg, std::string mysql_stat, int& counter) {
  Json::ObjectSharedPtr stats = Json::Factory::loadFromString(msg);
  for (const Json::ObjectSharedPtr& stat : stats->getObjectArray("stats")) {
    std::string entry = stat->getString("name");
    if (!entry.compare(mysql_stat)) {
      counter = stat->getInteger("value");
      return 0;
    }
  }
  return -1;
}

INSTANTIATE_TEST_CASE_P(IpVersions, MysqlIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

/* NewSession Test:
 * Attempt a New Session and verify it is received by
 * mysql onNewConnection.
 * Verify counters
 */
TEST_P(MysqlIntegrationTest, MysqlStatsNewSessionTest) {
  for (int idx = 0; idx < NEW_SESSIONS; idx++) {
    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

    tcp_client->close();
    ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  }

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/stats?format=json", "", Http::CodecClient::Type::HTTP1,
      Envoy::Network::Address::IpVersion::v4);

  int ret = 0;
  int counter = 0;
  std::string mysql_stat = "mysql.mysql_stats.new_sessions";
  ret = mysqlGetCounterValueFromStats(response->body(), mysql_stat, counter);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(counter, NEW_SESSIONS);
}

/* Login Test:
 * Attempt a mysql login and verify it is processed by the filter:
 * Verify counters:
 * - correct number of attempts
 * - no failures
 */
TEST_P(MysqlIntegrationTest, MysqLoginTest) {
  std::string str = "";
  std::string rcvd_data = "";
  std::string user = "user1";

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  /* greeting */
  std::string greeting = EncodeServerGreeting(MYSQL_PROTOCOL_10);
  ASSERT_TRUE(fake_upstream_connection->write(greeting));
  tcp_client->waitForData(str);

  /* Client username/password and capabilities */
  std::string login = EncodeClientLogin(MYSQL_CLIENT_CAPAB_41VS320, user);
  tcp_client->write(login);
  ASSERT_TRUE(fake_upstream_connection->waitForData(login.length(), &rcvd_data));
  EXPECT_EQ(login, rcvd_data);

  /* Server response OK to username/password */
  std::string loginok = EncodeClientLoginResp(MYSQL_RESP_OK);
  ASSERT_TRUE(fake_upstream_connection->write(loginok));
  tcp_client->waitForData(str);

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  /* Verify counters */
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/stats?format=json", "", Http::CodecClient::Type::HTTP1,
      Envoy::Network::Address::IpVersion::v4);
  int ret = 0;
  int counter = 0;
  std::string mysql_stat = "mysql.mysql_stats.login_attempts";
  ret = mysqlGetCounterValueFromStats(response->body(), mysql_stat, counter);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(counter, 1);
  mysql_stat = "mysql.mysql_stats.login_failures";
  ret = mysqlGetCounterValueFromStats(response->body(), mysql_stat, counter);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(counter, 0);
}

/* Multiple Connections Login Test:
 * Attempt a mysql login and verify it is processed by the filter:
 * Verify counters:
 * - correct number of attempts
 * - no failures
 */
TEST_P(MysqlIntegrationTest, MysqlUnitTestMultiClientsLoop) {
  int idx;
  std::string rcvd_data = "";
  std::string str = "";

  for (idx = 0; idx < CLIENT_NUM; idx++) {
    std::string user("user");
    user.append(std::to_string(idx));

    IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
    FakeRawConnectionPtr fake_upstream_connection;
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

    /* greeting */
    std::string greeting = EncodeServerGreeting(MYSQL_PROTOCOL_10);
    ASSERT_TRUE(fake_upstream_connection->write(greeting));
    tcp_client->waitForData(str);

    /* Client username/password and capabilities */
    std::string login = EncodeClientLogin(MYSQL_CLIENT_CAPAB_41VS320, user);
    tcp_client->write(login);
    ASSERT_TRUE(fake_upstream_connection->waitForData(login.length(), &rcvd_data));
    EXPECT_EQ(login, rcvd_data);

    /* Server response OK to username/password */
    std::string loginok = EncodeClientLoginResp(MYSQL_RESP_OK);
    ASSERT_TRUE(fake_upstream_connection->write(loginok));
    tcp_client->waitForData(str);

    tcp_client->close();
    ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  }

  /* Verify counters: CLIENT_NUM login attempts, no failures */
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "GET", "/stats?format=json", "", Http::CodecClient::Type::HTTP1,
      Envoy::Network::Address::IpVersion::v4);
  int ret = 0;
  int counter = 0;
  std::string mysql_stat = "mysql.mysql_stats.login_attempts";
  ret = mysqlGetCounterValueFromStats(response->body(), mysql_stat, counter);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(counter, CLIENT_NUM);
  mysql_stat = "mysql.mysql_stats.login_failures";
  ret = mysqlGetCounterValueFromStats(response->body(), mysql_stat, counter);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(counter, 0);
}

} // namespace MysqlProxy
} // namespace NetworkFilters
    } // namespace Extensions
} // namespace Envoy

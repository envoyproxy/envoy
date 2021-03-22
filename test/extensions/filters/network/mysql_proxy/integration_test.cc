#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/message_helper.h"
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
class MySQLIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                             public MySQLTestUtils,
                             public BaseIntegrationTest {
  std::string mysqlConfig() {
    return fmt::format(
        TestEnvironment::readFileToStringForTest(TestEnvironment::runfilesPath(
            "test/extensions/filters/network/mysql_proxy/mysql_test_config_auth.yaml")),
        Platform::null_device_path, Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getLoopbackAddressString(GetParam()),
        Network::Test::getAnyAddressString(GetParam()));
  }

public:
  void proxyResponseStep(const std::string& request, const std::string& proxy_response,
                         IntegrationTcpClientPtr& mysql_client, bool check_data = false) {
    mysql_client->clearData();
    ASSERT_TRUE(mysql_client->write(request));
    if (check_data) {
      mysql_client->waitForData(proxy_response);
      EXPECT_EQ(proxy_response, mysql_client->data());
    } else {
      ASSERT(mysql_client->waitForData(proxy_response.length()));
    }
  }

  void downstreamAuth(const std::string& username, const std::string& password,
                      const std::string& db, IntegrationTcpClientPtr& mysql_client) {
    // greet
    auto greet = MessageHelper::encodeGreeting(MySQLTestUtils::getAuthPluginData20());
    auto response = MessageHelper::encodePacket(greet, 0).toString();
    ASSERT(mysql_client->waitForData(response.size())); // do not know seed, just judge length
    Buffer::OwnedImpl buffer(mysql_client->data().data() + 4, mysql_client->data().size() - 4);
    greet.decode(buffer, 0, mysql_client->data().size() - 4);

    auto seed = greet.getAuthPluginData();
    auto auth_method = AuthHelper::authMethod(greet.getBaseServerCap(), greet.getExtServerCap(),
                                              greet.getAuthPluginName());

    auto client_message =
        MessageHelper::encodeClientLogin(auth_method, username, password, db, seed);
    auto request = MessageHelper::encodePacket(client_message, 1).toString();
    auto ok_message = MessageHelper::encodeOk();
    response = MessageHelper::encodePacket(ok_message, 2).toString();

    proxyResponseStep(request, response, mysql_client, true);
  }

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
  IntegrationTcpClientPtr mysql_client = makeTcpConnection(lookupPort("listener_0"));

  auto greet = MessageHelper::encodeGreeting(MySQLTestUtils::getAuthPluginData20());
  auto response = MessageHelper::encodePacket(greet, 0).toString();
  test_server_->waitForCounterGe("mysql.mysql.sessions", 1, std::chrono::milliseconds(100));

  ASSERT(mysql_client->waitForData(response.size()));
  // do not know seed, just judge length
  EXPECT_EQ(response.size(), mysql_client->data().size());
  // EXPECT_EQ(test_server_->counter("mysql.mysql.sessions")->value(), 1);

  mysql_client->close();
}

/**
 * NewSession Test:
 * Attempt login using wrong password.
 */
TEST_P(MySQLIntegrationTest, MySQLWrongLoginTest) {
  std::string str;
  std::string rcvd_data;
  std::string db = "db";
  std::string username = "username";
  std::string wrongpassword = "wrong_password";

  IntegrationTcpClientPtr mysql_client = makeTcpConnection(lookupPort("listener_0"));

  auto greet = MessageHelper::encodeGreeting(MySQLTestUtils::getAuthPluginData20());
  auto response = MessageHelper::encodePacket(greet, 0).toString();
  ASSERT(mysql_client->waitForData(response.size())); // do not know seed, just judge length
  Buffer::OwnedImpl buffer(mysql_client->data().data() + 4, mysql_client->data().size() - 4);
  greet.decode(buffer, 0, mysql_client->data().size() - 4);
  auto seed = greet.getAuthPluginData();
  auto auth_method = AuthHelper::authMethod(greet.getBaseServerCap(), greet.getExtServerCap(),
                                            greet.getAuthPluginName());

  auto client_message =
      MessageHelper::encodeClientLogin(auth_method, username, wrongpassword, db, seed);
  auto request = MessageHelper::encodePacket(client_message, 1).toString();
  auto err_message =
      MessageHelper::authError(username, Network::Test::getLoopbackAddressString(GetParam()), true);
  response = MessageHelper::encodePacket(err_message, 2).toString();

  proxyResponseStep(request, response, mysql_client);
  mysql_client->close();
  test_server_->waitForCounterGe("mysql.mysql.login_attempts", 1, std::chrono::milliseconds(100));
}

/**
 * NewSession Test:
 * Attempt login using right password.
 */
TEST_P(MySQLIntegrationTest, MySQLRightLoginTest) {
  std::string str;
  std::string rcvd_data;
  std::string db = "db";
  std::string username = "username";
  std::string password = "password";

  IntegrationTcpClientPtr mysql_client = makeTcpConnection(lookupPort("listener_0"));
  downstreamAuth(username, password, db, mysql_client);

  mysql_client->close();
  test_server_->waitForCounterGe("mysql.mysql.login_attempts", 1);
  // EXPECT_EQ(test_server_->counter("mysql.mysql.login_failures")->value(), 1);
}

/**
 * NewSession Test:
 * Attempt login using right password. Failed at upstream auth.
 */
TEST_P(MySQLIntegrationTest, MySQLFilterTest) {
  std::string str;
  std::string rcvd_data;
  std::string db = "db";
  std::string username = "username";
  std::string password = "password";

  IntegrationTcpClientPtr mysql_client = makeTcpConnection(lookupPort("listener_0"));

  downstreamAuth(username, password, db, mysql_client);

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  mysql_client->close();
  test_server_->waitForCounterGe("mysql.mysql.login_attempts", 1);

  // EXPECT_EQ(test_server_->counter("mysql.mysql.login_failures")->value(), 1);
}
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy

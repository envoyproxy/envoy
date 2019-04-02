#include "test/integration/redis_proxy_integration_test.h"

#include <sstream>
#include <vector>

#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"

namespace RedisCommon = Envoy::Extensions::NetworkFilters::Common::Redis;
namespace RedisCmdSplitter = Envoy::Extensions::NetworkFilters::RedisProxy::CommandSplitter;

namespace Envoy {
namespace {

std::string makeBulkStringArray(std::vector<std::string>&& command_strings) {
  std::stringstream result;

  result << "*" << command_strings.size() << "\r\n";
  for (uint64_t i = 0; i < command_strings.size(); i++) {
    result << "$" << command_strings[i].size() << "\r\n";
    result << command_strings[i] << "\r\n";
  }

  return result.str();
}

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

void RedisProxyIntegrationTest::initialize() {
  config_helper_.renameListener("redis_proxy");
  BaseIntegrationTest::initialize();
}

TEST_P(RedisProxyIntegrationTest, SimpleRequestAndResponse) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("redis_proxy"));

  std::string client_to_proxy = makeBulkStringArray({"get", "foo"});
  std::string proxy_to_server;

  ASSERT_TRUE(client_to_proxy.size() > 0);
  ASSERT_TRUE(client_to_proxy.find("get") != std::string::npos);
  ASSERT_TRUE(client_to_proxy.find("foo") != std::string::npos);
  tcp_client->write(client_to_proxy);

  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(client_to_proxy.size(), &proxy_to_server));
  ASSERT_EQ(client_to_proxy, proxy_to_server);

  std::string server_to_proxy = "$3\r\nbar\r\n"; // bulkstring reply of "bar"

  ASSERT_TRUE(fake_upstream_connection->write(server_to_proxy));
  tcp_client->waitForData(server_to_proxy);
  ASSERT_EQ(server_to_proxy, tcp_client->data());

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->close());
}

TEST_P(RedisProxyIntegrationTest, InvalidRequest) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("redis_proxy"));

  std::string client_to_proxy = makeBulkStringArray({"foo"});

  ASSERT_TRUE(client_to_proxy.size() > 0);
  ASSERT_TRUE(client_to_proxy.find("foo") != std::string::npos);
  tcp_client->write(client_to_proxy);

  std::stringstream error_response;
  error_response << "-" << RedisCmdSplitter::Response::get().InvalidRequest << "\r\n";
  std::string proxy_to_client = error_response.str();

  tcp_client->waitForData(proxy_to_client);
  ASSERT_EQ(proxy_to_client, tcp_client->data());

  tcp_client->close();
}

} // namespace
} // namespace Envoy

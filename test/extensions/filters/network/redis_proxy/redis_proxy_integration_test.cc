#include <sstream>
#include <vector>

#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"

#include "test/integration/integration.h"

#include "gtest/gtest.h"

namespace RedisCmdSplitter = Envoy::Extensions::NetworkFilters::RedisProxy::CommandSplitter;

namespace Envoy {
namespace {

const std::string REDIS_PROXY_CONFIG = R"EOF(
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    name: cluster_0
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
  listeners:
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    filter_chains:
      filters:
        name: envoy.redis_proxy
        config:
          stat_prefix: redis_stats
          cluster: cluster_0
          settings: 
            op_timeout: 5s
)EOF";

std::string makeBulkStringArray(std::vector<std::string>&& command_strings) {
  std::stringstream result;

  result << "*" << command_strings.size() << "\r\n";
  for (uint64_t i = 0; i < command_strings.size(); i++) {
    result << "$" << command_strings[i].size() << "\r\n";
    result << command_strings[i] << "\r\n";
  }

  return result.str();
}

class RedisProxyIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public BaseIntegrationTest {
public:
  RedisProxyIntegrationTest() : BaseIntegrationTest(GetParam(), REDIS_PROXY_CONFIG) {}

  ~RedisProxyIntegrationTest() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void initialize() override;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

void RedisProxyIntegrationTest::initialize() {
  config_helper_.renameListener("redis_proxy");
  BaseIntegrationTest::initialize();
}

// This test sends a simple "get foo" command from a fake
// downstream client through the proxy to a fake upstream
// Redis server. The fake server sends a valid response
// back to the client. The request and response should
// make it through the envoy proxy server code unchanged.

TEST_P(RedisProxyIntegrationTest, SimpleRequestAndResponse) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("redis_proxy"));

  std::string client_to_proxy = makeBulkStringArray({"get", "foo"});
  std::string proxy_to_server;

  EXPECT_TRUE(client_to_proxy.size() > 0);
  EXPECT_TRUE(client_to_proxy.find("get") != std::string::npos);
  EXPECT_TRUE(client_to_proxy.find("foo") != std::string::npos);
  tcp_client->write(client_to_proxy);

  FakeRawConnectionPtr fake_upstream_connection;
  EXPECT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  EXPECT_TRUE(fake_upstream_connection->waitForData(client_to_proxy.size(), &proxy_to_server));
  EXPECT_EQ(client_to_proxy, proxy_to_server);

  std::string server_to_proxy = "$3\r\nbar\r\n"; // bulkstring reply of "bar"

  EXPECT_TRUE(fake_upstream_connection->write(server_to_proxy));
  tcp_client->waitForData(server_to_proxy);
  EXPECT_EQ(server_to_proxy, tcp_client->data());

  tcp_client->close();
  EXPECT_TRUE(fake_upstream_connection->close());
}

// This test sends an invalid Redis command from a fake
// downstream client to the envoy proxy. Envoy will respond
// with an invalid request error.

TEST_P(RedisProxyIntegrationTest, InvalidRequest) {
  initialize();
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("redis_proxy"));

  std::string client_to_proxy = makeBulkStringArray({"foo"});

  EXPECT_TRUE(client_to_proxy.size() > 0);
  EXPECT_TRUE(client_to_proxy.find("foo") != std::string::npos);
  tcp_client->write(client_to_proxy);

  std::stringstream error_response;
  error_response << "-" << RedisCmdSplitter::Response::get().InvalidRequest << "\r\n";
  std::string proxy_to_client = error_response.str();

  tcp_client->waitForData(proxy_to_client);
  EXPECT_EQ(proxy_to_client, tcp_client->data());

  tcp_client->close();
}

} // namespace
} // namespace Envoy

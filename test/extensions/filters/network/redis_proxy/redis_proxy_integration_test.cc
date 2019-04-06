#include <sstream>
#include <vector>

#include "extensions/filters/network/redis_proxy/command_splitter_impl.h"

#include "test/integration/integration.h"

#include "gtest/gtest.h"

using testing::Return;

namespace RedisCmdSplitter = Envoy::Extensions::NetworkFilters::RedisProxy::CommandSplitter;

namespace Envoy {
namespace {

// This is a basic redis_proxy configuration with 2 endpoints/hosts
// in the cluster. The load balancing policy must be set
// to random for proper test operation.

const std::string CONFIG = R"EOF(
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    - name: cluster_0
      type: STATIC
      lb_policy: RANDOM
      load_assignment:
        cluster_name: cluster_0
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 127.0.0.1
                    port_value: 0
            - endpoint: 
                address:
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

// This is a configuration with moved/ask redirection support enabled.
const std::string CONFIG_WITH_REDIRECTION = CONFIG + R"EOF(
            enable_redirection: true
)EOF";

// This function encodes commands as an array of bulkstrings as transmitted by Redis clients to
// Redis servers, according to the Redis protocol.
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
  RedisProxyIntegrationTest(const std::string& config = CONFIG, int num_upstreams = 2)
      : BaseIntegrationTest(GetParam(), config), num_upstreams_(num_upstreams),
        version_(GetParam()) {}

  ~RedisProxyIntegrationTest() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  // This method encodes a fake upstream's IP address and TCP port in the
  // same format as one would expect from a Redis server in
  // an ask/moved redirection error.

  std::string redisAddressAndPort(FakeUpstreamPtr& upstream) {
    std::stringstream result;
    if (version_ == Network::Address::IpVersion::v4) {
      result << "127.0.0.1"
             << ":";
    } else {
      result << "::1"
             << ":";
    }
    result << upstream->localAddress()->ip()->port();
    return result.str();
  }

  void initialize() override;

  /**
   * Simple bi-directional test between a fake Redis client and Redis server.
   * @param request supplies Redis client data to transmit to the Redis server.
   * @param response supplies Redis server data to transmit to the client.
   */
  void simpleRequestAndResponse(const std::string& request, const std::string& response);
  /**
   * Simple bi-directional test between a fake Redis client and proxy server.
   * @param request supplies Redis client data to transmit to the proxy.
   * @param proxy_response supplies proxy data in response to the client's request.
   */
  void simpleProxyResponse(const std::string& request, const std::string& proxy_response);

protected:
  Runtime::MockRandomGenerator* mock_rng_{};
  const int num_upstreams_;
  const Network::Address::IpVersion version_;
};

class RedisProxyWithRedirectionIntegrationTest : public RedisProxyIntegrationTest {
public:
  RedisProxyWithRedirectionIntegrationTest()
      : RedisProxyIntegrationTest(CONFIG_WITH_REDIRECTION, 2) {}

  /**
   * Simple bi-directional test with a fake Redis client and 2 fake Redis servers.
   * @param target_server a handle to the second server that will respond to the request.
   * @param request supplies client data to transmit to the first upstream server.
   * @param redirection_response supplies the moved or ask redirection error from the first server.
   * @param received_request suplies data received by the second server from the proxy.
   * @param response supplies data sent by the second server back to the fake Redis client.
   */
  void simpleRedirection(FakeUpstreamPtr& target_server, const std::string& request,
                         const std::string& redirection_response,
                         const std::string& received_request, const std::string& response);
};

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_SUITE_P(IpVersions, RedisProxyWithRedirectionIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

void RedisProxyIntegrationTest::initialize() {
  setUpstreamCount(num_upstreams_);
  setDeterministic();
  config_helper_.renameListener("redis_proxy");
  BaseIntegrationTest::initialize();

  mock_rng_ = dynamic_cast<Runtime::MockRandomGenerator*>(&test_server_->server().random());
  // Abort now if we cannot downcast the server's random number generator pointer.
  ASSERT_TRUE(mock_rng_ != nullptr);
  // Ensure that fake_upstreams_[0] is the load balancer's host of choice by default.
  ON_CALL(*mock_rng_, random()).WillByDefault(Return(0));
}

void RedisProxyIntegrationTest::simpleRequestAndResponse(const std::string& request,
                                                         const std::string& response) {
  std::string proxy_to_server;
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  redis_client->write(request);

  FakeRawConnectionPtr fake_upstream_connection;
  EXPECT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  EXPECT_TRUE(fake_upstream_connection->waitForData(request.size(), &proxy_to_server));
  // The original request should be the same as the data received by the server.
  EXPECT_EQ(request, proxy_to_server);

  EXPECT_TRUE(fake_upstream_connection->write(response));
  redis_client->waitForData(response);
  // The original response should be received by the fake Redis client.
  EXPECT_EQ(response, redis_client->data());

  redis_client->close();
  EXPECT_TRUE(fake_upstream_connection->close());
}

void RedisProxyIntegrationTest::simpleProxyResponse(const std::string& request,
                                                    const std::string& proxy_response) {
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  redis_client->write(request);
  redis_client->waitForData(proxy_response);
  // After sending the request to the proxy, the fake redis client should receive proxy_response.
  EXPECT_EQ(proxy_response, redis_client->data());
  redis_client->close();
}

void RedisProxyWithRedirectionIntegrationTest::simpleRedirection(
    FakeUpstreamPtr& target_server, const std::string& request,
    const std::string& redirection_response, const std::string& received_request,
    const std::string& response) {
  std::string proxy_to_server;
  IntegrationTcpClientPtr redis_client = makeTcpConnection(lookupPort("redis_proxy"));
  redis_client->write(request);

  FakeRawConnectionPtr fake_upstream_connection_1, fake_upstream_connection_2;

  // Data from the client should always be routed to fake_upstreams_[0] by the load balancer.
  EXPECT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection_1));
  EXPECT_TRUE(fake_upstream_connection_1->waitForData(request.size(), &proxy_to_server));
  // The data in request should be received by the first server, fake_upstreams_[0].
  EXPECT_EQ(request, proxy_to_server);
  proxy_to_server.clear();

  // Send the redirection_response from the first fake Redis server back to the proxy.
  EXPECT_TRUE(fake_upstream_connection_1->write(redirection_response));
  // The proxy should initiate a new connection to the fake redis server, target_server, in
  // response.
  EXPECT_TRUE(target_server->waitForRawConnection(fake_upstream_connection_2));
  // The server, target_server, should receive received_request which may or may not be the same as
  // the original request.
  EXPECT_TRUE(fake_upstream_connection_2->waitForData(received_request.size(), &proxy_to_server));
  EXPECT_EQ(received_request, proxy_to_server);

  // Send response from the second fake Redis server, target_server, to the client.
  EXPECT_TRUE(fake_upstream_connection_2->write(response));
  redis_client->waitForData(response);
  // The client should receive response unchanged.
  EXPECT_EQ(response, redis_client->data());

  redis_client->close();
  EXPECT_TRUE(fake_upstream_connection_1->close());
  EXPECT_TRUE(fake_upstream_connection_2->close());
}

// This test sends a simple "get foo" command from a fake
// downstream client through the proxy to a fake upstream
// Redis server. The fake server sends a valid response
// back to the client. The request and response should
// make it through the envoy proxy server code unchanged.

TEST_P(RedisProxyIntegrationTest, SimpleRequestAndResponse) {
  initialize();
  simpleRequestAndResponse(makeBulkStringArray({"get", "foo"}), "$3\r\nbar\r\n");
}

// This test sends an invalid Redis command from a fake
// downstream client to the envoy proxy. Envoy will respond
// with an invalid request error.

TEST_P(RedisProxyIntegrationTest, InvalidRequest) {
  std::stringstream error_response;
  error_response << "-" << RedisCmdSplitter::Response::get().InvalidRequest << "\r\n";
  initialize();
  simpleProxyResponse(makeBulkStringArray({"foo"}), error_response.str());
}

// This test sends a simple Redis command to a fake upstream
// Redis server. The server replies with a MOVED or ASK redirection
// error, and that error is passed unchanged to the fake downstream
// since redirection support has not been enabled (by default).

TEST_P(RedisProxyIntegrationTest, RedirectWhenNotEnabled) {
  std::string request = makeBulkStringArray({"get", "foo"});
  initialize();
  if (version_ == Network::Address::IpVersion::v4) {
    simpleRequestAndResponse(request, "-MOVED 1111 127.0.0.1:34123\r\n");
    simpleRequestAndResponse(request, "-ASK 1111 127.0.0.1:34123\r\n");
  } else {
    simpleRequestAndResponse(request, "-MOVED 1111 ::1:34123\r\n");
    simpleRequestAndResponse(request, "-ASK 1111 ::1:34123\r\n");
  }
}

// This test sends a simple Redis command to a sequence of fake upstream
// Redis servers. The first server replies with a MOVED or ASK redirection
// error that specifies the second upstream server in the static configuration
// as its target. The target server responds to a possibly transformed
// request, and its response is received unchanged by the fake Redis client.

TEST_P(RedisProxyWithRedirectionIntegrationTest, RedirectToKnownServer) {
  std::string request = makeBulkStringArray({"get", "foo"});
  initialize();
  std::stringstream redirection_error;
  redirection_error << "-MOVED 1111 " << redisAddressAndPort(fake_upstreams_[1]) << "\r\n";
  simpleRedirection(fake_upstreams_[1], request, redirection_error.str(), request, "$3\r\nbar\r\n");

  redirection_error.str("");
  redirection_error << "-ASK 1111 " << redisAddressAndPort(fake_upstreams_[1]) << "\r\n";
  simpleRedirection(fake_upstreams_[1], request, redirection_error.str(),
                    makeBulkStringArray({"asking", "get", "foo"}), "$3\r\nbar\r\n");
}

// This test sends a simple Redis commands to a sequence of fake upstream
// Redis servers. The first server replies with a MOVED or ASK redirection
// error that specifies an unknown upstream server not in its static configuration
// as its target. The target server responds to a possibly transformed
// request, and its response is received unchanged by the fake Redis client.

TEST_P(RedisProxyWithRedirectionIntegrationTest, RedirectToUnknownServer) {
  std::string request = makeBulkStringArray({"get", "foo"});
  initialize();

  auto endpoint =
      Network::Utility::parseInternetAddress(Network::Test::getAnyAddressString(version_), 0);
  FakeUpstreamPtr target_server{
      new FakeUpstream(endpoint, upstreamProtocol(), timeSystem(), enable_half_close_)};

  std::stringstream redirection_error;
  redirection_error << "-MOVED 1111 " << redisAddressAndPort(target_server) << "\r\n";
  simpleRedirection(target_server, request, redirection_error.str(), request, "$3\r\nbar\r\n");

  redirection_error.str("");
  redirection_error << "-ASK 1111 " << redisAddressAndPort(target_server) << "\r\n";
  simpleRedirection(target_server, request, redirection_error.str(),
                    makeBulkStringArray({"asking", "get", "foo"}), "$3\r\nbar\r\n");
}

// This test verifies that various forms of bad MOVED/ASK redirection errors
// from a fake Redis server are not acted upon, and are passed unchanged
// to the fake Redis client.

TEST_P(RedisProxyWithRedirectionIntegrationTest, BadRedirectStrings) {
  initialize();
  std::string request = makeBulkStringArray({"get", "foo"});

  // Test with truncated moved errors.
  simpleRequestAndResponse(request, "-MOVED 1111\r\n");
  simpleRequestAndResponse(request, "-MOVED\r\n");
  // Test with truncated ask errors.
  simpleRequestAndResponse(request, "-ASK 1111\r\n");
  simpleRequestAndResponse(request, "-ASK\r\n");
  // Test with a badly specified IP address and TCP port field.
  simpleRequestAndResponse(request, "-MOVED 2222 badfield\r\n");
  simpleRequestAndResponse(request, "-ASK 2222 badfield\r\n");
  // Test with a bad IP address specification.
  if (version_ == Network::Address::IpVersion::v4) {
    simpleRequestAndResponse(request, "-MOVED 2222 127.0:3333\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 127.0:3333\r\n");
  } else {
    simpleRequestAndResponse(request, "-MOVED 2222 ::11111:3333\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 ::11111:3333\r\n");
  }
  // Test with a bad IP address specification (not numeric).
  if (version_ == Network::Address::IpVersion::v4) {
    simpleRequestAndResponse(request, "-MOVED 2222 badaddress:3333\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 badaddress:3333\r\n");
  } else {
    simpleRequestAndResponse(request, "-MOVED 2222 badaddress:3333\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 badaddress:3333\r\n");
  }
  // Test with a bad TCP port specification (out of range).
  if (version_ == Network::Address::IpVersion::v4) {
    simpleRequestAndResponse(request, "-MOVED 2222 127.0.0.1:100000\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 127.0.0.1:100000\r\n");
  } else {
    simpleRequestAndResponse(request, "-MOVED 2222 ::1:1000000\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 ::1:1000000\r\n");
  }
  // Test with a bad TCP port specification (not numeric).
  if (version_ == Network::Address::IpVersion::v4) {
    simpleRequestAndResponse(request, "-MOVED 2222 127.0.0.1:badport\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 127.0.0.1:badport\r\n");
  } else {
    simpleRequestAndResponse(request, "-MOVED 2222 ::1:badport\r\n");
    simpleRequestAndResponse(request, "-ASK 2222 ::1:badport\r\n");
  }
}

} // namespace
} // namespace Envoy

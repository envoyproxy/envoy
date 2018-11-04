#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/server/utility.h"

namespace Envoy {

static std::string find_and_replace_config;

class FindAndReplaceIntegrationTest : public BaseIntegrationTest,
                                      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  FindAndReplaceIntegrationTest() : BaseIntegrationTest(GetParam(), find_and_replace_config) {
    config_helper_.renameListener("find_and_replace");
  }

  // Called once by the gtest framework before any FindAndReplaceIntegrationTest are run.
  static void SetUpTestCase() {
    find_and_replace_config = ConfigHelper::BASE_CONFIG + R"EOF(
    filter_chains:
    - filters:
      - name: envoy.filters.network.find_and_replace
        config:
          input_rewrite_from: "PUT /test HTTP/1.1"
          input_rewrite_to: "GET /test HTTP/1.1\r\nConnection: Upgrade\r\nUpgrade: Websocket"
          output_rewrite_from: "HTTP/1.1 101 Switching Protocols\r\nconnection: upgrade\r\nupgrade: websocket"
          output_rewrite_to: "HTTP/1.1 200 OK"
      - name: envoy.http_connection_manager
        config:
          stat_prefix: config_test
          upgrade_configs:
            - upgrade_type: websocket
          http_filters:
            name: envoy.router
          codec_type: HTTP1
          route_config:
            virtual_hosts:
              name: integration
              routes:
                route:
                  cluster: cluster_0
                match:
                  prefix: "/"
              domains: "*"
            name: route_config_0
      )EOF";
  }

  void SetUp() override { BaseIntegrationTest::initialize(); }
};

INSTANTIATE_TEST_CASE_P(IpVersions, FindAndReplaceIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(FindAndReplaceIntegrationTest, Basic) {
  IntegrationTcpClientPtr client = makeTcpConnection(lookupPort("find_and_replace"));

  // TODO: add some payload to the initial request
  // This doesn't work yet.
  std::string request = "PUT /test HTTP/1.1\r\n"
                        "Host: some.example.com\r\n"
                        "SomeHeader: somevalue\r\n\r\n";
  client->write(request);
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));

  std::string upstream_data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &upstream_data));

  // Don't match the entire response because various other headers are inserted (such
  // as a trace-id). Just make sure the things we expect are present.
  EXPECT_TRUE(StringUtil::caseFindToken(upstream_data, "\r\n", "GET /test HTTP/1.1"));
  EXPECT_TRUE(StringUtil::caseFindToken(upstream_data, "\r\n", "Connection: Upgrade"));
  EXPECT_TRUE(StringUtil::caseFindToken(upstream_data, "\r\n", "Upgrade: Websocket"));
  EXPECT_TRUE(StringUtil::caseFindToken(upstream_data, "\r\n", "SomeHeader: somevalue"));

  std::string response = "This isn't even an http response. Send back whatever the upstream likes.";
  ASSERT_TRUE(fake_upstream_connection->write(response));
  client->waitForData(response);
  client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

} // namespace Envoy

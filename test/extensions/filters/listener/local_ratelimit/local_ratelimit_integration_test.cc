#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/listener/local_ratelimit/v3/local_ratelimit.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/extensions/filters/listener/local_ratelimit/local_ratelimit.h"

#include "test/integration/base_integration_test.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/server.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class LocalRateLimitTcpIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                         public BaseIntegrationTest {
public:
  LocalRateLimitTcpIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}

  void setup(const std::string& yaml) {
    ::envoy::extensions::filters::listener::local_ratelimit::v3::LocalRateLimit local_ratelimit;
    TestUtility::loadFromYaml(yaml, local_ratelimit);
    config_helper_.addConfigModifier([local_ratelimit = std::move(local_ratelimit)](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* ppv_filter = listener->add_listener_filters();
      ppv_filter->set_name("local_ratelimit");
      ppv_filter->mutable_typed_config()->PackFrom(local_ratelimit);
    });
    config_helper_.renameListener("tcp_proxy");
    initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, LocalRateLimitTcpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(LocalRateLimitTcpIntegrationTest, NoRateLimiting) {
  setup(R"EOF(
stat_prefix: local_rate_limit_stats
token_bucket:
  max_tokens: 1
  fill_interval: 0.2s
  )EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("hello", false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  EXPECT_EQ(0, test_server_->counter("listener_local_ratelimit.local_rate_limit_stats.rate_limited")
                   ->value());
}

TEST_P(LocalRateLimitTcpIntegrationTest, RateLimited) {
  setup(R"EOF(
stat_prefix: local_rate_limit_stats
token_bucket:
  max_tokens: 1
  # Set fill_interval to effectively infinite so we only get max_tokens to start and never re-fill.
  fill_interval: 100000s
  )EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(tcp_client->write("hello", false));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->close());
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  tcp_client->waitForDisconnect();

  test_server_->waitForCounterGe("listener_local_ratelimit.local_rate_limit_stats.rate_limited", 1);
}

} // namespace Envoy

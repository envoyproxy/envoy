#include "test/integration/integration.h"

namespace Envoy {
namespace {

class LocalRateLimitIntegrationTest : public Event::TestUsingSimulatedTime,
                                      public testing::TestWithParam<Network::Address::IpVersion>,
                                      public BaseIntegrationTest {
public:
  LocalRateLimitIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}

  void setup(const std::string& filter_yaml = {}) {
    if (!filter_yaml.empty()) {
      config_helper_.addNetworkFilter(filter_yaml);
    }
    BaseIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, LocalRateLimitIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Make sure the filter works in the basic case.
TEST_P(LocalRateLimitIntegrationTest, NoRateLimiting) {
  setup(R"EOF(
name: ratelimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.local_ratelimit.v3.LocalRateLimit
  stat_prefix: local_rate_limit_stats
  token_bucket:
    max_tokens: 1
    fill_interval: 0.2s
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  EXPECT_EQ(0,
            test_server_->counter("local_rate_limit.local_rate_limit_stats.rate_limited")->value());
}

TEST_P(LocalRateLimitIntegrationTest, RateLimited) {
  setup(R"EOF(
name: ratelimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.local_ratelimit.v3.LocalRateLimit
  stat_prefix: local_rate_limit_stats
  token_bucket:
    max_tokens: 1
    # Set fill_interval to effectively infinite so we only get max_tokens to start and never re-fill.
    fill_interval: 1000s
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->waitForDisconnect();

  EXPECT_EQ(1,
            test_server_->counter("local_rate_limit.local_rate_limit_stats.rate_limited")->value());
}

TEST_P(LocalRateLimitIntegrationTest, SharedTokenBucket) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    config_helper_.addNetworkFilter(R"EOF(
name: ratelimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.local_ratelimit.v3.LocalRateLimit
  stat_prefix: local_rate_limit_stats
  share_key: 'the_key'
  token_bucket:
    max_tokens: 2
    # Set fill_interval to effectively infinite so we only get max_tokens to start and never re-fill.
    fill_interval: 1000s
)EOF");

    // Clone the whole listener, which includes the `share_key`.
    auto static_resources = bootstrap.mutable_static_resources();
    auto* old_listener = static_resources->mutable_listeners(0);
    auto* cloned_listener = static_resources->add_listeners();
    cloned_listener->CopyFrom(*old_listener);
    cloned_listener->set_name("listener_1");
  });

  setup();

  // One connection on each listener will exhaust the token bucket, which has 2 tokens.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  tcp_client = makeTcpConnection(lookupPort("listener_1"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  // Now both listeners will reject connections due to the shared token bucket being empty.
  tcp_client = makeTcpConnection(lookupPort("listener_0"));
  tcp_client->waitForDisconnect();
  EXPECT_EQ(1,
            test_server_->counter("local_rate_limit.local_rate_limit_stats.rate_limited")->value());

  tcp_client = makeTcpConnection(lookupPort("listener_1"));
  tcp_client->waitForDisconnect();
  EXPECT_EQ(2,
            test_server_->counter("local_rate_limit.local_rate_limit_stats.rate_limited")->value());
}

// TODO(mattklein123): Create an integration test that tests rate limiting. Right now this is
// not easily possible using simulated time due to the fact that simulated time runs alarms on
// their correct threads when woken up, but does not have any barrier for when the alarms have
// actually fired. This makes a deterministic test impossible without resorting to hacks like
// storing the number of tokens in a stat, etc.

} // namespace
} // namespace Envoy

#include "test/integration/integration.h"

namespace Envoy {
namespace {

class LocalRateLimitIntegrationTest : public Event::TestUsingSimulatedTime,
                                      public testing::TestWithParam<Network::Address::IpVersion>,
                                      public BaseIntegrationTest {
public:
  LocalRateLimitIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::TCP_PROXY_CONFIG) {}

  ~LocalRateLimitIntegrationTest() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }

  void setup(const std::string& filter_yaml) {
    config_helper_.addNetworkFilter(filter_yaml);
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
  "@type": type.googleapis.com/envoy.config.filter.network.local_rate_limit.v2alpha.LocalRateLimit
  stat_prefix: local_rate_limit_stats
  token_bucket:
    max_tokens: 1
    fill_interval: 0.2s
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  tcp_client->write("hello");
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect(true));

  EXPECT_EQ(0,
            test_server_->counter("local_rate_limit.local_rate_limit_stats.rate_limited")->value());
}

// TODO(mattklein123): Create an integration test that tests rate limiting. Right now this is
// not easily possible using simulated time due to the fact that simulated time runs alarms on
// their correct threads when woken up, but does not have any barrier for when the alarms have
// actually fired. This makes a deterministic test impossible without resorting to hacks like
// storing the number of tokens in a stat, etc.

} // namespace
} // namespace Envoy

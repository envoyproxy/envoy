#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/listener/local_ratelimit/v3/local_ratelimit.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/thread_local_store.h"
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
      std::ignore = ppv_filter->mutable_typed_config()->PackFrom(local_ratelimit);
    });
    config_helper_.renameListener("tcp_proxy");
    initialize();
  }

  // Whether the stats store derives tags with the explicit-tags logic rather than the legacy
  // tag-extraction rules. Both modes must produce identical stat names, tag-extracted names and
  // tags, so the two IP versions run the server in different modes.
  bool explicitTags() const { return version_ == Network::Address::IpVersion::v6; }

  void initialize() override {
    // The mode is read during server initialization, before the runtime loader exists, so it has
    // to be set directly rather than with addRuntimeOverride().
    Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.enable_stats_explicit_tags",
                                  explicitTags());
    BaseIntegrationTest::initialize();
    auto* store = dynamic_cast<Stats::ThreadLocalStoreImpl*>(&test_server_->statStore());
    ASSERT_NE(store, nullptr);
    EXPECT_EQ(store->useExplicitTags(), explicitTags());
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

  test_server_->waitForCounter("listener_local_ratelimit.local_rate_limit_stats.rate_limited",
                               testing::Ge(1));
}

// The stat prefix is an explicit tag of the stats in both stats modes.
TEST_P(LocalRateLimitTcpIntegrationTest, StatsAreTagged) {
  setup(R"EOF(
stat_prefix: local_rate_limit_stats
token_bucket:
  max_tokens: 1
  fill_interval: 0.2s
  )EOF");

  const auto rate_limited =
      test_server_->counter("listener_local_ratelimit.local_rate_limit_stats.rate_limited");
  ASSERT_NE(rate_limited.get(), nullptr);
  EXPECT_EQ("listener_local_ratelimit.rate_limited", rate_limited->tagExtractedName());
  EXPECT_THAT(rate_limited->tags(),
              testing::UnorderedElementsAreArray(Stats::TagVector{
                  {"envoy.local_listener_ratelimit_prefix", "local_rate_limit_stats"}}));
}

} // namespace Envoy

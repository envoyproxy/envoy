#include "source/common/config/well_known_names.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/thread_local_store.h"

#include "test/integration/integration.h"
#include "test/test_common/simulated_time_system.h"

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
    initialize();
  }

  // Whether the stats store derives tags with the explicit-tags logic (the tag-friendly scope API)
  // rather than with the legacy tag-extraction rules. Both modes must produce identical stat names,
  // tag-extracted names and tags for the filter stats.
  bool explicitTags() const { return version_ == Network::Address::IpVersion::v6; }

  void initialize() override {
    // Exercise both stats modes without doubling the test matrix: the two IP versions run the
    // server in different modes. The mode is read during server initialization, before the runtime
    // loader exists, so it has to be set directly rather than with addRuntimeOverride().
    Runtime::maybeSetRuntimeGuard("envoy.reloadable_features.enable_stats_explicit_tags",
                                  explicitTags());
    BaseIntegrationTest::initialize();

    // Sanity check that the parameterized mode really took effect; otherwise both IP versions
    // would silently be exercising the same thing.
    auto* store = dynamic_cast<Stats::ThreadLocalStoreImpl*>(&test_server_->statStore());
    ASSERT_NE(store, nullptr);
    EXPECT_EQ(store->useExplicitTags(), explicitTags());
  }

  // Checks a stat's flat name, the name it is tag-extracted to, and the tags attached to it.
  void expectStat(const std::string& name, const std::string& tag_extracted_name,
                  const std::vector<std::pair<std::string, std::string>>& tags) {
    Stats::CounterSharedPtr counter = test_server_->counter(name);
    Stats::GaugeSharedPtr gauge = test_server_->gauge(name);
    const Stats::Metric* metric = counter != nullptr ? static_cast<Stats::Metric*>(counter.get())
                                                     : static_cast<Stats::Metric*>(gauge.get());
    ASSERT_NE(metric, nullptr) << "no counter or gauge named '" << name << "'";

    EXPECT_EQ(metric->tagExtractedName(), tag_extracted_name) << " for stat '" << name << "'";

    std::vector<std::pair<std::string, std::string>> actual_tags;
    for (const Stats::Tag& tag : metric->tags()) {
      actual_tags.emplace_back(tag.name_, tag.value_);
    }
    std::sort(actual_tags.begin(), actual_tags.end());
    std::vector<std::pair<std::string, std::string>> expected_tags = tags;
    std::sort(expected_tags.begin(), expected_tags.end());
    EXPECT_EQ(actual_tags, expected_tags) << " for stat '" << name << "'";
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

// The filter's stats have the same flat names, tag-extracted names and tags whether the tags are
// derived by the legacy tag-extraction rules (IPv4) or supplied explicitly (IPv6).
TEST_P(LocalRateLimitIntegrationTest, StatsTagsAndNamesAreIdenticalInBothModes) {
  setup(R"EOF(
name: ratelimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.local_ratelimit.v3.LocalRateLimit
  stat_prefix: local_rate_limit_stats
  token_bucket:
    max_tokens: 1
    fill_interval: 0.2s
)EOF");

  expectStat("local_rate_limit.local_rate_limit_stats.rate_limited",
             "local_rate_limit.rate_limited",
             {{Config::TagNames::get().LOCAL_NETWORK_RATELIMIT_PREFIX, "local_rate_limit_stats"}});
}

} // namespace
} // namespace Envoy

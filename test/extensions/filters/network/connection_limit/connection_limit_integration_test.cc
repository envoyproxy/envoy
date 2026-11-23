#include "source/common/config/well_known_names.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/thread_local_store.h"

#include "test/integration/integration.h"
#include "test/test_common/simulated_time_system.h"

using testing::Eq;
namespace Envoy {
namespace {

class ConnectionLimitIntegrationTest : public Event::TestUsingSimulatedTime,
                                       public testing::TestWithParam<Network::Address::IpVersion>,
                                       public BaseIntegrationTest {
public:
  ConnectionLimitIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::tcpProxyConfig()) {}

  void setup(const std::string& filter_yaml) {
    config_helper_.addNetworkFilter(filter_yaml);
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

INSTANTIATE_TEST_SUITE_P(IpVersions, ConnectionLimitIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Make sure the filter works in the basic case.
TEST_P(ConnectionLimitIntegrationTest, NoConnectionLimiting) {
  setup(R"EOF(
name: connectionlimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.connection_limit.v3.ConnectionLimit
  stat_prefix: connection_limit_stats
  max_connections: 1
  delay: 0.2s
)EOF");

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("listener_0"));
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(fake_upstream_connection->waitForData(5));
  ASSERT_TRUE(fake_upstream_connection->write("world"));
  tcp_client->waitForData("world");

  EXPECT_EQ(
      1,
      test_server_->gauge("connection_limit.connection_limit_stats.active_connections")->value());

  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  test_server_->waitForGauge("connection_limit.connection_limit_stats.active_connections", Eq(0),
                             std::chrono::milliseconds(100));

  EXPECT_EQ(0, test_server_->counter("connection_limit.connection_limit_stats.limited_connections")
                   ->value());
}

// Make sure the filter works in the connection limit case.
TEST_P(ConnectionLimitIntegrationTest, ConnectionLimiting) {
  setup(R"EOF(
name: connectionlimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.connection_limit.v3.ConnectionLimit
  stat_prefix: connection_limit_stats
  max_connections: 1
  delay: 0.2s
)EOF");

  IntegrationTcpClientPtr tcp_client1 = makeTcpConnection(lookupPort("listener_0"));
  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("listener_0"));

  FakeRawConnectionPtr fake_upstream_connection1;
  FakeRawConnectionPtr fake_upstream_connection2;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection1) ||
              fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection2));

  test_server_->waitForGauge("connection_limit.connection_limit_stats.active_connections", Eq(1),
                             std::chrono::milliseconds(200));

  tcp_client1->close();
  tcp_client2->close();

  test_server_->waitForGauge("connection_limit.connection_limit_stats.active_connections", Eq(0),
                             std::chrono::milliseconds(100));

  EXPECT_EQ(1, test_server_->counter("connection_limit.connection_limit_stats.limited_connections")
                   ->value());
}

// The filter's stats have the same flat names, tag-extracted names and tags whether the tags are
// derived by the legacy tag-extraction rules (IPv4) or supplied explicitly (IPv6).
TEST_P(ConnectionLimitIntegrationTest, StatsTagsAndNamesAreIdenticalInBothModes) {
  setup(R"EOF(
name: connectionlimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.network.connection_limit.v3.ConnectionLimit
  stat_prefix: connection_limit_stats
  max_connections: 1
  delay: 0.2s
)EOF");

  const std::vector<std::pair<std::string, std::string>> tags{
      {Config::TagNames::get().CONNECTION_LIMIT_PREFIX, "connection_limit_stats"}};
  expectStat("connection_limit.connection_limit_stats.limited_connections",
             "connection_limit.limited_connections", tags);
  expectStat("connection_limit.connection_limit_stats.active_connections",
             "connection_limit.active_connections", tags);
}

} // namespace
} // namespace Envoy

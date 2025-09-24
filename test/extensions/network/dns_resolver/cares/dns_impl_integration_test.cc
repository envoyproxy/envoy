#include "source/common/protobuf/protobuf.h"
#include "source/extensions/network/dns_resolver/cares/dns_impl.h"

#include "test/integration/http_integration.h"
#include "test/test_common/simulated_time_system.h"

namespace Envoy {
namespace Network {
namespace {

class DnsImplIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                               public HttpIntegrationTest {
public:
  DnsImplIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DnsImplIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DnsImplIntegrationTest, LogicalDnsWithCaresResolver) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    auto& cluster = *bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster.set_type(envoy::config::cluster::v3::Cluster::LOGICAL_DNS);
    cluster.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::ALL);
  });
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
        route->mutable_route()->mutable_auto_host_rewrite()->set_value(true);
      });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(DnsImplIntegrationTest, StrictDnsWithCaresResolver) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    auto& cluster = *bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster.set_type(envoy::config::cluster::v3::Cluster::STRICT_DNS);
    cluster.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::ALL);
  });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test UDP Channel Refresh Behavior
class DnsResolverUdpChannelRefreshIntegrationTest : public testing::Test {
public:
  DnsResolverUdpChannelRefreshIntegrationTest()
      : api_(Api::createApiForTest(stats_store_, simulated_time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")) {}

  void SetUp() override {
    resolver_address_ = Network::Utility::parseInternetAddressAndPortNoThrow("127.0.0.1:5353");
    ASSERT_NE(nullptr, resolver_address_);
  }

  std::shared_ptr<DnsResolverImpl>
  createResolver(std::chrono::milliseconds refresh_duration = std::chrono::milliseconds::zero()) {
    envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig config;
    config.mutable_dns_resolver_options()->set_use_tcp_for_dns_lookups(false);

    // Add resolver address.
    envoy::config::core::v3::Address resolver_addr;
    Network::Utility::addressToProtobufAddress(*resolver_address_, resolver_addr);
    config.add_resolvers()->CopyFrom(resolver_addr);

    // Set UDP channel refresh duration if specified.
    if (refresh_duration > std::chrono::milliseconds::zero()) {
      config.mutable_max_udp_channel_duration()->CopyFrom(
          Protobuf::util::TimeUtil::MillisecondsToDuration(refresh_duration.count()));
    }

    auto csv_or_error = DnsResolverImpl::maybeBuildResolversCsv({resolver_address_});
    EXPECT_TRUE(csv_or_error.ok());
    return std::make_shared<DnsResolverImpl>(config, *dispatcher_, csv_or_error.value(),
                                             *stats_store_.rootScope());
  }

  Stats::TestUtil::TestStore stats_store_;
  Event::SimulatedTimeSystem simulated_time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Network::Address::InstanceConstSharedPtr resolver_address_;
};

// Test that UDP channel refresh actually triggers periodic reinitializations.
TEST_F(DnsResolverUdpChannelRefreshIntegrationTest, PeriodicRefreshWorks) {
  // Create resolver with 2-second refresh interval.
  auto resolver = createResolver(std::chrono::seconds(2));

  // Verify initial state: no reinitializations.
  EXPECT_EQ(0, stats_store_.counter("dns.cares.reinits").value());

  // Advance time but not enough to trigger refresh.
  simulated_time_system_.advanceTimeAndRun(std::chrono::milliseconds(1500), *dispatcher_,
                                           Event::Dispatcher::RunType::NonBlock);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(0, stats_store_.counter("dns.cares.reinits").value());

  // Advance time to trigger first refresh.
  simulated_time_system_.advanceTimeAndRun(std::chrono::milliseconds(600), *dispatcher_,
                                           Event::Dispatcher::RunType::NonBlock);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(1, stats_store_.counter("dns.cares.reinits").value());

  // Advance time to trigger second refresh.
  simulated_time_system_.advanceTimeAndRun(std::chrono::seconds(2), *dispatcher_,
                                           Event::Dispatcher::RunType::NonBlock);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(2, stats_store_.counter("dns.cares.reinits").value());

  // Advance time to trigger third refresh.
  simulated_time_system_.advanceTimeAndRun(std::chrono::seconds(2), *dispatcher_,
                                           Event::Dispatcher::RunType::NonBlock);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(3, stats_store_.counter("dns.cares.reinits").value());
}

// Test that without UDP channel refresh configured, no periodic reinitialization happens.
TEST_F(DnsResolverUdpChannelRefreshIntegrationTest, NoPeriodicRefreshWhenDisabled) {
  // Create resolver without refresh configuration. This is the default behavior.
  auto resolver = createResolver();

  // Verify initial state i.e., no reinitializations.
  EXPECT_EQ(0, stats_store_.counter("dns.cares.reinits").value());

  // Advance time significantly.
  simulated_time_system_.advanceTimeAndRun(std::chrono::seconds(10), *dispatcher_,
                                           Event::Dispatcher::RunType::NonBlock);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Should still be zero since periodic refresh is disabled.
  EXPECT_EQ(0, stats_store_.counter("dns.cares.reinits").value());

  // Advance more time to be sure.
  simulated_time_system_.advanceTimeAndRun(std::chrono::seconds(30), *dispatcher_,
                                           Event::Dispatcher::RunType::NonBlock);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(0, stats_store_.counter("dns.cares.reinits").value());
}

// Test that different refresh durations work correctly.
TEST_F(DnsResolverUdpChannelRefreshIntegrationTest, DifferentRefreshDurationsWork) {
  // Test with a very short refresh interval (500ms).
  auto resolver = createResolver(std::chrono::milliseconds(500));

  EXPECT_EQ(0, stats_store_.counter("dns.cares.reinits").value());

  // Should trigger refresh after 500ms.
  simulated_time_system_.advanceTimeAndRun(std::chrono::milliseconds(550), *dispatcher_,
                                           Event::Dispatcher::RunType::NonBlock);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(1, stats_store_.counter("dns.cares.reinits").value());

  // Should trigger again after another 500ms.
  simulated_time_system_.advanceTimeAndRun(std::chrono::milliseconds(500), *dispatcher_,
                                           Event::Dispatcher::RunType::NonBlock);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(2, stats_store_.counter("dns.cares.reinits").value());
}

// Test that refresh works alongside actual DNS queries.
TEST_F(DnsResolverUdpChannelRefreshIntegrationTest, RefreshWorksWithDnsQueries) {
  // Create resolver with 1-second refresh interval.
  auto resolver = createResolver(std::chrono::seconds(1));
  // Verify initial state i.e., no reinitializations yet.
  EXPECT_EQ(0, stats_store_.counter("dns.cares.reinits").value());

  // Perform a DNS query. This will likely fail due to no real DNS server, but that's OK.
  bool callback_called = false;
  resolver->resolve("example.com", DnsLookupFamily::V4Only,
                    [&](DnsResolver::ResolutionStatus, absl::string_view,
                        std::list<DnsResponse>&&) { callback_called = true; });

  // Advance time to trigger refresh.
  simulated_time_system_.advanceTimeAndRun(std::chrono::milliseconds(1100), *dispatcher_,
                                           Event::Dispatcher::RunType::NonBlock);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Should see reinitialization even with active DNS queries.
  EXPECT_GE(stats_store_.counter("dns.cares.reinits").value(), 1);

  // Advance time again.
  simulated_time_system_.advanceTimeAndRun(std::chrono::seconds(1), *dispatcher_,
                                           Event::Dispatcher::RunType::NonBlock);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_GE(stats_store_.counter("dns.cares.reinits").value(), 2);
}

} // namespace
} // namespace Network
} // namespace Envoy

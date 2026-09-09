#include "source/common/protobuf/protobuf.h"
#include "source/extensions/network/dns_resolver/cares/dns_impl.h"

#include "test/integration/http_integration.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"

namespace Envoy {
namespace Network {
namespace {

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

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

class SharedDnsResolverIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  SharedDnsResolverIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()),
        registered_dns_factory_(dns_resolver_factory_) {
    setUpstreamCount(2);
  }

  void configureDnsClusters() {
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
          auto* strict_dns_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          strict_dns_cluster->set_type(envoy::config::cluster::v3::Cluster::STRICT_DNS);
          strict_dns_cluster->set_dns_lookup_family(envoy::config::cluster::v3::Cluster::ALL);

          envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares;
          auto* typed_dns_resolver_config = strict_dns_cluster->mutable_typed_dns_resolver_config();
          typed_dns_resolver_config->set_name(std::string(Network::CaresDnsResolver));
          std::ignore = typed_dns_resolver_config->mutable_typed_config()->PackFrom(cares);

          auto* logical_dns_cluster = bootstrap.mutable_static_resources()->add_clusters();
          logical_dns_cluster->CopyFrom(*strict_dns_cluster);
          logical_dns_cluster->set_name("cluster_1");
          logical_dns_cluster->set_type(envoy::config::cluster::v3::Cluster::LOGICAL_DNS);
          logical_dns_cluster->mutable_load_assignment()->set_cluster_name("cluster_1");
        });
  }

  Network::DnsResolverSharedPtr makeResolver() {
    auto resolver = std::make_shared<NiceMock<Network::MockDnsResolver>>();
    ON_CALL(*resolver, resolve(_, _, _))
        .WillByDefault(Invoke([](const std::string&, Network::DnsLookupFamily,
                                     Network::DnsResolver::ResolveCb callback) {
          callback(Network::DnsResolver::ResolutionStatus::Completed, "",
                   TestUtility::makeDnsResponse(
                       {Network::Test::getLoopbackAddressString(GetParam())}));
          return nullptr;
        }));
    return resolver;
  }

  NiceMock<Network::MockDnsResolverFactory> dns_resolver_factory_;
  Registry::InjectFactory<Network::DnsResolverFactory> registered_dns_factory_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SharedDnsResolverIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(SharedDnsResolverIntegrationTest, StrictAndLogicalDnsClustersShareResolver) {
  config_helper_.addRuntimeOverride("envoy.restart_features.shared_cares_dns_resolver", "true");
  configureDnsClusters();

  auto resolver = makeResolver();
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _)).WillOnce(Return(resolver));

  initialize();
}

TEST_P(SharedDnsResolverIntegrationTest, RuntimeGuardDisablesResolverSharing) {
  config_helper_.addRuntimeOverride("envoy.restart_features.shared_cares_dns_resolver", "false");
  configureDnsClusters();

  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .Times(2)
      .WillRepeatedly(Invoke([this](Event::Dispatcher&, Api::Api&,
                                    const envoy::config::core::v3::TypedExtensionConfig&) {
        return makeResolver();
      }));

  initialize();
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
    EXPECT_OK(csv_or_error);
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

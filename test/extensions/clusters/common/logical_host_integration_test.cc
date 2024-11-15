#include "source/common/config/utility.h"

#include "test/integration/http_integration.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/threadsafe_singleton_injector.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {

// Logical Host integration test.
class LogicalHostIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public HttpIntegrationTest {
public:
  LogicalHostIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()),
        registered_dns_factory_(dns_resolver_factory_) {}

  void createUpstreams() override { HttpIntegrationTest::createUpstreams(); }
  struct address {
    uint32_t address_;
    absl::string_view first_address_string_;
  };

  NiceMock<Network::MockDnsResolverFactory> dns_resolver_factory_;
  Registry::InjectFactory<Network::DnsResolverFactory> registered_dns_factory_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, LogicalHostIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Reproduces a race from https://github.com/envoyproxy/envoy/issues/32850.
// The test is by mocking the DNS resolver to return multiple different
// addresses, also config dns_refresh_rate to be extremely fast.
TEST_P(LogicalHostIntegrationTest, LogicalDNSRaceCrashTest) {
  auto address_ptr = std::make_shared<address>();
  address_ptr->address_ = 0;
  // first_address_string_ is used to make connections. It needs
  // to match with the IpVersion of the test.
  if (version_ == Network::Address::IpVersion::v4) {
    address_ptr->first_address_string_ = "127.0.0.1";
  } else {
    address_ptr->first_address_string_ = "::1";
  }

  auto dns_resolver = std::make_shared<Network::MockDnsResolver>();
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillRepeatedly(testing::Return(dns_resolver));
  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(Invoke(
          [address_ptr](const std::string&, Network::DnsLookupFamily,
                        Network::DnsResolver::ResolveCb dns_callback) -> Network::ActiveDnsQuery* {
            const uint32_t address = address_ptr->address_;
            // Keep changing the returned addresses to force address update.
            dns_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
                         TestUtility::makeDnsResponse({
                             // The only significant address is the first one; the other ones are
                             // just used to populate a list whose maintenance is race-prone.
                             std::string(address_ptr->first_address_string_),
                             absl::StrCat("127.0.0.", address),
                             absl::StrCat("127.0.0.", address + 1),
                             absl::StrCat("127.0.0.", address + 2),
                             absl::StrCat("127.0.0.", address + 3),
                             absl::StrCat("127.0.0.", address + 4),
                             absl::StrCat("127.0.0.", address + 5),
                             absl::StrCat("127.0.0.", address + 6),
                             absl::StrCat("127.0.0.", address + 7),
                             absl::StrCat("127.0.0.", address + 8),
                             absl::StrCat("127.0.0.", address + 9),
                             "::2",
                             "::3",
                             "::4",
                             "::5",
                             "::6",
                             "::7",
                             "::8",
                             "::9",
                         }));
            address_ptr->address_ = (address + 1) % 128;
            return nullptr;
          }));
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    auto& cluster = *bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster.set_type(envoy::config::cluster::v3::Cluster::LOGICAL_DNS);
    cluster.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::ALL);
    // Make the refresh rate fast to hit the R/W race.
    cluster.mutable_dns_refresh_rate()->set_nanos(1000001);
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

} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

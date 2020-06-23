#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "extensions/filters/udp/dns_filter/dns_filter.h"

#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"

#include "dns_filter_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace {

class DnsFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public BaseIntegrationTest {
public:
  DnsFilterIntegrationTest() : BaseIntegrationTest(GetParam(), configToUse()) {
    setupResponseParser();
  }

  void setupResponseParser() {
    response_parser_ = std::make_unique<DnsMessageParser>(true /*recursive queries */,
                                                          0 /* retry_count */, random_);
  }

  static std::string configToUse() {
    return absl::StrCat(ConfigHelper::baseUdpListenerConfig(), R"EOF(
    listener_filters:
      name: "envoy.filters.udp.dns_filter"
      typed_config:
        '@type': 'type.googleapis.com/envoy.extensions.filters.udp.dns_filter.v3alpha.DnsFilterConfig'
        stat_prefix: "my_prefix"
        server_config:
          inline_dns_table:
            external_retry_count: 3
            known_suffixes:
            - suffix: "foo1.com"
            - suffix: "cluster_0"
            virtual_domains:
            - name: "www.foo1.com"
              endpoint:
                address_list:
                  address:
                  - 10.0.0.1
                  - 10.0.0.2
                  - 10.0.0.3
                  - 10.0.0.4
            - name: "cluster.foo1.com"
              endpoint:
                cluster_name: "cluster_0"
    )EOF");
  }

  void setup(uint32_t upstream_count) {
    udp_fake_upstream_ = true;
    if (upstream_count > 1) {
      setDeterministic();
      setUpstreamCount(upstream_count);
      config_helper_.addConfigModifier(
          [upstream_count](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
            for (uint32_t i = 1; i < upstream_count; i++) {
              bootstrap.mutable_static_resources()
                  ->mutable_clusters(0)
                  ->mutable_load_assignment()
                  ->mutable_endpoints(0)
                  ->add_lb_endpoints()
                  ->mutable_endpoint()
                  ->MergeFrom(ConfigHelper::buildEndpoint(
                      Network::Test::getLoopbackAddressString(GetParam())));
            }
          });
    }
    BaseIntegrationTest::initialize();
  }

  void requestResponseWithListenerAddress(const Network::Address::Instance& listener_address,
                                          const std::string& data_to_send,
                                          Network::UdpRecvData& response_datagram) {
    Network::Test::UdpSyncPeer client(version_);
    client.write(data_to_send, listener_address);
    client.recv(response_datagram);
  }

  NiceMock<Runtime::MockRandomGenerator> random_;
  std::unique_ptr<DnsMessageParser> response_parser_;
  DnsQueryContextPtr query_ctx_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DnsFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DnsFilterIntegrationTest, LocalLookupTest) {
  setup(0);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  Network::UdpRecvData response;
  std::string query =
      Utils::buildQueryForDomain("www.foo1.com", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  query_ctx_ = response_parser_->createQueryContext(response);
  EXPECT_TRUE(query_ctx_->parse_status_);

  EXPECT_EQ(4, query_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
}

TEST_P(DnsFilterIntegrationTest, ClusterLookupTest) {
  setup(2);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  uint16_t record_type;
  if (listener_address->ip()->ipv6()) {
    record_type = DNS_RECORD_TYPE_AAAA;
  } else {
    record_type = DNS_RECORD_TYPE_A;
  }

  Network::UdpRecvData response;
  std::string query = Utils::buildQueryForDomain("cluster_0", record_type, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  query_ctx_ = response_parser_->createQueryContext(response);
  EXPECT_TRUE(query_ctx_->parse_status_);

  EXPECT_EQ(2, query_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
}

TEST_P(DnsFilterIntegrationTest, ClusterEndpointLookupTest) {
  setup(2);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  uint16_t record_type;
  if (listener_address->ip()->ipv6()) {
    record_type = DNS_RECORD_TYPE_AAAA;
  } else {
    record_type = DNS_RECORD_TYPE_A;
  }

  Network::UdpRecvData response;
  std::string query =
      Utils::buildQueryForDomain("cluster.foo1.com", record_type, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  query_ctx_ = response_parser_->createQueryContext(response);
  EXPECT_TRUE(query_ctx_->parse_status_);

  EXPECT_EQ(2, query_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
}

} // namespace
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "source/extensions/filters/udp/dns_filter/dns_filter.h"

#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"

#include "dns_filter_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace {

using ResponseValidator = Utils::DnsResponseValidator;

class DnsFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public BaseIntegrationTest {
public:
  DnsFilterIntegrationTest()
      : BaseIntegrationTest(GetParam(), configToUse()), api_(Api::createApiForTest()),
        counters_(mock_query_buffer_underflow_, mock_record_name_overflow_, query_parsing_failure_,
                  queries_with_additional_rrs_, queries_with_ans_or_authority_rrs_) {
    setupResponseParser();
  }

  void setupResponseParser() { histogram_.unit_ = Stats::Histogram::Unit::Milliseconds; }

  static std::string configToUse() {
    return fmt::format(R"EOF(
admin:
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: "{}"
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
static_resources:
  clusters:
    name: cluster_0
    load_assignment:
      cluster_name: cluster_0
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: {}
                port_value: 0
    )EOF",
                       Platform::null_device_path,
                       Network::Test::getLoopbackAddressString(GetParam()));
  }

  Network::Address::InstanceConstSharedPtr getListenerBindAddressAndPort() {
    auto addr = Network::Utility::parseInternetAddressAndPort(
        fmt::format("{}:{}", Envoy::Network::Test::getLoopbackAddressUrlString(version_), 0),
        false);

    ASSERT(addr != nullptr);

    addr = Network::Test::findOrCheckFreePort(addr, Network::Socket::Type::Datagram);
    ASSERT(addr != nullptr && addr->ip() != nullptr);

    return addr;
  }

  envoy::config::listener::v3::Listener
  getListener0(Network::Address::InstanceConstSharedPtr& addr) {
    auto config = fmt::format(R"EOF(
name: listener_0
address:
  socket_address:
    address: {}
    port_value: 0
    protocol: udp
listener_filters:
  name: "envoy.filters.udp.dns_filter"
  typed_config:
    '@type': 'type.googleapis.com/envoy.extensions.filters.udp.dns_filter.v3.DnsFilterConfig'
    stat_prefix: "my_prefix"
    client_config:
      resolver_timeout: 1s
      typed_dns_resolver_config:
        name: envoy.network.dns_resolver.cares
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig
          resolvers:
          - socket_address:
              address: {}
              port_value: {}
          dns_resolver_options:
            use_tcp_for_dns_lookups: false
            no_default_search_domain: false
      max_pending_lookups: 256
    server_config:
      inline_dns_table:
        external_retry_count: 0
        virtual_domains:
        - name: "im_just_here_so_coverage_doesnt_fail.foo1.com"
          endpoint:
            cluster_name: "cluster_0"
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
        - name: "web.foo1.com"
          endpoint:
            service_list:
              services:
              - service_name: "http"
                protocol: {{ name: "tcp" }}
                ttl: 43200s
                targets:
                - cluster_name: "cluster_0"
                  weight: 10
                  priority: 40
                  port: 80
              - service_name: "https"
                protocol: {{ name: "tcp" }}
                ttl: 43200s
                targets:
                - cluster_name: "cluster_0"
                  weight: 20
                  priority: 10
)EOF",
                              addr->ip()->addressAsString(), addr->ip()->addressAsString(),
                              addr->ip()->port());
    return TestUtility::parseYaml<envoy::config::listener::v3::Listener>(config);
  }

  envoy::config::listener::v3::Listener
  getListener1(Network::Address::InstanceConstSharedPtr& addr) {
    auto config = fmt::format(R"EOF(
name: listener_1
address:
  socket_address:
    address: {}
    port_value: {}
    protocol: udp
listener_filters:
  name: "envoy.filters.udp.dns_filter"
  typed_config:
    '@type': 'type.googleapis.com/envoy.extensions.filters.udp.dns_filter.v3.DnsFilterConfig'
    stat_prefix: "external_resolver"
    server_config:
      inline_dns_table:
        external_retry_count: 0
        virtual_domains:
        - name: "www.google.com"
          endpoint:
            address_list:
              address:
              - 42.42.42.42
              - 2607:42:42::42:42
)EOF",
                              addr->ip()->addressAsString(), addr->ip()->port());
    return TestUtility::parseYaml<envoy::config::listener::v3::Listener>(config);
  }

  void setup(uint32_t upstream_count) {
    setUdpFakeUpstream(FakeUpstreamConfig::UdpConfig());
    if (upstream_count > 1) {
      setDeterministicValue();
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

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto addr_port = getListenerBindAddressAndPort();
      auto listener_0 = getListener0(addr_port);
      auto listener_1 = getListener1(addr_port);
      bootstrap.mutable_static_resources()->add_listeners()->MergeFrom(listener_0);
      bootstrap.mutable_static_resources()->add_listeners()->MergeFrom(listener_1);
    });

    BaseIntegrationTest::initialize();
  }

  void requestResponseWithListenerAddress(const Network::Address::Instance& listener_address,
                                          const std::string& data_to_send,
                                          Network::UdpRecvData& response_datagram) {
    Network::Test::UdpSyncPeer client(version_);
    client.write(data_to_send, listener_address);
    client.recv(response_datagram);
  }

  Api::ApiPtr api_;
  NiceMock<Stats::MockHistogram> histogram_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Stats::MockCounter> mock_query_buffer_underflow_;
  NiceMock<Stats::MockCounter> mock_record_name_overflow_;
  NiceMock<Stats::MockCounter> query_parsing_failure_;
  NiceMock<Stats::MockCounter> queries_with_additional_rrs_;
  NiceMock<Stats::MockCounter> queries_with_ans_or_authority_rrs_;
  DnsParserCounters counters_;
  DnsQueryContextPtr response_ctx_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DnsFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DnsFilterIntegrationTest, ExternalLookupTest) {
  setup(0);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  Network::UdpRecvData response;
  std::string query =
      Utils::buildQueryForDomain("www.google.com", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(1, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
}

TEST_P(DnsFilterIntegrationTest, ExternalLookupTestIPv6) {
  setup(0);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  Network::UdpRecvData response;
  std::string query =
      Utils::buildQueryForDomain("www.google.com", DNS_RECORD_TYPE_AAAA, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(1, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
}

TEST_P(DnsFilterIntegrationTest, LocalLookupTest) {
  setup(0);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  Network::UdpRecvData response;
  std::string query =
      Utils::buildQueryForDomain("www.foo1.com", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(4, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
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

  response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(2, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
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

  response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(2, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
}

TEST_P(DnsFilterIntegrationTest, ClusterEndpointWithPortServiceRecordLookupTest) {
  setup(2);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  const std::string service("_http._tcp.web.foo1.com");
  Network::UdpRecvData response;
  std::string query = Utils::buildQueryForDomain(service, DNS_RECORD_TYPE_SRV, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(2, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());

  for (const auto& answer : response_ctx_->answers_) {
    EXPECT_EQ(answer.second->type_, DNS_RECORD_TYPE_SRV);

    DnsSrvRecord* srv_rec = dynamic_cast<DnsSrvRecord*>(answer.second.get());

    EXPECT_EQ(service, srv_rec->name_);
    EXPECT_EQ(43200, srv_rec->ttl_.count());

    EXPECT_EQ(1, srv_rec->targets_.size());
    const auto& target = srv_rec->targets_.begin();

    EXPECT_EQ(10, target->second.weight);
    EXPECT_EQ(40, target->second.priority);
    EXPECT_EQ(80, target->second.port);
  }
}

TEST_P(DnsFilterIntegrationTest, ClusterEndpointWithoutPortServiceRecordLookupTest) {
  constexpr size_t endpoints = 2;
  setup(endpoints);
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  const std::string service("_https._tcp.web.foo1.com");
  Network::UdpRecvData response;
  std::string query = Utils::buildQueryForDomain(service, DNS_RECORD_TYPE_SRV, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(endpoints, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());

  std::set<uint16_t> ports;
  for (const auto& answer : response_ctx_->answers_) {
    EXPECT_EQ(answer.second->type_, DNS_RECORD_TYPE_SRV);

    DnsSrvRecord* srv_rec = dynamic_cast<DnsSrvRecord*>(answer.second.get());

    EXPECT_EQ(service, srv_rec->name_);
    EXPECT_EQ(43200, srv_rec->ttl_.count());

    EXPECT_EQ(1, srv_rec->targets_.size());
    const auto& target = srv_rec->targets_.begin();

    EXPECT_EQ(20, target->second.weight);
    EXPECT_EQ(10, target->second.priority);

    // The port is unspecified and automatically assigned by the cluster
    EXPECT_NE(0, target->second.priority);
    ports.emplace(target->second.port);
  }

  EXPECT_EQ(endpoints, ports.size());
}
} // namespace
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

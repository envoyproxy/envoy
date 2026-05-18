#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "source/extensions/filters/udp/dns_filter/dns_filter.h"
#include "source/extensions/network/dns_resolver/getaddrinfo/getaddrinfo.h"

#include "test/integration/integration.h"
#include "test/test_common/network_utility.h"

#include "dns_filter_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace {

using ResponseValidator = Utils::DnsResponseValidator;

class DnsFilterAccessLogIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public BaseIntegrationTest {
public:
  DnsFilterAccessLogIntegrationTest()
      : BaseIntegrationTest(GetParam(), configToUse()),
        access_log_path_(TestEnvironment::temporaryPath(TestUtility::uniqueFilename())),
        counters_(mock_query_buffer_underflow_, mock_record_name_overflow_, query_parsing_failure_,
                  queries_with_additional_rrs_, queries_with_ans_or_authority_rrs_) {}

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
    auto addr = Network::Utility::parseInternetAddressAndPortNoThrow(
        fmt::format("{}:{}", Network::Test::getLoopbackAddressUrlString(version_), 0), false);

    ASSERT(addr != nullptr);

    addr = Network::Test::findOrCheckFreePort(addr, Network::Socket::Type::Datagram);
    ASSERT(addr != nullptr && addr->ip() != nullptr);

    return addr;
  }

  envoy::config::listener::v3::Listener
  getListenerWithAccessLog(Network::Address::InstanceConstSharedPtr& addr) {
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
    server_config:
      inline_dns_table:
        external_retry_count: 0
        virtual_domains:
        - name: "www.foo1.com"
          endpoint:
            address_list:
              address:
              - 10.0.0.1
              - 10.0.0.2
    access_log:
    - name: envoy.access_loggers.file
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
        path: "{}"
        log_format:
          text_format_source:
            inline_string: "query_name=%QUERY_NAME% query_type=%QUERY_TYPE% answer_count=%ANSWER_COUNT% response_code=%RESPONSE_CODE%\n"
)EOF",
                              addr->ip()->addressAsString(), access_log_path_);
    return TestUtility::parseYaml<envoy::config::listener::v3::Listener>(config);
  }

  void setup() {
    setUdpFakeUpstream(FakeUpstreamConfig::UdpConfig());
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* typed_dns_resolver_config = bootstrap.mutable_typed_dns_resolver_config();
      typed_dns_resolver_config->set_name("envoy.network.dns_resolver.getaddrinfo");
      envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig
          config;
      config.mutable_num_retries()->set_value(1);
      typed_dns_resolver_config->mutable_typed_config()->PackFrom(config);
    });

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto addr_port = getListenerBindAddressAndPort();
      auto listener = getListenerWithAccessLog(addr_port);
      bootstrap.mutable_static_resources()->add_listeners()->MergeFrom(listener);
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

  const std::string access_log_path_;
  NiceMock<Stats::MockCounter> mock_query_buffer_underflow_;
  NiceMock<Stats::MockCounter> mock_record_name_overflow_;
  NiceMock<Stats::MockCounter> query_parsing_failure_;
  NiceMock<Stats::MockCounter> queries_with_additional_rrs_;
  NiceMock<Stats::MockCounter> queries_with_ans_or_authority_rrs_;
  DnsParserCounters counters_;
  DnsQueryContextPtr response_ctx_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DnsFilterAccessLogIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DnsFilterAccessLogIntegrationTest, DnsAccessLogFormatCommands) {
  setup();
  const uint32_t port = lookupPort("listener_0");
  const auto listener_address = *Network::Utility::resolveUrl(
      fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

  Network::UdpRecvData response;
  std::string query =
      Utils::buildQueryForDomain("www.foo1.com", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  requestResponseWithListenerAddress(*listener_address, query, response);

  response_ctx_ = ResponseValidator::createResponseContext(response, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(2, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());

  std::string log_entry = waitForAccessLog(access_log_path_);
  EXPECT_THAT(log_entry, testing::HasSubstr("query_name=www.foo1.com"));
  EXPECT_THAT(log_entry, testing::HasSubstr("query_type=1"));
  EXPECT_THAT(log_entry, testing::HasSubstr("answer_count=2"));
  EXPECT_THAT(log_entry, testing::HasSubstr("response_code=0"));
}

} // namespace
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

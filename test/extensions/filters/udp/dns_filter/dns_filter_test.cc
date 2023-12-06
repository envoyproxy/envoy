#include "envoy/extensions/filters/udp/dns_filter/v3/dns_filter.pb.h"
#include "envoy/extensions/filters/udp/dns_filter/v3/dns_filter.pb.validate.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/udp/dns_filter/dns_filter_constants.h"
#include "source/extensions/filters/udp/dns_filter/dns_filter_utils.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/listener_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"

#include "dns_filter_test_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AnyNumber;
using testing::AtLeast;
using testing::DoAll;
using testing::InSequence;
using testing::Mock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace {

using ResponseValidator = Utils::DnsResponseValidator;

Api::IoCallUint64Result makeNoError(uint64_t rc) {
  auto no_error = Api::ioCallUint64ResultNoError();
  no_error.return_value_ = rc;
  return no_error;
}

class DnsFilterTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  DnsFilterTest()
      : listener_address_(Network::Utility::parseInternetAddressAndPort("127.0.2.1:5353")),
        api_(Api::createApiForTest(random_)),
        counters_(mock_query_buffer_underflow_, mock_record_name_overflow_, query_parsing_failure_,
                  queries_with_additional_rrs_, queries_with_ans_or_authority_rrs_) {
    udp_response_.addresses_.local_ = listener_address_;
    udp_response_.addresses_.peer_ = listener_address_;
    udp_response_.buffer_ = std::make_unique<Buffer::OwnedImpl>();

    setupResponseParser();
    EXPECT_CALL(callbacks_, udpListener()).Times(AtLeast(0));
    EXPECT_CALL(callbacks_.udp_listener_, send(_))
        .WillRepeatedly(
            Invoke([this](const Network::UdpSendData& send_data) -> Api::IoCallUint64Result {
              udp_response_.buffer_->drain(udp_response_.buffer_->length());
              udp_response_.buffer_->move(send_data.buffer_);
              return makeNoError(udp_response_.buffer_->length());
            }));
    EXPECT_CALL(callbacks_.udp_listener_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
  }

  ~DnsFilterTest() override { EXPECT_CALL(callbacks_.udp_listener_, onDestroy()); }

  void setupResponseParser() {
    histogram_.unit_ = Stats::Histogram::Unit::Milliseconds;
    query_parser_ = std::make_unique<DnsMessageParser>(
        true /* recursive queries */, api_->timeSource(), 0 /* retries */, random_, histogram_);
  }

  void setup(const std::string& yaml) {
    envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig config;
    TestUtility::loadFromYamlAndValidate(yaml, config);
    auto store = stats_store_.createScope("dns_scope");
    ON_CALL(listener_factory_, scope()).WillByDefault(ReturnRef(*store));
    ON_CALL(listener_factory_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(random_, random()).WillByDefault(Return(3));
    ON_CALL(listener_factory_, random()).WillByDefault(ReturnRef(random_));

    resolver_ = std::make_shared<Network::MockDnsResolver>();
    NiceMock<Network::MockDnsResolverFactory> dns_resolver_factory_;
    Registry::InjectFactory<Network::DnsResolverFactory> registered_dns_factory_(
        dns_resolver_factory_);
    ON_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
        .WillByDefault(DoAll(SaveArg<2>(&typed_dns_resolver_config_), Return(resolver_)));

    config_ = std::make_shared<DnsFilterEnvoyConfig>(listener_factory_, config);
    filter_ = std::make_unique<DnsFilter>(callbacks_, config_);
    // Verify typed DNS resolver config is c-ares.
    EXPECT_EQ(typed_dns_resolver_config_.name(), std::string(Network::CaresDnsResolver));
    EXPECT_EQ(typed_dns_resolver_config_.typed_config().type_url(),
              "type.googleapis.com/"
              "envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig");
    typed_dns_resolver_config_.typed_config().UnpackTo(&cares_);
    dns_resolver_options_.MergeFrom(cares_.dns_resolver_options());
  }

  void sendQueryFromClient(const std::string& peer_address, const std::string& buffer) {
    Network::UdpRecvData data{};
    data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort(peer_address);
    data.addresses_.local_ = listener_address_;
    data.buffer_ = std::make_unique<Buffer::OwnedImpl>(buffer);
    data.receive_time_ = MonotonicTime(std::chrono::seconds(0));
    filter_->onData(data);
  }

  const Network::Address::InstanceConstSharedPtr listener_address_;
  envoy::config::core::v3::DnsResolverOptions dns_resolver_options_;
  envoy::config::core::v3::TypedExtensionConfig typed_dns_resolver_config_;
  envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig cares_;
  NiceMock<Random::MockRandomGenerator> random_;
  Api::ApiPtr api_;
  DnsFilterEnvoyConfigSharedPtr config_;
  NiceMock<Stats::MockCounter> mock_query_buffer_underflow_;
  NiceMock<Stats::MockCounter> mock_record_name_overflow_;
  NiceMock<Stats::MockCounter> query_parsing_failure_;
  NiceMock<Stats::MockCounter> queries_with_additional_rrs_;
  NiceMock<Stats::MockCounter> queries_with_ans_or_authority_rrs_;
  DnsParserCounters counters_;
  DnsQueryContextPtr response_ctx_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Network::MockUdpReadFilterCallbacks callbacks_;
  Network::UdpRecvData udp_response_;
  NiceMock<Filesystem::MockInstance> file_system_;
  NiceMock<Stats::MockHistogram> histogram_;
  NiceMock<Server::Configuration::MockListenerFactoryContext> listener_factory_;
  Stats::IsolatedStoreImpl stats_store_;
  std::shared_ptr<Network::MockDnsResolver> resolver_;
  std::unique_ptr<DnsFilter> filter_;
  std::unique_ptr<DnsMessageParser> query_parser_;

  const std::string forward_query_off_config = R"EOF(
stat_prefix: "my_prefix"
server_config:
  inline_dns_table:
    external_retry_count: 3
    virtual_domains:
    - name: "www.foo1.com"
      endpoint:
        address_list:
          address:
          - "10.0.0.1"
          - "10.0.0.2"
    - name: "www.foo2.com"
      endpoint:
        address_list:
          address:
          - "2001:8a:c1::2800:7"
          - "2001:8a:c1::2800:8"
          - "2001:8a:c1::2800:9"
    - name: "www.foo3.com"
      endpoint:
        address_list:
          address:
          - "10.0.3.1"
    - name: "www.foo16.com"
      endpoint:
        address_list:
          address:
          - "10.0.16.1"
          - "10.0.16.2"
          - "10.0.16.3"
          - "10.0.16.4"
          - "10.0.16.5"
          - "10.0.16.6"
          - "10.0.16.7"
          - "10.0.16.8"
          - "10.0.16.9"
          - "10.0.16.10"
          - "10.0.16.11"
          - "10.0.16.12"
          - "10.0.16.13"
          - "10.0.16.14"
          - "10.0.16.15"
          - "10.0.16.16"
    - name: www.supercalifragilisticexpialidocious.thisismydomainforafivehundredandtwelvebytetest.com
      endpoint:
        address_list:
          address:
          - "2001:8a:c1::2801:0001"
          - "2001:8a:c1::2801:0002"
          - "2001:8a:c1::2801:0003"
          - "2001:8a:c1::2801:0004"
          - "2001:8a:c1::2801:0005"
          - "2001:8a:c1::2801:0006"
          - "2001:8a:c1::2801:0007"
          - "2001:8a:c1::2801:0008"
)EOF";

  const std::string forward_query_on_config = R"EOF(
stat_prefix: "my_prefix"
client_config:
  resolver_timeout: 1s
  typed_dns_resolver_config:
    name: envoy.network.dns_resolver.cares
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig
      resolvers:
      - socket_address:
          address: "1.1.1.1"
          port_value: 53
      - socket_address:
          address: "8.8.8.8"
          port_value: 53
      - socket_address:
          address: "8.8.4.4"
          port_value: 53
  max_pending_lookups: 1
server_config:
  inline_dns_table:
    external_retry_count: 0
    virtual_domains:
      - name: "www.foo1.com"
        endpoint:
          address_list:
            address:
            - "10.0.0.1"
)EOF";

  static constexpr absl::string_view external_dns_table_config = R"EOF(
stat_prefix: "my_prefix"
client_config:
  resolver_timeout: 1s
  typed_dns_resolver_config:
    name: envoy.network.dns_resolver.cares
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig
      resolvers:
      - socket_address:
          address: "1.1.1.1"
          port_value: 53
  max_pending_lookups: 256
server_config:
  external_dns_table:
    filename: {}
)EOF";

  static constexpr absl::string_view dns_resolver_options_config_not_set = R"EOF(
stat_prefix: "my_prefix"
client_config:
  resolver_timeout: 1s
  typed_dns_resolver_config:
    name: envoy.network.dns_resolver.cares
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig
      resolvers:
      - socket_address:
          address: "1.1.1.1"
          port_value: 53
  max_pending_lookups: 256
server_config:
  external_dns_table:
    filename: {}
)EOF";

  static constexpr absl::string_view dns_resolver_options_config_set_false = R"EOF(
stat_prefix: "my_prefix"
client_config:
  resolver_timeout: 1s
  typed_dns_resolver_config:
    name: envoy.network.dns_resolver.cares
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig
      dns_resolver_options:
        use_tcp_for_dns_lookups: false
        no_default_search_domain: false
      resolvers:
      - socket_address:
          address: "1.1.1.1"
          port_value: 53
  max_pending_lookups: 256
server_config:
  external_dns_table:
    filename: {}
)EOF";

  static constexpr absl::string_view dns_resolver_options_config_set_true = R"EOF(
stat_prefix: "my_prefix"
client_config:
  resolver_timeout: 1s
  typed_dns_resolver_config:
    name: envoy.network.dns_resolver.cares
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig
      dns_resolver_options:
        use_tcp_for_dns_lookups: true
        no_default_search_domain: true
      resolvers:
      - socket_address:
          address: "1.1.1.1"
          port_value: 53
  max_pending_lookups: 256
server_config:
  external_dns_table:
    filename: {}
)EOF";

  const std::string external_dns_table_json = R"EOF(
{
  "external_retry_count": 3,
  "virtual_domains": [
    {
      "name": "www.external_foo1.com",
      "endpoint": { "address_list": { "address": [ "10.0.0.1", "10.0.0.2" ] } }
    },
    {
      "name": "www.external_foo2.com",
      "endpoint": { "address_list": { "address": [ "2001:8a:c1::2800:7" ] } }
    },
    {
      "name": "www.external_foo3.com",
      "endpoint": { "address_list": { "address": [ "10.0.3.1" ] } }
    }
  ]
}
)EOF";

  const std::string external_dns_table_yaml = R"EOF(
external_retry_count: 3
virtual_domains:
  - name: "www.external_foo1.com"
    endpoint:
      address_list:
        address:
        - "10.0.0.1"
  - name: "www.external_foo1.com"
    endpoint:
      address_list:
        address:
        - "10.0.0.2"
  - name: "www.external_foo2.com"
    endpoint:
      address_list:
        address:
        - "2001:8a:c1::2800:7"
  - name: "www.external_foo3.com"
    endpoint:
      address_list:
        address:
        - "10.0.3.1"
)EOF";

  const std::string max_records_table_yaml = R"EOF(
external_retry_count: 3
virtual_domains:
  - name: "one.web.ermac.com"
    endpoint:
      address_list: { address: [ "10.0.17.1" ] }
  - name: "two.web.ermac.com"
    endpoint:
      address_list: { address: [ "10.0.17.2" ] }
  - name: "three.web.ermac.com"
    endpoint:
      address_list: { address: [ "10.0.17.3" ] }
  - name: "four.web.ermac.com"
    endpoint:
      address_list: { address: [ "10.0.17.4" ] }
  - name: "five.web.ermac.com"
    endpoint:
      address_list: { address: [ "10.0.17.5" ] }
  - name: "six.web.ermac.com"
    endpoint:
      address_list: { address: [ "10.0.17.6" ] }
  - name: "seven.web.ermac.com"
    endpoint:
      address_list: { address: [ "10.0.17.7" ] }
  - name: "eight.web.ermac.com"
    endpoint:
      address_list: { address: [ "10.0.17.8" ] }
  - name: "nine.web.ermac.com"
    endpoint:
      address_list: { address: [ "10.0.17.9" ] }
  - name: "ten.web.ermac.com"
    endpoint:
      address_list: { address: [ "10.0.17.10" ] }
  - name: "eleven.web.ermac.com"
    endpoint:
      address_list: { address: [ "10.0.17.11" ] }
  - name: "twelve.web.ermac.com"
    endpoint:
      address_list: { address: [ "10.0.17.12" ] }
  - name: "web.ermac.com"
    endpoint:
      service_list:
        services:
        - service_name: "http"
          protocol: { number: 6 }
          ttl: 86400s
          targets: [
            { host_name: "one.web.ermac.com" , weight: 120, priority: 10, port: 80 },
            { host_name: "two.web.ermac.com", weight: 110, priority: 10, port: 80 },
            { host_name: "three.web.ermac.com", weight: 100, priority: 10, port: 80 },
            { host_name: "four.web.ermac.com", weight: 90, priority: 10, port: 80 },
            { host_name: "five.web.ermac.com" , weight: 80, priority: 10, port: 80 },
            { host_name: "six.web.ermac.com", weight: 70, priority: 10, port: 80 },
            { host_name: "seven.web.ermac.com", weight: 60, priority: 10, port: 80 },
            { host_name: "eight.web.ermac.com", weight: 50, priority: 10, port: 80 },
            { host_name: "nine.web.ermac.com" , weight: 40, priority: 10, port: 80 },
            { host_name: "ten.web.ermac.com", weight: 30, priority: 10, port: 80 },
            { host_name: "eleven.web.ermac.com", weight: 20, priority: 10, port: 80 },
            { host_name: "twelve.web.ermac.com", weight: 10, priority: 10, port: 80 }
          ]
)EOF";

  const std::string external_dns_table_services_yaml = R"EOF(
external_retry_count: 3
virtual_domains:
  - name: "primary.voip.subzero.com"
    endpoint:
      address_list: { address: [ "10.0.3.1" ] }
  - name: "secondary.voip.subzero.com"
    endpoint:
      address_list: { address: [ "10.0.3.2" ] }
  - name: "backup.voip.subzero.com"
    endpoint:
      address_list: { address: [ "10.0.3.3" ] }
  - name: "emergency.voip.subzero.com"
    endpoint:
      address_list: { address: [ "2200:823f::cafe:beef" ] }
  - name: "voip.subzero.com"
    endpoint:
      service_list:
        services:
        - service_name: "sip"
          protocol: { number: 6 }
          ttl: 86400s
          targets: [
            { host_name: "primary.voip.subzero.com" , weight: 30, priority: 10, port: 5060 },
            { host_name: "secondary.voip.subzero.com", weight: 20, priority: 10, port: 5061 },
            { host_name: "backup.voip.subzero.com", weight: 10, priority: 10, port: 5062 },
            { host_name: "emergency.voip.subzero.com", weight: 40, priority: 10, port: 5063 }
          ]
  - name: "web.subzero.com"
    endpoint:
      service_list:
        services:
        - service_name: "http"
          protocol: { name: "tcp" }
          ttl: 43200s
          port: 80
          targets:
          - name:
              cluster_name: "fake_http_cluster_0"
            weight: 10
            priority: 1
        - service_name: "https"
          protocol: { name: "tcp" }
          ttl: 43200s
          targets:
          - name:
              cluster_name: "fake_http_cluster_1"
            weight: 10
            priority: 1
        - service_name: "for_coverage_no_protocol_defined_so_record_is_skipped"
          ttl: 86400s
          targets:
          - name:
              cluster_name: "fake_http_cluster_3"
            weight: 3
            priority: 99
)EOF";
};

TEST_F(DnsFilterTest, InvalidQuery) {
  InSequence s;

  setup(forward_query_off_config);
  sendQueryFromClient("10.0.0.1:1000", "hello");
  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(0, response_ctx_->answers_.size());

  // Validate stats
  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
  EXPECT_TRUE(config_->stats().downstream_rx_bytes_.used());
  EXPECT_TRUE(config_->stats().downstream_tx_bytes_.used());
}

TEST_F(DnsFilterTest, MaxQueryAndResponseSizeTest) {
  InSequence s;

  setup(forward_query_off_config);
  std::string domain(
      "www.supercalifragilisticexpialidocious.thisismydomainforafivehundredandtwelvebytetest.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_AAAA, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  sendQueryFromClient("10.0.0.1:1000", query);
  EXPECT_LT(udp_response_.buffer_->length(), Utils::MAX_UDP_DNS_SIZE);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
  // There are 8 addresses, however, since the domain is part of the answer record, each
  // serialized answer is over 100 bytes in size, there is room for 3 before the next
  // serialized answer puts the buffer over the 512 byte limit. The query itself is also
  // around 100 bytes.
  EXPECT_EQ(3, response_ctx_->answers_.size());

  // Validate stats
  EXPECT_EQ(1, config_->stats().aaaa_record_queries_.value());

  // Although there are only 3 answers returned, the filter did find 8 records for the query
  EXPECT_EQ(8, config_->stats().local_aaaa_record_answers_.value());
  EXPECT_EQ(0, config_->stats().downstream_rx_invalid_queries_.value());
  EXPECT_TRUE(config_->stats().downstream_rx_bytes_.used());
  EXPECT_TRUE(config_->stats().downstream_tx_bytes_.used());
}

TEST_F(DnsFilterTest, InvalidQueryNameTooLongTest) {
  InSequence s;

  setup(forward_query_off_config);
  std::string domain = "www." + std::string(256, 'a') + ".com";
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(0, response_ctx_->answers_.size());

  // Validate stats
  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
  EXPECT_TRUE(config_->stats().downstream_rx_bytes_.used());
  EXPECT_TRUE(config_->stats().downstream_tx_bytes_.used());

  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(0, response_ctx_->answers_.size());
}

TEST_F(DnsFilterTest, InvalidLabelNameTooLongTest) {
  InSequence s;

  setup(forward_query_off_config);
  std::string domain(64, 'a');
  domain += ".com";
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(0, response_ctx_->answers_.size());

  // Validate stats
  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
  EXPECT_TRUE(config_->stats().downstream_rx_bytes_.used());
  EXPECT_TRUE(config_->stats().downstream_tx_bytes_.used());

  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(0, response_ctx_->answers_.size());
}

TEST_F(DnsFilterTest, SingleTypeAQuery) {
  InSequence s;

  setup(forward_query_off_config);

  const std::string domain("www.foo3.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(1, response_ctx_->answers_.size());

  // Verify that we have an answer record for the queried domain

  const DnsAnswerRecordPtr& answer = response_ctx_->answers_.find(domain)->second;

  // Verify the address returned
  const std::list<std::string> expected{"10.0.3.1"};

  Utils::verifyAddress(expected, answer);

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().known_domain_queries_.value());
  EXPECT_EQ(1, config_->stats().local_a_record_answers_.value());
  EXPECT_EQ(1, config_->stats().a_record_queries_.value());
  EXPECT_TRUE(config_->stats().downstream_rx_bytes_.used());
  EXPECT_TRUE(config_->stats().downstream_tx_bytes_.used());
}

TEST_F(DnsFilterTest, NoHostForSingleTypeAQuery) {
  InSequence s;

  setup(forward_query_off_config);

  const std::string domain("www.api.foo3.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NAME_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(0, response_ctx_->answers_.size());

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().known_domain_queries_.value());
  EXPECT_EQ(0, config_->stats().local_a_record_answers_.value());
  EXPECT_EQ(1, config_->stats().a_record_queries_.value());
  EXPECT_TRUE(config_->stats().downstream_rx_bytes_.used());
  EXPECT_TRUE(config_->stats().downstream_tx_bytes_.used());
}

TEST_F(DnsFilterTest, RepeatedTypeAQuerySuccess) {
  InSequence s;

  setup(forward_query_off_config);
  constexpr size_t loopCount = 5;
  const std::string domain("www.foo3.com");

  std::list<uint16_t> query_id_list{};

  query_id_list.resize(loopCount);
  for (size_t i = 0; i < loopCount; i++) {

    // Generate a changing, non-zero query ID for each lookup
    const uint16_t query_id = (random_.random() + i) % 0xFFFF + 1;
    const std::string query =
        Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN, query_id);
    ASSERT_FALSE(query.empty());
    sendQueryFromClient("10.0.0.1:1000", query);

    response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);

    const uint16_t response_id = response_ctx_->header_.id;
    auto iter = std::find(query_id_list.begin(), query_id_list.end(), query_id);
    EXPECT_EQ(iter, query_id_list.end());
    EXPECT_TRUE(response_ctx_->parse_status_);

    EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
    EXPECT_EQ(1, response_ctx_->answers_.size());

    // Verify that we have an answer record for the queried domain
    const DnsAnswerRecordPtr& answer = response_ctx_->answers_.find(domain)->second;

    // Verify that the Query ID matches the Response ID
    EXPECT_EQ(query_id, response_id);
    query_id_list.emplace_back(query_id);

    // Verify the address returned
    std::list<std::string> expected{"10.0.3.1"};
    Utils::verifyAddress(expected, answer);
  }

  // Validate stats
  EXPECT_EQ(loopCount, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(loopCount, config_->stats().known_domain_queries_.value());
  EXPECT_EQ(loopCount, config_->stats().local_a_record_answers_.value());
  EXPECT_EQ(loopCount, config_->stats().a_record_queries_.value());
}

TEST_F(DnsFilterTest, LocalTypeAQueryFail) {
  InSequence s;

  setup(forward_query_off_config);
  const std::string query =
      Utils::buildQueryForDomain("www.foo2.com", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  sendQueryFromClient("10.0.0.1:1000", query);
  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NAME_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(0, response_ctx_->answers_.size());

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().known_domain_queries_.value());
  EXPECT_EQ(0, config_->stats().local_a_record_answers_.value());
  EXPECT_EQ(1, config_->stats().a_record_queries_.value());
  EXPECT_EQ(1, config_->stats().unanswered_queries_.value());
}

TEST_F(DnsFilterTest, LocalTypeAAAAQuerySuccess) {
  InSequence s;

  setup(forward_query_off_config);
  std::list<std::string> expected{"2001:8a:c1::2800:7", "2001:8a:c1::2800:8", "2001:8a:c1::2800:9"};
  const std::string domain("www.foo2.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_AAAA, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  sendQueryFromClient("10.0.0.1:1000", query);
  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(expected.size(), response_ctx_->answers_.size());

  // Verify the address returned
  for (const auto& answer : response_ctx_->answers_) {
    EXPECT_EQ(answer.first, domain);
    Utils::verifyAddress(expected, answer.second);
  }

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().known_domain_queries_.value());
  EXPECT_EQ(3, config_->stats().local_aaaa_record_answers_.value());
  EXPECT_EQ(1, config_->stats().aaaa_record_queries_.value());
}

TEST_F(DnsFilterTest, ExternalResolutionReturnSingleAddress) {
  InSequence s;

  auto timeout_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(_, _));

  const std::string expected_address("130.207.244.251");
  const std::string domain("www.foobaz.com");
  setup(forward_query_on_config);

  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  // Verify that we are calling the resolver with the expected name
  Network::DnsResolver::ResolveCb resolve_cb;
  EXPECT_CALL(*resolver_, resolve(domain, _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));

  // Send a query to for a name not in our configuration
  sendQueryFromClient("10.0.0.1:1000", query);

  EXPECT_CALL(*timeout_timer, disableTimer()).Times(AnyNumber());

  // Execute resolve callback
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({expected_address}));

  // parse the result
  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(1, response_ctx_->answers_.size());

  std::list<std::string> expected{expected_address};
  for (const auto& answer : response_ctx_->answers_) {
    EXPECT_EQ(answer.first, domain);
    Utils::verifyAddress(expected, answer.second);
  }

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().external_a_record_queries_.value());
  EXPECT_EQ(1, config_->stats().external_a_record_answers_.value());
  EXPECT_EQ(1, config_->stats().a_record_queries_.value());
  EXPECT_EQ(0, config_->stats().aaaa_record_queries_.value());
  EXPECT_EQ(0, config_->stats().unanswered_queries_.value());

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(resolver_.get()));
}

TEST_F(DnsFilterTest, ExternalResolutionIpv6SingleAddress) {
  InSequence s;

  auto timeout_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(_, _));

  const std::string expected_address("2a04:4e42:d::323");
  const std::string domain("www.foobaz.com");

  setup(forward_query_on_config);

  // Verify that we are calling the resolver with the expected name
  Network::DnsResolver::ResolveCb resolve_cb;
  EXPECT_CALL(*resolver_, resolve(domain, _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));

  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_AAAA, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  // Send a query to for a name not in our configuration
  sendQueryFromClient("10.0.0.1:1000", query);

  EXPECT_CALL(*timeout_timer, disableTimer());

  // Execute resolve callback
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({expected_address}));

  // parse the result
  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(1, response_ctx_->answers_.size());

  std::list<std::string> expected{expected_address};
  for (const auto& answer : response_ctx_->answers_) {
    EXPECT_EQ(answer.first, domain);
    Utils::verifyAddress(expected, answer.second);
  }

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().external_aaaa_record_queries_.value());
  EXPECT_EQ(1, config_->stats().external_aaaa_record_answers_.value());
  EXPECT_EQ(1, config_->stats().aaaa_record_queries_.value());
  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(0, config_->stats().unanswered_queries_.value());

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(resolver_.get()));
}

TEST_F(DnsFilterTest, ExternalResolutionReturnMultipleAddresses) {
  InSequence s;

  auto timeout_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(_, _));

  const std::list<std::string> expected_address{"130.207.244.251", "130.207.244.252",
                                                "130.207.244.253", "130.207.244.254"};
  const std::string domain("www.foobaz.com");
  setup(forward_query_on_config);

  // Verify that we are calling the resolver with the expected name
  Network::DnsResolver::ResolveCb resolve_cb;
  EXPECT_CALL(*resolver_, resolve(domain, _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));

  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  // Send a query to for a name not in our configuration
  sendQueryFromClient("10.0.0.1:1000", query);

  EXPECT_CALL(*timeout_timer, disableTimer());

  // Execute resolve callback
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({expected_address}));

  // parse the result
  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(expected_address.size(), response_ctx_->answers_.size());

  EXPECT_LT(udp_response_.buffer_->length(), Utils::MAX_UDP_DNS_SIZE);

  for (const auto& answer : response_ctx_->answers_) {
    EXPECT_EQ(answer.first, domain);
    Utils::verifyAddress(expected_address, answer.second);
  }

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().external_a_record_queries_.value());
  EXPECT_EQ(expected_address.size(), config_->stats().external_a_record_answers_.value());
  EXPECT_EQ(1, config_->stats().a_record_queries_.value());
  EXPECT_EQ(0, config_->stats().aaaa_record_queries_.value());
  EXPECT_EQ(0, config_->stats().unanswered_queries_.value());

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(resolver_.get()));
}

TEST_F(DnsFilterTest, ExternalResolutionReturnNoAddresses) {
  InSequence s;

  auto timeout_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(_, _));

  const std::string domain("www.foobaz.com");
  setup(forward_query_on_config);

  // Verify that we are calling the resolver with the expected name
  Network::DnsResolver::ResolveCb resolve_cb;
  EXPECT_CALL(*resolver_, resolve(domain, _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));

  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  // Send a query to for a name not in our configuration
  sendQueryFromClient("10.0.0.1:1000", query);

  EXPECT_CALL(*timeout_timer, disableTimer());

  // Execute resolve callback
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success, TestUtility::makeDnsResponse({}));

  // parse the result
  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NAME_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(0, response_ctx_->answers_.size());

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().external_a_record_queries_.value());
  EXPECT_EQ(0, config_->stats().external_a_record_answers_.value());
  EXPECT_EQ(1, config_->stats().a_record_queries_.value());
  EXPECT_EQ(0, config_->stats().aaaa_record_queries_.value());
  EXPECT_EQ(1, config_->stats().unanswered_queries_.value());

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(resolver_.get()));
}

TEST_F(DnsFilterTest, ExternalResolutionTimeout) {
  InSequence s;

  auto timeout_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(_, _));

  const std::string domain("www.foobaz.com");
  setup(forward_query_on_config);

  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  EXPECT_CALL(*resolver_, resolve(domain, _, _)).WillOnce(Return(&resolver_->active_query_));

  // Send a query to for a name not in our configuration
  sendQueryFromClient("10.0.0.1:1000", query);
  simTime().advanceTimeWait(std::chrono::milliseconds(1500));

  // Execute timeout timer callback
  timeout_timer->invokeCallback();

  // parse the result
  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NAME_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(0, response_ctx_->answers_.size());

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().external_a_record_queries_.value());
  EXPECT_EQ(0, config_->stats().external_a_record_answers_.value());
  EXPECT_EQ(1, config_->stats().a_record_queries_.value());
  EXPECT_EQ(0, config_->stats().aaaa_record_queries_.value());
  EXPECT_EQ(1, config_->stats().unanswered_queries_.value());

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(resolver_.get()));
}

TEST_F(DnsFilterTest, ExternalResolutionTimeout2) {
  InSequence s;

  auto timeout_timer = new NiceMock<Event::MockTimer>(&dispatcher_);
  EXPECT_CALL(*timeout_timer, enableTimer(_, _));

  const std::string domain("www.foobaz.com");
  setup(forward_query_on_config);

  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  // Verify that we are calling the resolver with the expected name
  Network::DnsResolver::ResolveCb resolve_cb;
  EXPECT_CALL(*resolver_, resolve(domain, _, _))
      .WillOnce(DoAll(SaveArg<2>(&resolve_cb), Return(&resolver_->active_query_)));

  // Send a query to for a name not in our configuration
  sendQueryFromClient("10.0.0.1:1000", query);
  simTime().advanceTimeWait(std::chrono::milliseconds(1500));

  // Execute timeout timer callback
  timeout_timer->invokeCallback();

  // Execute resolve callback. This should harmlessly return and not alter
  // the response received by the client. Even though we are returning a successful
  // response, the client does not get an answer
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({"130.207.244.251"}));

  // parse the result
  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NAME_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(0, response_ctx_->answers_.size());

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().external_a_record_queries_.value());
  EXPECT_EQ(0, config_->stats().external_a_record_answers_.value());
  EXPECT_EQ(1, config_->stats().a_record_queries_.value());
  EXPECT_EQ(0, config_->stats().aaaa_record_queries_.value());
  EXPECT_EQ(1, config_->stats().unanswered_queries_.value());

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(resolver_.get()));
}

TEST_F(DnsFilterTest, ExternalResolutionExceedMaxPendingLookups) {
  InSequence s;

  const std::string domain("www.foobaz.com");
  setup(forward_query_on_config);
  const std::string query1 =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query1.empty());

  const std::string query2 =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_AAAA, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query2.empty());

  const std::string query3 =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query3.empty());

  // Send the first query. This will remain 'in-flight'
  EXPECT_CALL(dispatcher_, createTimer_(_));
  EXPECT_CALL(*resolver_, resolve(domain, _, _));
  sendQueryFromClient("10.0.0.1:1000", query1);

  // Send the second query. This will remain 'in-flight' also
  EXPECT_CALL(dispatcher_, createTimer_(_));
  EXPECT_CALL(*resolver_, resolve(domain, _, _));
  sendQueryFromClient("10.0.0.1:1000", query2);

  // The third query should be rejected since pending queries (2) > 1, and
  // we've disabled retries. The client will get a response for this single
  // query
  sendQueryFromClient("10.0.0.1:1000", query3);

  // Parse the result for the third query. Since the first two queries are
  // still in flight, the third query is the only one to generate a response
  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(0, response_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NAME_ERROR, response_ctx_->getQueryResponseCode());

  // Validate stats
  EXPECT_EQ(3, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().external_a_record_queries_.value());
  EXPECT_EQ(0, config_->stats().external_a_record_answers_.value());
  EXPECT_EQ(2, config_->stats().a_record_queries_.value());
  EXPECT_EQ(1, config_->stats().aaaa_record_queries_.value());
  EXPECT_EQ(1, config_->stats().unanswered_queries_.value());
}

TEST_F(DnsFilterTest, ConsumeExternalJsonTableTest) {
  InSequence s;

  std::string temp_path =
      TestEnvironment::writeStringToFileForTest("dns_table.json", external_dns_table_json);
  std::string config_to_use = fmt::format(external_dns_table_config, temp_path);
  setup(config_to_use);

  const std::string domain("www.external_foo1.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);

  ASSERT_FALSE(query.empty());
  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(2, response_ctx_->answers_.size());

  // Verify the address returned
  const std::list<std::string> expected{"10.0.0.1", "10.0.0.2"};
  for (const auto& answer : response_ctx_->answers_) {
    EXPECT_EQ(answer.first, domain);
    Utils::verifyAddress(expected, answer.second);
  }

  // Validate stats
  ASSERT_EQ(1, config_->stats().downstream_rx_queries_.value());
  ASSERT_EQ(1, config_->stats().known_domain_queries_.value());
  ASSERT_EQ(2, config_->stats().local_a_record_answers_.value());
  ASSERT_EQ(1, config_->stats().a_record_queries_.value());
}

TEST_F(DnsFilterTest, ConsumeExternalJsonTableTestNoIpv6Answer) {
  InSequence s;

  std::string temp_path =
      TestEnvironment::writeStringToFileForTest("dns_table.json", external_dns_table_json);
  std::string config_to_use = fmt::format(external_dns_table_config, temp_path);
  setup(config_to_use);

  const std::string domain("www.external_foo1.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_AAAA, DNS_RECORD_CLASS_IN);

  ASSERT_FALSE(query.empty());
  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NAME_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(0, response_ctx_->answers_.size());

  // Validate stats
  ASSERT_EQ(1, config_->stats().downstream_rx_queries_.value());
  ASSERT_EQ(1, config_->stats().known_domain_queries_.value());
  ASSERT_EQ(0, config_->stats().local_a_record_answers_.value());
  ASSERT_EQ(0, config_->stats().local_aaaa_record_answers_.value());
  ASSERT_EQ(0, config_->stats().a_record_queries_.value());
  ASSERT_EQ(1, config_->stats().aaaa_record_queries_.value());
}

TEST_F(DnsFilterTest, ConsumeExternalYamlTableTest) {
  InSequence s;

  std::string temp_path =
      TestEnvironment::writeStringToFileForTest("dns_table.yaml", external_dns_table_yaml);
  std::string config_to_use = fmt::format(external_dns_table_config, temp_path);
  setup(config_to_use);

  const std::string domain("www.external_foo1.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);

  ASSERT_FALSE(query.empty());
  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(2, response_ctx_->answers_.size());

  // Verify the address returned
  const std::list<std::string> expected{"10.0.0.1", "10.0.0.2"};
  for (const auto& answer : response_ctx_->answers_) {
    EXPECT_EQ(answer.first, domain);
    Utils::verifyAddress(expected, answer.second);
  }

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().known_domain_queries_.value());
  EXPECT_EQ(2, config_->stats().local_a_record_answers_.value());
  EXPECT_EQ(1, config_->stats().a_record_queries_.value());
}

TEST_F(DnsFilterTest, RawBufferTest) {
  InSequence s;

  setup(forward_query_off_config);
  const std::string domain("www.foo3.com");

  constexpr char dns_request[] = {
      0x36, 0x6b,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x00,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x00,                               // Additional RRs
      0x03, 0x77, 0x77, 0x77, 0x04, 0x66, 0x6f, // Query record for
      0x6f, 0x33, 0x03, 0x63, 0x6f, 0x6d, 0x00, // www.foo3.com
      0x00, 0x01,                               // Query Type - A
      0x00, 0x01,                               // Query Class - IN
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);
  const std::string query = Utils::buildQueryFromBytes(dns_request, count);

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(1, response_ctx_->answers_.size());

  // Verify that we have an answer record for the queried domain
  const DnsAnswerRecordPtr& answer = response_ctx_->answers_.find(domain)->second;

  // Verify the address returned
  const std::list<std::string> expected{"10.0.3.1"};
  Utils::verifyAddress(expected, answer);
}

TEST_F(DnsFilterTest, InvalidAnswersInQueryTest) {
  InSequence s;

  setup(forward_query_off_config);
  const std::string domain("www.foo3.com");

  // Answer count is non-zero in a query.
  constexpr char dns_request[] = {
      0x36, 0x6b,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x01,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x00,                               // Additional RRs
      0x03, 0x77, 0x77, 0x77, 0x04, 0x66, 0x6f, // Query record for
      0x6f, 0x33, 0x03, 0x63, 0x6f, 0x6d, 0x00, // www.foo3.com
      0x00, 0x01,                               // Query Type - A
      0x00, 0x01,                               // Query Class - IN
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);
  const std::string query = Utils::buildQueryFromBytes(dns_request, count);

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(0, response_ctx_->answers_.size());
  EXPECT_EQ(0, config_->stats().queries_with_additional_rrs_.value());
  EXPECT_EQ(1, config_->stats().queries_with_ans_or_authority_rrs_.value());
}

TEST_F(DnsFilterTest, InvalidAuthorityRRsInQueryTest) {
  InSequence s;

  setup(forward_query_off_config);
  const std::string domain("www.foo3.com");

  // Authority RRs count is non-zero in a query.
  constexpr char dns_request[] = {
      0x36, 0x6b,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x00,                               // Answers
      0x00, 0x01,                               // Authority RRs
      0x00, 0x00,                               // Additional RRs
      0x03, 0x77, 0x77, 0x77, 0x04, 0x66, 0x6f, // Query record for
      0x6f, 0x33, 0x03, 0x63, 0x6f, 0x6d, 0x00, // www.foo3.com
      0x00, 0x01,                               // Query Type - A
      0x00, 0x01,                               // Query Class - IN
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);
  const std::string query = Utils::buildQueryFromBytes(dns_request, count);

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(0, response_ctx_->answers_.size());
  EXPECT_EQ(0, config_->stats().queries_with_additional_rrs_.value());
  EXPECT_EQ(1, config_->stats().queries_with_ans_or_authority_rrs_.value());
}

TEST_F(DnsFilterTest, IgnoreAdditionalRRsInQueryTest) {
  InSequence s;

  setup(forward_query_off_config);
  const std::string domain("www.foo3.com");

  // Additional RRs count is non-zero in a query.
  constexpr char dns_request[] = {
      0x36, 0x6b,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x00,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x01,                               // Additional RRs
      0x03, 0x77, 0x77, 0x77, 0x04, 0x66, 0x6f, // Query record for
      0x6f, 0x33, 0x03, 0x63, 0x6f, 0x6d, 0x00, // www.foo3.com
      0x00, 0x01,                               // Query Type - A
      0x00, 0x01,                               // Query Class - IN
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);
  const std::string query = Utils::buildQueryFromBytes(dns_request, count);

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(1, config_->stats().queries_with_additional_rrs_.value());
  EXPECT_EQ(0, config_->stats().queries_with_ans_or_authority_rrs_.value());
}

TEST_F(DnsFilterTest, InvalidQueryNameTest) {
  InSequence s;

  setup(forward_query_off_config);

  // In this buffer the name segment sizes are incorrect. The filter will indicate that the parsing
  // failed
  constexpr char dns_request[] = {
      0x36, 0x6c,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x00,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x00,                               // Additional RRs
      0x02, 0x77, 0x77, 0x77, 0x03, 0x66, 0x6f, // Query record for
      0x6f, 0x33, 0x01, 0x63, 0x6f, 0x6d, 0x00, // www.foo3.com
      0x00, 0x01,                               // Query Type - A
      0x00, 0x01,                               // Query Class - IN
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);
  const std::string query = Utils::buildQueryFromBytes(dns_request, count);

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_ctx_->getQueryResponseCode());

  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
}

TEST_F(DnsFilterTest, InvalidAnswerNameTest) {
  InSequence s;

  // In this buffer the name label is incorrect for the answer. The labels are separated
  // by periods and not the segment length. The filter will indicate that the parsing failed
  constexpr unsigned char dns_request[] = {
      0x36, 0x6b,                               // Transaction ID
      0x81, 0x80,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x01,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x01,                               // Additional RRs
      0x04, 0x69, 0x70, 0x76, 0x36, 0x02, 0x68, // Query record for
      0x65, 0x03, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x01,                               // Record Type
      0x00, 0x01,                               // Record Class
      0x69, 0x70, 0x76, 0x36, 0x2e, 0x68,       // Answer record for
      0x65, 0x2e, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x01,                               // Answer Record Type
      0x00, 0x01,                               // Answer Record Class
      0x00, 0x00, 0x01, 0x19,                   // Answer TTL
      0x00, 0x04,                               // Answer Data Length
      0x42, 0xdc, 0x02, 0x4b,                   // Answer IP Address
      0x00,                                     // Additional RR
      0x00, 0x29, 0x10, 0x00,                   // UDP Payload Size (4096)
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);

  Network::UdpRecvData data{};
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort("10.0.0.1:1000");
  data.addresses_.local_ = listener_address_;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>(dns_request, count);
  data.receive_time_ = MonotonicTime(std::chrono::seconds(0));

  response_ctx_ = ResponseValidator::createResponseContext(data, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);

  // We should have zero parsed answers
  EXPECT_TRUE(response_ctx_->answers_.empty());
}

TEST_F(DnsFilterTest, InvalidAnswerTypeTest) {
  InSequence s;

  // In this buffer the answer type is incorrect for the given query. The answer is a NS
  // type when an A record was requested. This should not happen on the wire.
  constexpr unsigned char dns_request[] = {
      0x36, 0x6b,                               // Transaction ID
      0x81, 0x80,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x01,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x01,                               // Additional RRs
      0x04, 0x69, 0x70, 0x76, 0x36, 0x02, 0x68, // Query record for
      0x65, 0x03, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x01,                               // Record Type
      0x00, 0x01,                               // Record Class
      0x04, 0x69, 0x70, 0x76, 0x36, 0x02, 0x68, // Answer record for
      0x65, 0x03, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x02,                               // Answer Record Type
      0x00, 0x01,                               // Answer Record Class
      0x00, 0x00, 0x01, 0x19,                   // Answer TTL
      0x00, 0x04,                               // Answer Data Length
      0x42, 0xdc, 0x02, 0x4b,                   // Answer IP Address
      0x00,                                     // Additional RR
      0x00, 0x29, 0x10, 0x00,                   // UDP Payload Size (4096)
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);

  Network::UdpRecvData data{};
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort("10.0.0.1:1000");
  data.addresses_.local_ = listener_address_;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>(dns_request, count);
  data.receive_time_ = MonotonicTime(std::chrono::seconds(0));

  response_ctx_ = ResponseValidator::createResponseContext(data, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);

  // We should have zero parsed answers
  EXPECT_TRUE(response_ctx_->answers_.empty());
}

TEST_F(DnsFilterTest, InvalidAnswerClassTest) {
  InSequence s;

  // In this buffer the answer class is incorrect for the given query. The answer is a CH
  // class when an IN class was requested. This should not happen on the wire.
  constexpr unsigned char dns_request[] = {
      0x36, 0x6b,                               // Transaction ID
      0x81, 0x80,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x01,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x01,                               // Additional RRs
      0x04, 0x69, 0x70, 0x76, 0x36, 0x02, 0x68, // Query record for
      0x65, 0x03, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x01,                               // Record Type
      0x00, 0x01,                               // Record Class
      0x04, 0x69, 0x70, 0x76, 0x36, 0x02, 0x68, // Answer record for
      0x65, 0x03, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x01,                               // Answer Record Type
      0x00, 0x03,                               // Answer Record Class
      0x00, 0x00, 0x01, 0x19,                   // Answer TTL
      0x00, 0x04,                               // Answer Data Length
      0x42, 0xdc, 0x02, 0x4b,                   // Answer IP Address
      0x00,                                     // Additional RR
      0x00, 0x29, 0x10, 0x00,                   // UDP Payload Size (4096)
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);

  Network::UdpRecvData data{};
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort("10.0.0.1:1000");
  data.addresses_.local_ = listener_address_;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>(dns_request, count);
  data.receive_time_ = MonotonicTime(std::chrono::seconds(0));

  response_ctx_ = ResponseValidator::createResponseContext(data, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);

  // We should have zero parsed answers
  EXPECT_TRUE(response_ctx_->answers_.empty());
}

TEST_F(DnsFilterTest, InvalidAnswerAddressTest) {
  InSequence s;

  // In this buffer the address in the answer record is invalid. The IP should
  // fail to parse. The class suggests it's an IPv6 address but there are only 4
  // bytes available.
  constexpr unsigned char dns_request[] = {
      0x36, 0x6b,                               // Transaction ID
      0x81, 0x80,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x01,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x01,                               // Additional RRs
      0x04, 0x69, 0x70, 0x76, 0x36, 0x02, 0x68, // Query record for
      0x65, 0x03, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x01,                               // Record Type
      0x00, 0x01,                               // Record Class
      0x04, 0x69, 0x70, 0x76, 0x36, 0x02, 0x68, // Answer record for
      0x65, 0x03, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x1c,                               // Answer Record Type
      0x00, 0x01,                               // Answer Record Class
      0x00, 0x00, 0x01, 0x19,                   // Answer TTL
      0x00, 0x10,                               // Answer Data Length
      0x42, 0xdc, 0x02, 0x4b,                   // Answer IP Address
      0x00,                                     // Additional RR
      0x00, 0x29, 0x10, 0x00,                   // UDP Payload Size (4096)
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);

  Network::UdpRecvData data{};
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort("10.0.0.1:1000");
  data.addresses_.local_ = listener_address_;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>(dns_request, count);
  data.receive_time_ = MonotonicTime(std::chrono::seconds(0));

  setup(forward_query_off_config);
  response_ctx_ = ResponseValidator::createResponseContext(data, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);

  // We should have one parsed query
  EXPECT_FALSE(response_ctx_->queries_.empty());

  // We should have zero parsed answers due to the IP parsing failure
  EXPECT_TRUE(response_ctx_->answers_.empty());
}

TEST_F(DnsFilterTest, InvalidAnswerDataLengthTest) {
  InSequence s;

  // In this buffer the answer data length is invalid (zero). This should not
  // occur in data on the wire.
  constexpr unsigned char dns_request[] = {
      0x36, 0x6b,                               // Transaction ID
      0x81, 0x80,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x01,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x01,                               // Additional RRs
      0x04, 0x69, 0x70, 0x76, 0x36, 0x02, 0x68, // Query record for
      0x65, 0x03, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x01,                               // Record Type
      0x00, 0x01,                               // Record Class
      0x04, 0x69, 0x70, 0x76, 0x36, 0x02, 0x68, // Answer record for
      0x65, 0x03, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x01,                               // Answer Record Type
      0x00, 0x01,                               // Answer Record Class
      0x00, 0x00, 0x01, 0x19,                   // Answer TTL
      0x00, 0x00,                               // Answer Data Length
      0x42, 0xdc, 0x02, 0x4b,                   // Answer IP Address
      0x00,                                     // Additional RR (we do not parse this)
      0x00, 0x29, 0x10, 0x00,                   // UDP Payload Size (4096)
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);

  Network::UdpRecvData data{};
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort("10.0.0.1:1000");
  data.addresses_.local_ = listener_address_;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>(dns_request, count);
  data.receive_time_ = MonotonicTime(std::chrono::seconds(0));

  response_ctx_ = ResponseValidator::createResponseContext(data, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);

  // We should have zero parsed answers
  EXPECT_TRUE(response_ctx_->answers_.empty());
}

TEST_F(DnsFilterTest, TruncatedAnswerRecordTest) {
  InSequence s;

  // In this buffer the answer record is truncated. The filter should indicate
  // a parsing failure
  constexpr unsigned char dns_request[] = {
      0x36, 0x6b,                               // Transaction ID
      0x81, 0x80,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x01,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x00,                               // Additional RRs
      0x04, 0x69, 0x70, 0x76, 0x36, 0x02, 0x68, // Query record for
      0x65, 0x03, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x01,                               // Record Type
      0x00, 0x01,                               // Record Class
      0x04, 0x69, 0x70, 0x76, 0x36, 0x02, 0x68, // Answer record for
      0x65, 0x03, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x01,                               // Answer Record Type
      0x00, 0x01,                               // Answer Record Class
      0x00, 0x00, 0x01, 0x19,                   // Answer TTL
                                                // Remaining data is truncated
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);

  Network::UdpRecvData data{};
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort("10.0.0.1:1000");
  data.addresses_.local_ = listener_address_;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>(dns_request, count);
  data.receive_time_ = MonotonicTime(std::chrono::seconds(0));

  setup(forward_query_off_config);
  response_ctx_ = ResponseValidator::createResponseContext(data, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);

  // We should have one parsed query
  EXPECT_FALSE(response_ctx_->queries_.empty());

  // We should have zero parsed answers due to the IP parsing failure
  EXPECT_TRUE(response_ctx_->answers_.empty());
}

TEST_F(DnsFilterTest, TruncatedQueryBufferTest) {
  InSequence s;

  // In this buffer the query record is truncated. The filter should indicate
  // a parsing failure
  constexpr unsigned char dns_request[] = {
      0x36, 0x6b,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x00,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x01,                               // Additional RRs
      0x04, 0x69, 0x70, 0x76, 0x36, 0x02, 0x68, // Query record for
      0x65, 0x03, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x01                                // Record Type
                                                // Truncated bytes here
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);

  Network::UdpRecvData data{};
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort("10.0.0.1:1000");
  data.addresses_.local_ = listener_address_;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>(dns_request, count);
  data.receive_time_ = MonotonicTime(std::chrono::seconds(0));

  response_ctx_ = ResponseValidator::createResponseContext(data, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);

  // We should have zero parsed answers
  EXPECT_TRUE(response_ctx_->answers_.empty());
}

TEST_F(DnsFilterTest, InvalidQueryClassTypeTest) {
  InSequence s;

  // In this buffer the query class is unsupported.
  constexpr unsigned char dns_request[] = {
      0x36, 0x6b,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x00,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x00,                               // Additional RRs
      0x04, 0x69, 0x70, 0x76, 0x36, 0x02, 0x68, // Query record for
      0x65, 0x03, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x01,                               // Record Type
      0x00, 0x02,                               // Record Class
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);

  Network::UdpRecvData data{};
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort("10.0.0.1:1000");
  data.addresses_.local_ = listener_address_;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>(dns_request, count);
  data.receive_time_ = MonotonicTime(std::chrono::seconds(0));

  response_ctx_ = query_parser_->createQueryContext(data, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);

  // We should have zero parsed queries or answers
  EXPECT_TRUE(response_ctx_->queries_.empty());
  EXPECT_TRUE(response_ctx_->answers_.empty());
}

TEST_F(DnsFilterTest, InsufficientDataforQueryRecord) {
  InSequence s;

  // In this buffer the query data is insufficient.
  constexpr unsigned char dns_request[] = {
      0x36, 0x6b,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x00,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x00,                               // Additional RRs
      0x04, 0x69, 0x70, 0x76, 0x36, 0x02, 0x68, // Query record for
      0x65, 0x03, 0x6e, 0x65, 0x74, 0x00,       // ipv6.he.net
      0x00, 0x01,                               // Record Type
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);

  Network::UdpRecvData data{};
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort("10.0.0.1:1000");
  data.addresses_.local_ = listener_address_;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>(dns_request, count);
  data.receive_time_ = MonotonicTime(std::chrono::seconds(0));

  response_ctx_ = query_parser_->createQueryContext(data, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);

  // We should have zero parsed queries or answers
  EXPECT_TRUE(response_ctx_->queries_.empty());
  EXPECT_TRUE(response_ctx_->answers_.empty());
}

TEST_F(DnsFilterTest, InvalidQueryNameTest2) {
  InSequence s;

  setup(forward_query_off_config);
  // In this buffer the name segment sizes are incorrect. The first segment points
  // past the end of the buffer. The filter will indicate that the parsing failed.
  constexpr char dns_request[] = {
      0x36, 0x6c,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x00,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x00,                               // Additional RRs
      0x4c, 0x77, 0x77, 0x77, 0x03, 0x66, 0x6f, // Query record for
      0x6f, 0x33, 0x01, 0x63, 0x6f, 0x6d, 0x00, // www.foo3.com
      0x00, 0x01,                               // Query Type - A
      0x00, 0x01,                               // Query Class - IN
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);
  const std::string query = Utils::buildQueryFromBytes(dns_request, count);

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_ctx_->getQueryResponseCode());

  // TODO(suniltheta): underflow/overflow stats
  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
}

TEST_F(DnsFilterTest, MultipleQueryCountTest) {
  InSequence s;

  setup(forward_query_off_config);
  // In this buffer we have 2 queries for two different domains. This is a rare case
  // and serves to validate that we handle the protocol correctly. We will return an
  // error to the client since most implementations will send the two questions as two
  // separate DNS queries
  constexpr char dns_request[] = {
      0x36, 0x6d,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x02,                               // Questions
      0x00, 0x00,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x00,                               // Additional RRs
      0x03, 0x77, 0x77, 0x77, 0x04, 0x66, 0x6f, // begin query record for
      0x6f, 0x33, 0x03, 0x63, 0x6f, 0x6d, 0x00, // www.foo3.com
      0x00, 0x01,                               // Query Type - A
      0x00, 0x01,                               // Query Class - IN
      0x03, 0x77, 0x77, 0x77, 0x04, 0x66, 0x6f, // Query record for
      0x6f, 0x31, 0x03, 0x63, 0x6f, 0x6d, 0x00, // www.foo1.com
      0x00, 0x01,                               // Query Type - A
      0x00, 0x01,                               // Query Class - IN
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);
  const std::string query = Utils::buildQueryFromBytes(dns_request, count);

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_ctx_->getQueryResponseCode());

  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(0, response_ctx_->answers_.size());
}

TEST_F(DnsFilterTest, InvalidQueryCountTest) {
  InSequence s;

  setup(forward_query_off_config);
  // In this buffer the Questions count is zero. This is an invalid query and is handled as such.
  constexpr char dns_request[] = {
      0x36, 0x6f,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x00,                               // Questions
      0x00, 0x00,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x00,                               // Additional RRs
      0x03, 0x77, 0x77, 0x77, 0x04, 0x66, 0x6f, // Query record for
      0x6f, 0x33, 0x03, 0x63, 0x6f, 0x6d, 0x00, // www.foo3.com
      0x00, 0x01,                               // Query Type - A
      0x00, 0x01,                               // Query Class - IN
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);
  const std::string query = Utils::buildQueryFromBytes(dns_request, count);

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_ctx_->getQueryResponseCode());

  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
  EXPECT_EQ(0, response_ctx_->answers_.size());
}

TEST_F(DnsFilterTest, InvalidNameLabelTest) {
  InSequence s;

  setup(forward_query_off_config);
  // In this buffer the name label is not formatted as the RFC specifies. The
  // label separators are periods and not the label length
  constexpr char dns_request[] = {
      0x36, 0x6f,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x00,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x00,                               // Additional RRs
      0x77, 0x77, 0x77, 0x2e, 0x66, 0x6f, 0x6f, // Query record for
      0x33, 0x2e, 0x63, 0x6f, 0x6d, 0x00,       // www.foo3.com
      0x00, 0x01,                               // Query Type - A
      0x00, 0x01,                               // Query Class - IN
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);
  const std::string query = Utils::buildQueryFromBytes(dns_request, count);

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_ctx_->getQueryResponseCode());

  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
  EXPECT_EQ(0, response_ctx_->answers_.size());
}

TEST_F(DnsFilterTest, NotImplementedQueryTest) {
  InSequence s;

  setup(forward_query_off_config);
  // This buffer requests a CNAME record which we do not support. We respond to the client with a
  // "not implemented" response code
  constexpr char dns_request[] = {
      0x36, 0x70,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x00,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x00,                               // Additional RRs
      0x03, 0x77, 0x77, 0x77, 0x04, 0x66, 0x6f, // Query record for
      0x6f, 0x33, 0x03, 0x63, 0x6f, 0x6d, 0x00, // www.foo3.com
      0x00, 0x05,                               // Query Type - CNAME
      0x00, 0x01,                               // Query Class - IN
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);
  const std::string query = Utils::buildQueryFromBytes(dns_request, count);

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NOT_IMPLEMENTED, response_ctx_->getQueryResponseCode());

  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(0, config_->stats().downstream_rx_invalid_queries_.value());
}

TEST_F(DnsFilterTest, NoTransactionIdTest) {
  InSequence s;

  setup(forward_query_off_config);
  // This buffer has an invalid Transaction ID. We should return an error
  // to the client
  constexpr char dns_request[] = {
      0x00, 0x00,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x01,                               // Questions
      0x00, 0x00,                               // Answers
      0x00, 0x00,                               // Authority RRs
      0x00, 0x00,                               // Additional RRs
      0x03, 0x77, 0x77, 0x77, 0x04, 0x66, 0x6f, // Query record for
      0x6f, 0x33, 0x03, 0x63, 0x6f, 0x6d, 0x00, // www.foo3.com
      0x00, 0x05,                               // Query Type - CNAME
      0x00, 0x01,                               // Query Class - IN
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);
  const std::string query = Utils::buildQueryFromBytes(dns_request, count);

  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_ctx_->getQueryResponseCode());
}

TEST_F(DnsFilterTest, InvalidShortBufferTest) {
  InSequence s;

  setup(forward_query_off_config);
  // This is an invalid query. Envoy should handle the packet and indicate a parsing failure
  constexpr char dns_request[] = {0x1c};
  const std::string query = Utils::buildQueryFromBytes(dns_request, 1);
  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_FALSE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_ctx_->getQueryResponseCode());

  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
}

TEST_F(DnsFilterTest, RandomizeFirstAnswerTest) {
  InSequence s;

  setup(forward_query_off_config);
  const std::string domain("www.foo16.com");

  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());
  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());

  // Although 16 addresses are defined, only 8 are returned
  EXPECT_EQ(8, response_ctx_->answers_.size());

  // We shuffle the list of addresses when we read the config, and in the case of more than
  // 8 defined addresses, we randomize the initial starting index. We should not end up with
  // the first answer being the first defined address, or the answers appearing in the same
  // order as they are defined.
  const std::list<std::string> defined_order{"10.0.16.1", "10.0.16.2", "10.0.16.3", "10.0.16.4",
                                             "10.0.16.5", "10.0.16.6", "10.0.16.7", "10.0.16.8"};
  auto defined_answer_iter = defined_order.begin();
  for (const auto& answer : response_ctx_->answers_) {
    const auto resolved_address = answer.second->ip_addr_->ip()->addressAsString();
    EXPECT_NE(0L, resolved_address.compare(*defined_answer_iter++));
  }
}

TEST_F(DnsFilterTest, ConsumeExternalTableWithServicesTest) {
  InSequence s;

  std::string temp_path =
      TestEnvironment::writeStringToFileForTest("dns_table.yaml", external_dns_table_services_yaml);
  std::string config_to_use = fmt::format(external_dns_table_config, temp_path);
  setup(config_to_use);

  const std::string service("_sip._tcp.voip.subzero.com");

  const std::string query =
      Utils::buildQueryForDomain(service, DNS_RECORD_TYPE_SRV, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());
  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());

  std::map<uint16_t, std::string> validation_weight_map = {
      {10, "backup.voip.subzero.com"},
      {20, "secondary.voip.subzero.com"},
      {30, "primary.voip.subzero.com"},
      {40, "emergency.voip.subzero.com"},
  };

  std::map<uint16_t, std::string> validation_port_map = {
      {5062, "backup.voip.subzero.com"},
      {5061, "secondary.voip.subzero.com"},
      {5060, "primary.voip.subzero.com"},
      {5063, "emergency.voip.subzero.com"},
  };

  // Validate the weight for each SRV record. The TTL and priority are the same value for each
  // entry
  EXPECT_EQ(validation_weight_map.size(), response_ctx_->answers_.size());
  for (const auto& answer : response_ctx_->answers_) {
    EXPECT_EQ(answer.second->type_, DNS_RECORD_TYPE_SRV);

    DnsSrvRecord* srv_rec = dynamic_cast<DnsSrvRecord*>(answer.second.get());

    EXPECT_STREQ("_sip._tcp.voip.subzero.com", srv_rec->name_.c_str());
    EXPECT_EQ(86400, srv_rec->ttl_.count());

    EXPECT_EQ(1, srv_rec->targets_.size());
    const auto target = srv_rec->targets_.begin();
    const auto target_name = target->first;
    const auto& attributes = target->second;

    EXPECT_EQ(10, attributes.priority);
    auto expected_target = validation_weight_map[attributes.weight];
    EXPECT_EQ(expected_target, target_name);

    auto port_entry = validation_port_map[attributes.port];
    EXPECT_EQ(expected_target, port_entry);
  }

  // Validate additional records from the SRV query. Remove a matching
  // entry to ensure that we are getting unique addresses in the additional
  // records
  std::map<std::string, std::string> target_map = {
      {"primary.voip.subzero.com", "10.0.3.1"},
      {"secondary.voip.subzero.com", "10.0.3.2"},
      {"backup.voip.subzero.com", "10.0.3.3"},
      {"emergency.voip.subzero.com", "2200:823f::cafe:beef"},
  };
  const size_t target_size = target_map.size();

  EXPECT_EQ(target_map.size(), response_ctx_->additional_.size());
  for (const auto& [hostname, address] : response_ctx_->additional_) {
    const auto& entry = target_map.find(hostname);
    EXPECT_NE(entry, target_map.end());
    Utils::verifyAddress({entry->second}, address);
    target_map.erase(hostname);
  }

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().known_domain_queries_.value());
  EXPECT_EQ(target_size, config_->stats().local_srv_record_answers_.value());
  EXPECT_EQ(target_size - 1, config_->stats().local_a_record_answers_.value());
  EXPECT_EQ(1, config_->stats().local_aaaa_record_answers_.value());
  EXPECT_EQ(1, config_->stats().srv_record_queries_.value());
}

TEST_F(DnsFilterTest, SrvTargetResolution) {
  InSequence s;

  std::string temp_path =
      TestEnvironment::writeStringToFileForTest("dns_table.yaml", external_dns_table_services_yaml);
  std::string config_to_use = fmt::format(external_dns_table_config, temp_path);
  setup(config_to_use);

  struct RecordProperties {
    uint16_t type;
    std::string address;
  };

  const std::map<std::string, struct RecordProperties> target_map = {
      {"primary.voip.subzero.com", {DNS_RECORD_TYPE_A, "10.0.3.1"}},
      {"secondary.voip.subzero.com", {DNS_RECORD_TYPE_A, "10.0.3.2"}},
      {"backup.voip.subzero.com", {DNS_RECORD_TYPE_A, "10.0.3.3"}},
      {"emergency.voip.subzero.com", {DNS_RECORD_TYPE_AAAA, "2200:823f::cafe:beef"}},
  };

  for (const auto& [domain, properties] : target_map) {
    const uint16_t address_type = properties.type;
    const std::string& ip = properties.address;

    const std::string query = Utils::buildQueryForDomain(domain, address_type, DNS_RECORD_CLASS_IN);
    ASSERT_FALSE(query.empty());
    sendQueryFromClient("10.0.0.1:1000", query);

    response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
    EXPECT_TRUE(response_ctx_->parse_status_);
    EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());
    EXPECT_EQ(1, response_ctx_->answers_.size());

    const DnsAnswerRecordPtr& answer = response_ctx_->answers_.find(domain)->second;
    Utils::verifyAddress({ip}, answer);
  }

  // Validate stats
  EXPECT_EQ(target_map.size(), config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(target_map.size(), config_->stats().known_domain_queries_.value());
  EXPECT_EQ(target_map.size() - 1, config_->stats().local_a_record_answers_.value());
  EXPECT_EQ(1, config_->stats().local_aaaa_record_answers_.value());
  EXPECT_EQ(target_map.size() - 1, config_->stats().a_record_queries_.value());
  EXPECT_EQ(1, config_->stats().aaaa_record_queries_.value());
}

TEST_F(DnsFilterTest, NonExistentClusterServiceLookup) {
  InSequence s;

  std::string temp_path =
      TestEnvironment::writeStringToFileForTest("dns_table.yaml", external_dns_table_services_yaml);
  std::string config_to_use = fmt::format(external_dns_table_config, temp_path);
  setup(config_to_use);

  const std::string service("_http._tcp.web.subzero.com");

  const std::string query =
      Utils::buildQueryForDomain(service, DNS_RECORD_TYPE_SRV, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());
  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NAME_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(0, response_ctx_->answers_.size());

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().known_domain_queries_.value());
  EXPECT_EQ(0, config_->stats().local_srv_record_answers_.value());
  EXPECT_EQ(1, config_->stats().srv_record_queries_.value());
}

TEST_F(DnsFilterTest, SrvRecordQuery) {
  InSequence s;

  setup(forward_query_off_config);
  // This buffer requests a SRV record
  constexpr char dns_request[] = {
      0x32, 0x6e,             // Transaction ID
      0x01, 0x00,             // Flags
      0x00, 0x01,             // Questions
      0x00, 0x00,             // Answers
      0x00, 0x00,             // Authority RRs
      0x00, 0x00,             // Additional RRs
      0x05, 0x5f, 0x6c, 0x64, // SRV query for
      0x61, 0x70, 0x04, 0x5f, // _ldap._tcp.Default-First-Site-Name._sites.dc._msdcs.utelsystems.local
      0x74, 0x63, 0x70, 0x17, 0x44, 0x65, 0x66, 0x61, 0x75, 0x6c, 0x74, 0x2d, 0x46, 0x69,
      0x72, 0x73, 0x74, 0x2d, 0x53, 0x69, 0x74, 0x65, 0x2d, 0x4e, 0x61, 0x6d, 0x65, 0x06,
      0x5f, 0x73, 0x69, 0x74, 0x65, 0x73, 0x02, 0x64, 0x63, 0x06, 0x5f, 0x6d, 0x73, 0x64,
      0x63, 0x73, 0x0b, 0x75, 0x74, 0x65, 0x6c, 0x73, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x73,
      0x05, 0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x00, 0x00, 0x21, // Type - SRV (0x21 -> 33)
      0x00, 0x01                                            // Class - IN
  };

  constexpr size_t count = sizeof(dns_request) / sizeof(dns_request[0]);
  const std::string query = Utils::buildQueryFromBytes(dns_request, count);
  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NAME_ERROR, response_ctx_->getQueryResponseCode());
  EXPECT_EQ(1, response_ctx_->queries_.size());

  const auto& parsed_query = response_ctx_->queries_.front();
  EXPECT_EQ(parsed_query->type_, DNS_RECORD_TYPE_SRV);
  EXPECT_STREQ("_ldap._tcp.Default-First-Site-Name._sites.dc._msdcs.utelsystems.local",
               parsed_query->name_.c_str());

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(0, config_->stats().known_domain_queries_.value());
  EXPECT_EQ(0, config_->stats().local_srv_record_answers_.value());
  EXPECT_EQ(1, config_->stats().srv_record_queries_.value());
}

TEST_F(DnsFilterTest, SrvQueryMaxRecords) {
  InSequence s;

  std::string temp_path =
      TestEnvironment::writeStringToFileForTest("dns_table.yaml", max_records_table_yaml);
  std::string config_to_use = fmt::format(external_dns_table_config, temp_path);
  setup(config_to_use);

  const std::string service{"_http._tcp.web.ermac.com"};
  const std::string query =
      Utils::buildQueryForDomain(service, DNS_RECORD_TYPE_SRV, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());
  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());

  // We can only serialize 7 records before reaching the 512 byte limit
  EXPECT_LT(response_ctx_->answers_.size(), MAX_RETURNED_RECORDS);
  EXPECT_LT(response_ctx_->additional_.size(), MAX_RETURNED_RECORDS);

  const std::list<std::string> hosts{
      "one.web.ermac.com",  "two.web.ermac.com", "three.web.ermac.com", "four.web.ermac.com",
      "five.web.ermac.com", "six.web.ermac.com", "seven.web.ermac.com",
  };

  // Verify the service name and targets are sufficiently randomized
  size_t exact_matches = 0;
  auto host = hosts.begin();
  for (const auto& answer : response_ctx_->answers_) {
    EXPECT_EQ(answer.second->type_, DNS_RECORD_TYPE_SRV);
    DnsSrvRecord* srv_rec = dynamic_cast<DnsSrvRecord*>(answer.second.get());

    EXPECT_STREQ(service.c_str(), srv_rec->name_.c_str());

    const auto target = srv_rec->targets_.begin();
    const auto target_name = target->first;
    exact_matches += (target_name == *host++);
  }
  EXPECT_LT(exact_matches, hosts.size());

  // Verify that the additional records are not in the same order as the configuration
  exact_matches = 0;
  host = hosts.begin();
  for (const auto& answer : response_ctx_->additional_) {
    exact_matches += (answer.first == *host++);
  }
  EXPECT_LT(exact_matches, hosts.size());
}

TEST_F(DnsFilterTest, DnsResolverOptionsNotSet) {
  InSequence s;

  std::string temp_path =
      TestEnvironment::writeStringToFileForTest("dns_table.yaml", max_records_table_yaml);
  std::string config_to_use = fmt::format(dns_resolver_options_config_not_set, temp_path);
  setup(config_to_use);
  // `false` here means use_tcp_for_dns_lookups is not set via dns filter config
  EXPECT_EQ(false, dns_resolver_options_.use_tcp_for_dns_lookups());
  // `false` here means no_default_search_domain is not set via dns filter config
  EXPECT_EQ(false, dns_resolver_options_.no_default_search_domain());
}

TEST_F(DnsFilterTest, DnsResolverOptionsSetTrue) {
  InSequence s;

  std::string temp_path =
      TestEnvironment::writeStringToFileForTest("dns_table.yaml", max_records_table_yaml);
  std::string config_to_use = fmt::format(dns_resolver_options_config_set_true, temp_path);
  setup(config_to_use);

  // `true` here means use_tcp_for_dns_lookups is set true
  EXPECT_EQ(true, dns_resolver_options_.use_tcp_for_dns_lookups());
  // `true` here means no_default_search_domain is set true
  EXPECT_EQ(true, dns_resolver_options_.no_default_search_domain());
}

TEST_F(DnsFilterTest, DnsResolverOptionsSetFalse) {
  InSequence s;

  std::string temp_path =
      TestEnvironment::writeStringToFileForTest("dns_table.yaml", max_records_table_yaml);
  std::string config_to_use = fmt::format(dns_resolver_options_config_set_false, temp_path);
  setup(config_to_use);

  // `false` here means use_tcp_for_dns_lookups is set true
  EXPECT_EQ(false, dns_resolver_options_.use_tcp_for_dns_lookups());
  // `false` here means no_default_search_domain is set true
  EXPECT_EQ(false, dns_resolver_options_.no_default_search_domain());
}

TEST_F(DnsFilterTest, DEPRECATED_FEATURE_TEST(DnsResolutionConfigExist)) {
  constexpr absl::string_view dns_resolution_config_exist = R"EOF(
stat_prefix: "my_prefix"
client_config:
  resolver_timeout: 1s
  dns_resolution_config:
    dns_resolver_options:
      use_tcp_for_dns_lookups: false
      no_default_search_domain: false
    resolvers:
    - socket_address:
        address: "1.1.1.1"
        port_value: 53
  max_pending_lookups: 256
server_config:
  external_dns_table:
    filename: {}
)EOF";

  InSequence s;

  std::string temp_path =
      TestEnvironment::writeStringToFileForTest("dns_table.yaml", max_records_table_yaml);
  std::string config_to_use = fmt::format(dns_resolution_config_exist, temp_path);
  setup(config_to_use);

  EXPECT_EQ(false, dns_resolver_options_.use_tcp_for_dns_lookups());
  EXPECT_EQ(false, dns_resolver_options_.no_default_search_domain());

  // address matches
  auto resolvers = envoy::config::core::v3::Address();
  resolvers.mutable_socket_address()->set_address("1.1.1.1");
  resolvers.mutable_socket_address()->set_port_value(53);

  EXPECT_EQ(true, TestUtility::protoEqual(cares_.resolvers(0), resolvers));
}

// test typed_dns_resolver_config exits which overrides dns_resolution_config.
TEST_F(DnsFilterTest, DEPRECATED_FEATURE_TEST(TypedDnsResolverConfigOverrideDnsResolutionConfig)) {
  constexpr absl::string_view typed_dns_resolver_config_exist = R"EOF(
stat_prefix: "my_prefix"
client_config:
  resolver_timeout: 1s
  dns_resolution_config:
    dns_resolver_options:
      use_tcp_for_dns_lookups: false
      no_default_search_domain: false
    resolvers:
    - socket_address:
        address: "1.1.1.1"
        port_value: 53
  typed_dns_resolver_config:
    name: envoy.network.dns_resolver.cares
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig
      resolvers:
      - socket_address:
          address: "1.2.3.4"
          port_value: 80
      dns_resolver_options:
        use_tcp_for_dns_lookups: true
        no_default_search_domain: true
  max_pending_lookups: 256
server_config:
  external_dns_table:
    filename: {}
)EOF";

  InSequence s;

  std::string temp_path =
      TestEnvironment::writeStringToFileForTest("dns_table.yaml", max_records_table_yaml);
  std::string config_to_use = fmt::format(typed_dns_resolver_config_exist, temp_path);
  setup(config_to_use);

  EXPECT_EQ(true, dns_resolver_options_.use_tcp_for_dns_lookups());
  EXPECT_EQ(true, dns_resolver_options_.no_default_search_domain());

  // address matches
  auto resolvers = envoy::config::core::v3::Address();
  resolvers.mutable_socket_address()->set_address("1.2.3.4");
  resolvers.mutable_socket_address()->set_port_value(80);
  EXPECT_EQ(true, TestUtility::protoEqual(cares_.resolvers(0), resolvers));
}

// test typed_dns_resolver_config exits.
TEST_F(DnsFilterTest, TypedDnsResolverConfigExist) {
  constexpr absl::string_view typed_dns_resolver_config_exist = R"EOF(
stat_prefix: "my_prefix"
client_config:
  resolver_timeout: 1s
  typed_dns_resolver_config:
    name: envoy.network.dns_resolver.cares
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig
      resolvers:
      - socket_address:
          address: "1.2.3.4"
          port_value: 80
      dns_resolver_options:
        use_tcp_for_dns_lookups: true
        no_default_search_domain: true
  max_pending_lookups: 256
server_config:
  external_dns_table:
    filename: {}
)EOF";

  InSequence s;

  std::string temp_path =
      TestEnvironment::writeStringToFileForTest("dns_table.yaml", max_records_table_yaml);
  std::string config_to_use = fmt::format(typed_dns_resolver_config_exist, temp_path);
  setup(config_to_use);

  EXPECT_EQ(true, dns_resolver_options_.use_tcp_for_dns_lookups());
  EXPECT_EQ(true, dns_resolver_options_.no_default_search_domain());

  // address matches
  auto resolvers = envoy::config::core::v3::Address();
  resolvers.mutable_socket_address()->set_address("1.2.3.4");
  resolvers.mutable_socket_address()->set_port_value(80);
  EXPECT_EQ(true, TestUtility::protoEqual(cares_.resolvers(0), resolvers));
}

// test when no DNS related config exists, an empty typed_dns_resolver_config is the parameter.
TEST_F(DnsFilterTest, NoDnsConfigExist) {
  constexpr absl::string_view no_dns_config_exist = R"EOF(
stat_prefix: "my_prefix"
client_config:
  resolver_timeout: 1s
  max_pending_lookups: 256
server_config:
  external_dns_table:
    filename: {}
)EOF";

  InSequence s;

  std::string temp_path =
      TestEnvironment::writeStringToFileForTest("dns_table.yaml", max_records_table_yaml);
  std::string config_to_use = fmt::format(no_dns_config_exist, temp_path);
  setup(config_to_use);

  EXPECT_EQ(false, dns_resolver_options_.use_tcp_for_dns_lookups());
  EXPECT_EQ(false, dns_resolver_options_.no_default_search_domain());

  // No address
  EXPECT_EQ(0, cares_.resolvers().size());
}

// Verify downstream send and receive error handling.
TEST_F(DnsFilterTest, SendReceiveErrorHandling) {
  InSequence s;

  setup(forward_query_off_config);

  filter_->onReceiveError(Api::IoError::IoErrorCode::UnknownError);
  EXPECT_EQ(1, config_->stats().downstream_rx_errors_.value());
}

TEST_F(DnsFilterTest, DEPRECATED_FEATURE_TEST(DeprecatedKnownSuffixes)) {
  InSequence s;

  const std::string config_using_known_suffixes = R"EOF(
stat_prefix: "my_prefix"
server_config:
  inline_dns_table:
    external_retry_count: 0
    known_suffixes:
    - suffix: "foo1.com"
    virtual_domains:
      - name: "www.foo1.com"
        endpoint:
          address_list:
            address:
            - "10.0.0.1"
)EOF";
  setup(config_using_known_suffixes);

  const std::string query =
      Utils::buildQueryForDomain("www.foo1.com", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());
  sendQueryFromClient("10.0.0.1:1000", query);

  response_ctx_ = ResponseValidator::createResponseContext(udp_response_, counters_);
  EXPECT_TRUE(response_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_ctx_->getQueryResponseCode());

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().known_domain_queries_.value());
}

} // namespace
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

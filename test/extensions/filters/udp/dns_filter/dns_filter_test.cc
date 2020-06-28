#include "envoy/extensions/filters/udp/dns_filter/v3alpha/dns_filter.pb.h"
#include "envoy/extensions/filters/udp/dns_filter/v3alpha/dns_filter.pb.validate.h"

#include "common/common/logger.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"

#include "dns_filter_test_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AnyNumber;
using testing::AtLeast;
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

Api::IoCallUint64Result makeNoError(uint64_t rc) {
  auto no_error = Api::ioCallUint64ResultNoError();
  no_error.rc_ = rc;
  return no_error;
}

class DnsFilterTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  DnsFilterTest()
      : listener_address_(Network::Utility::parseInternetAddressAndPort("127.0.2.1:5353")),
        api_(Api::createApiForTest()),
        counters_(mock_query_buffer_underflow_, mock_record_name_overflow_,
                  query_parsing_failure_) {
    udp_response_.addresses_.local_ = listener_address_;
    udp_response_.addresses_.peer_ = listener_address_;
    udp_response_.buffer_ = std::make_unique<Buffer::OwnedImpl>();

    setupResponseParser();
    EXPECT_CALL(callbacks_, udpListener()).Times(AtLeast(0));
    EXPECT_CALL(callbacks_.udp_listener_, send(_))
        .WillRepeatedly(
            Invoke([this](const Network::UdpSendData& send_data) -> Api::IoCallUint64Result {
              udp_response_.buffer_->move(send_data.buffer_);
              return makeNoError(udp_response_.buffer_->length());
            }));
    EXPECT_CALL(callbacks_.udp_listener_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
  }

  ~DnsFilterTest() override { EXPECT_CALL(callbacks_.udp_listener_, onDestroy()); }

  void setupResponseParser() {
    histogram_.unit_ = Stats::Histogram::Unit::Milliseconds;
    response_parser_ = std::make_unique<DnsMessageParser>(
        true /* recursive queries */, api_->timeSource(), 0 /* retries */, random_, histogram_);
  }

  void setup(const std::string& yaml) {
    envoy::extensions::filters::udp::dns_filter::v3alpha::DnsFilterConfig config;
    TestUtility::loadFromYamlAndValidate(yaml, config);
    auto store = stats_store_.createScope("dns_scope");
    ON_CALL(listener_factory_, scope()).WillByDefault(ReturnRef(*store));
    ON_CALL(listener_factory_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(random_, random()).WillByDefault(Return(3));
    ON_CALL(listener_factory_, random()).WillByDefault(ReturnRef(random_));

    resolver_ = std::make_shared<Network::MockDnsResolver>();
    ON_CALL(dispatcher_, createDnsResolver(_, _)).WillByDefault(Return(resolver_));

    config_ = std::make_shared<DnsFilterEnvoyConfig>(listener_factory_, config);
    filter_ = std::make_unique<DnsFilter>(callbacks_, config_);
  }

  void sendQueryFromClient(const std::string& peer_address, const std::string& buffer) {
    Network::UdpRecvData data;
    data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPort(peer_address);
    data.addresses_.local_ = listener_address_;
    data.buffer_ = std::make_unique<Buffer::OwnedImpl>(buffer);
    data.receive_time_ = MonotonicTime(std::chrono::seconds(0));
    filter_->onData(data);
  }

  const Network::Address::InstanceConstSharedPtr listener_address_;
  Api::ApiPtr api_;
  DnsFilterEnvoyConfigSharedPtr config_;
  NiceMock<Stats::MockCounter> mock_query_buffer_underflow_;
  NiceMock<Stats::MockCounter> mock_record_name_overflow_;
  NiceMock<Stats::MockCounter> query_parsing_failure_;
  DnsParserCounters counters_;
  DnsQueryContextPtr query_ctx_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Network::MockUdpReadFilterCallbacks callbacks_;
  Network::UdpRecvData udp_response_;
  NiceMock<Filesystem::MockInstance> file_system_;
  NiceMock<Stats::MockHistogram> histogram_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<Server::Configuration::MockListenerFactoryContext> listener_factory_;
  Stats::IsolatedStoreImpl stats_store_;
  std::shared_ptr<Network::MockDnsResolver> resolver_;
  std::unique_ptr<DnsFilter> filter_;
  std::unique_ptr<DnsMessageParser> response_parser_;

  const std::string forward_query_off_config = R"EOF(
stat_prefix: "my_prefix"
server_config:
  inline_dns_table:
    external_retry_count: 3
    known_suffixes:
    - suffix: foo1.com
    - suffix: foo2.com
    - suffix: foo3.com
    - suffix: foo16.com
    - suffix: thisismydomainforafivehundredandtwelvebytetest.com
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
  upstream_resolvers:
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
    known_suffixes:
    - suffix: foo1.com
    - suffix: foo2.com
    virtual_domains:
      - name: "www.foo1.com"
        endpoint:
          address_list:
            address:
            - "10.0.0.1"
)EOF";

  const std::string external_dns_table_config = R"EOF(
stat_prefix: "my_prefix"
client_config:
  resolver_timeout: 1s
  upstream_resolvers:
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
  "known_suffixes": [ { "suffix": "com" } ],
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
known_suffixes:
  - suffix: "com"
virtual_domains:
  - name: "www.external_foo1.com"
    endpoint:
      address_list:
        address:
        - "10.0.0.1"
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
};

TEST_F(DnsFilterTest, InvalidQuery) {
  InSequence s;

  setup(forward_query_off_config);
  sendQueryFromClient("10.0.0.1:1000", "hello");
  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_FALSE(query_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(0, query_ctx_->answers_.size());

  // Validate stats
  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
  EXPECT_TRUE(config_->stats().downstream_rx_bytes_.used());
  EXPECT_TRUE(config_->stats().downstream_tx_bytes_.used());

  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(0, query_ctx_->answers_.size());
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  // There are 8 addresses, however, since the domain is part of the answer record, each
  // serialized answer is over 100 bytes in size, there is room for 3 before the next
  // serialized answer puts the buffer over the 512 byte limit. The query itself is also
  // around 100 bytes.
  EXPECT_EQ(3, query_ctx_->answers_.size());

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

  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_FALSE(query_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(0, query_ctx_->answers_.size());

  // Validate stats
  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
  EXPECT_TRUE(config_->stats().downstream_rx_bytes_.used());
  EXPECT_TRUE(config_->stats().downstream_tx_bytes_.used());

  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(0, query_ctx_->answers_.size());
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_FALSE(query_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(0, query_ctx_->answers_.size());

  // Validate stats
  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
  EXPECT_TRUE(config_->stats().downstream_rx_bytes_.used());
  EXPECT_TRUE(config_->stats().downstream_tx_bytes_.used());

  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(0, query_ctx_->answers_.size());
}

TEST_F(DnsFilterTest, SingleTypeAQuery) {
  InSequence s;

  setup(forward_query_off_config);

  const std::string domain("www.foo3.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  sendQueryFromClient("10.0.0.1:1000", query);

  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(1, query_ctx_->answers_.size());

  // Verify that we have an answer record for the queried domain

  const DnsAnswerRecordPtr& answer = query_ctx_->answers_.find(domain)->second;

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

TEST_F(DnsFilterTest, RepeatedTypeAQuerySuccess) {
  InSequence s;

  setup(forward_query_off_config);
  constexpr size_t loopCount = 5;
  const std::string domain("www.foo3.com");
  size_t total_query_bytes = 0;

  for (size_t i = 0; i < loopCount; i++) {
    const std::string query =
        Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
    total_query_bytes += query.size();
    ASSERT_FALSE(query.empty());
    sendQueryFromClient("10.0.0.1:1000", query);

    query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
    EXPECT_TRUE(query_ctx_->parse_status_);

    EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
    EXPECT_EQ(1, query_ctx_->answers_.size());

    // Verify that we have an answer record for the queried domain
    const DnsAnswerRecordPtr& answer = query_ctx_->answers_.find(domain)->second;

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
  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NAME_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(0, query_ctx_->answers_.size());

  // Validate stats
  EXPECT_EQ(1, config_->stats().downstream_rx_queries_.value());
  EXPECT_EQ(1, config_->stats().known_domain_queries_.value());
  EXPECT_EQ(3, config_->stats().local_a_record_answers_.value());
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
  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(expected.size(), query_ctx_->answers_.size());

  // Verify the address returned
  for (const auto& answer : query_ctx_->answers_) {
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
  EXPECT_CALL(*timeout_timer, enableTimer(_, _)).Times(1);

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
  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(1, query_ctx_->answers_.size());

  std::list<std::string> expected{expected_address};
  for (const auto& answer : query_ctx_->answers_) {
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
  EXPECT_CALL(*timeout_timer, enableTimer(_, _)).Times(1);

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

  EXPECT_CALL(*timeout_timer, disableTimer()).Times(1);

  // Execute resolve callback
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({expected_address}));

  // parse the result
  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(1, query_ctx_->answers_.size());

  std::list<std::string> expected{expected_address};
  for (const auto& answer : query_ctx_->answers_) {
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
  EXPECT_CALL(*timeout_timer, enableTimer(_, _)).Times(1);

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

  EXPECT_CALL(*timeout_timer, disableTimer()).Times(1);

  // Execute resolve callback
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success,
             TestUtility::makeDnsResponse({expected_address}));

  // parse the result
  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(expected_address.size(), query_ctx_->answers_.size());

  EXPECT_LT(udp_response_.buffer_->length(), Utils::MAX_UDP_DNS_SIZE);

  for (const auto& answer : query_ctx_->answers_) {
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
  EXPECT_CALL(*timeout_timer, enableTimer(_, _)).Times(1);

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

  EXPECT_CALL(*timeout_timer, disableTimer()).Times(1);

  // Execute resolve callback
  resolve_cb(Network::DnsResolver::ResolutionStatus::Success, TestUtility::makeDnsResponse({}));

  // parse the result
  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NAME_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(0, query_ctx_->answers_.size());

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
  EXPECT_CALL(*timeout_timer, enableTimer(_, _)).Times(1);

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
  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NAME_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(0, query_ctx_->answers_.size());

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
  EXPECT_CALL(*timeout_timer, enableTimer(_, _)).Times(1);

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
  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NAME_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(0, query_ctx_->answers_.size());

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
  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);
  EXPECT_EQ(0, query_ctx_->answers_.size());
  EXPECT_EQ(DNS_RESPONSE_CODE_NAME_ERROR, response_parser_->getQueryResponseCode());

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

  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(2, query_ctx_->answers_.size());

  // Verify the address returned
  const std::list<std::string> expected{"10.0.0.1", "10.0.0.2"};
  for (const auto& answer : query_ctx_->answers_) {
    EXPECT_EQ(answer.first, domain);
    Utils::verifyAddress(expected, answer.second);
  }

  // Validate stats
  ASSERT_EQ(1, config_->stats().downstream_rx_queries_.value());
  ASSERT_EQ(1, config_->stats().known_domain_queries_.value());
  ASSERT_EQ(2, config_->stats().local_a_record_answers_.value());
  ASSERT_EQ(1, config_->stats().a_record_queries_.value());
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(2, query_ctx_->answers_.size());

  // Verify the address returned
  const std::list<std::string> expected{"10.0.0.1", "10.0.0.2"};
  for (const auto& answer : query_ctx_->answers_) {
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(1, query_ctx_->answers_.size());

  // Verify that we have an answer record for the queried domain
  const DnsAnswerRecordPtr& answer = query_ctx_->answers_.find(domain)->second;

  // Verify the address returned
  const std::list<std::string> expected{"10.0.3.1"};
  Utils::verifyAddress(expected, answer);
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_FALSE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());

  // TODO(abaptiste): underflow stats
  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_FALSE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());

  // TODO(abaptiste): underflow/overflow stats
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_FALSE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());

  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(0, query_ctx_->answers_.size());
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_FALSE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());

  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(1, config_->stats().downstream_rx_invalid_queries_.value());
  EXPECT_EQ(0, query_ctx_->answers_.size());
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NOT_IMPLEMENTED, response_parser_->getQueryResponseCode());

  EXPECT_EQ(0, config_->stats().a_record_queries_.value());
  EXPECT_EQ(0, config_->stats().downstream_rx_invalid_queries_.value());
}

TEST_F(DnsFilterTest, InvalidShortBufferTest) {
  InSequence s;

  setup(forward_query_off_config);
  // This is an invalid query. Envoy should handle the packet and indicate a parsing failure
  constexpr char dns_request[] = {0x1c};
  const std::string query = Utils::buildQueryFromBytes(dns_request, 1);
  sendQueryFromClient("10.0.0.1:1000", query);

  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_FALSE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());

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

  query_ctx_ = response_parser_->createQueryContext(udp_response_, counters_);
  EXPECT_TRUE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());

  // Although 16 addresses are defined, only 8 are returned
  EXPECT_EQ(8, query_ctx_->answers_.size());

  // We shuffle the list of addresses when we read the config, and in the case of more than
  // 8 defined addresses, we randomize the initial starting index. We should not end up with
  // the first answer being the first defined address, or the answers appearing in the same
  // order as they are defined.
  const std::list<std::string> defined_order{"10.0.16.1", "10.0.16.2", "10.0.16.3", "10.0.16.4",
                                             "10.0.16.5", "10.0.16.6", "10.0.16.7", "10.0.16.8"};
  auto defined_answer_iter = defined_order.begin();
  for (const auto& answer : query_ctx_->answers_) {
    const auto resolved_address = answer.second->ip_addr_->ip()->addressAsString();
    EXPECT_NE(0L, resolved_address.compare(*defined_answer_iter++));
  }
}

} // namespace
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

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

using testing::AtLeast;
using testing::InSequence;
using testing::Return;
using testing::ReturnRef;

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

class DnsFilterTest : public testing::Test {
public:
  DnsFilterTest()
      : listener_address_(Network::Utility::parseInternetAddressAndPort("127.0.2.1:5353")),
        api_(Api::createApiForTest()) {

    response_parser_ =
        std::make_unique<DnsMessageParser>(true /* recursive queries */, 0 /* retries */, random_);
    udp_response_.addresses_.local_ = listener_address_;
    udp_response_.addresses_.peer_ = listener_address_;
    udp_response_.buffer_ = std::make_unique<Buffer::OwnedImpl>();

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

  void setup(const std::string& yaml) {
    envoy::extensions::filters::udp::dns_filter::v3alpha::DnsFilterConfig config;
    TestUtility::loadFromYamlAndValidate(yaml, config);
    auto store = stats_store_.createScope("dns_scope");
    EXPECT_CALL(listener_factory_, scope()).WillOnce(ReturnRef(*store));
    EXPECT_CALL(listener_factory_, dispatcher()).Times(AtLeast(0));
    EXPECT_CALL(listener_factory_, clusterManager()).Times(AtLeast(0));
    EXPECT_CALL(listener_factory_, api()).WillOnce(ReturnRef(*api_));
    ON_CALL(random_, random()).WillByDefault(Return(3));
    EXPECT_CALL(listener_factory_, random()).WillOnce(ReturnRef(random_));

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
  DnsQueryContextPtr query_ctx_;
  Event::MockDispatcher dispatcher_;
  Network::MockUdpReadFilterCallbacks callbacks_;
  Network::UdpRecvData udp_response_;
  NiceMock<Filesystem::MockInstance> file_system_;
  NiceMock<Stats::MockHistogram> histogram_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Server::Configuration::MockListenerFactoryContext listener_factory_;
  Stats::IsolatedStoreImpl stats_store_;
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
  resolver_timeout: 5s
  upstream_resolvers:
  - "1.1.1.1"
  - "8.8.8.8"
  - "8.8.4.4"
server_config:
  inline_dns_table:
    external_retry_count: 3
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
  resolver_timeout: 5s
  upstream_resolvers:
  - "1.1.1.1"
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
  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_FALSE(query_ctx_->parse_status_);

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

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_TRUE(query_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  // There are 8 addresses, however, since the domain is part of the answer record, each
  // serialized answer is over 100 bytes in size, there is room for 3 before the next
  // serialized answer puts the buffer over the 512 byte limit. The query itself is also
  // around 100 bytes.
  EXPECT_EQ(3, query_ctx_->answers_.size());
}

TEST_F(DnsFilterTest, InvalidQueryNameTooLongTest) {
  InSequence s;

  setup(forward_query_off_config);
  std::string domain = "www." + std::string(256, 'a') + ".com";
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  sendQueryFromClient("10.0.0.1:1000", query);

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_FALSE(query_ctx_->parse_status_);

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

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_FALSE(query_ctx_->parse_status_);

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

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_TRUE(query_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(1, query_ctx_->answers_.size());

  // Verify that we have an answer record for the queried domain

  const DnsAnswerRecordPtr& answer = query_ctx_->answers_.find(domain)->second;

  // Verify the address returned
  const std::list<std::string> expected{"10.0.3.1"};
  Utils::verifyAddress(expected, answer);
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

    query_ctx_ = response_parser_->createQueryContext(udp_response_);
    EXPECT_TRUE(query_ctx_->parse_status_);

    EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
    EXPECT_EQ(1, query_ctx_->answers_.size());

    // Verify that we have an answer record for the queried domain
    const DnsAnswerRecordPtr& answer = query_ctx_->answers_.find(domain)->second;

    // Verify the address returned
    std::list<std::string> expected{"10.0.3.1"};
    Utils::verifyAddress(expected, answer);
  }
}

TEST_F(DnsFilterTest, LocalTypeAQueryFail) {
  InSequence s;

  setup(forward_query_off_config);
  const std::string query =
      Utils::buildQueryForDomain("www.foo2.com", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());

  sendQueryFromClient("10.0.0.1:1000", query);
  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_TRUE(query_ctx_->parse_status_);

  EXPECT_EQ(3, response_parser_->getQueryResponseCode());
  EXPECT_EQ(0, query_ctx_->answers_.size());
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
  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_TRUE(query_ctx_->parse_status_);

  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(expected.size(), query_ctx_->answers_.size());

  // Verify the address returned
  for (const auto& answer : query_ctx_->answers_) {
    EXPECT_EQ(answer.first, domain);
    Utils::verifyAddress(expected, answer.second);
  }
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_TRUE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(2, query_ctx_->answers_.size());

  // Verify the address returned
  const std::list<std::string> expected{"10.0.0.1", "10.0.0.2"};
  for (const auto& answer : query_ctx_->answers_) {
    EXPECT_EQ(answer.first, domain);
    Utils::verifyAddress(expected, answer.second);
  }
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_TRUE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(2, query_ctx_->answers_.size());

  // Verify the address returned
  const std::list<std::string> expected{"10.0.0.1", "10.0.0.2"};
  for (const auto& answer : query_ctx_->answers_) {
    EXPECT_EQ(answer.first, domain);
    Utils::verifyAddress(expected, answer.second);
  }
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_FALSE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_FALSE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());
}

TEST_F(DnsFilterTest, MultipleQueryCountTest) {
  InSequence s;

  setup(forward_query_off_config);
  // In this buffer we have 2 queries for two different domains. This is a rare case
  // and serves to validate that we handle the protocol correctly.
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_TRUE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NO_ERROR, response_parser_->getQueryResponseCode());
  EXPECT_EQ(3, query_ctx_->answers_.size());

  // Verify that the answers contain an entry for each domain
  for (const auto& answer : query_ctx_->answers_) {
    if (answer.first == "www.foo1.com") {
      Utils::verifyAddress({"10.0.0.1", "10.0.0.2"}, answer.second);
    } else if (answer.first == "www.foo3.com") {
      Utils::verifyAddress({"10.0.3.1"}, answer.second);
    } else {
      FAIL() << "Unexpected domain in DNS response: " << answer.first;
    }
  }
}

TEST_F(DnsFilterTest, InvalidQueryCountTest) {
  InSequence s;

  setup(forward_query_off_config);
  // In this buffer the Questions count is incorrect. We will abort parsing and return a response
  // to the client.
  constexpr char dns_request[] = {
      0x36, 0x6e,                               // Transaction ID
      0x01, 0x20,                               // Flags
      0x00, 0x0a,                               // Questions
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_TRUE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());
}

TEST_F(DnsFilterTest, InvalidQueryCountTest2) {
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_FALSE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());
}

TEST_F(DnsFilterTest, NotImplementedQueryTest) {
  InSequence s;

  setup(forward_query_off_config);
  // In this buffer the Questions count is zero. This is an invalid query and is handled as such.
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

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_TRUE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_NOT_IMPLEMENTED, response_parser_->getQueryResponseCode());
}

TEST_F(DnsFilterTest, InvalidShortBufferTest) {
  InSequence s;

  setup(forward_query_off_config);
  // This is an invalid query. Envoy should handle the packet and indicate a parsing failure
  constexpr char dns_request[] = {0x1c};
  const std::string query = Utils::buildQueryFromBytes(dns_request, 1);
  sendQueryFromClient("10.0.0.1:1000", query);

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
  EXPECT_FALSE(query_ctx_->parse_status_);
  EXPECT_EQ(DNS_RESPONSE_CODE_FORMAT_ERROR, response_parser_->getQueryResponseCode());
}

TEST_F(DnsFilterTest, RandomizeFirstAnswerTest) {
  InSequence s;

  setup(forward_query_off_config);
  const std::string domain("www.foo16.com");

  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  ASSERT_FALSE(query.empty());
  sendQueryFromClient("10.0.0.1:1000", query);

  query_ctx_ = response_parser_->createQueryContext(udp_response_);
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

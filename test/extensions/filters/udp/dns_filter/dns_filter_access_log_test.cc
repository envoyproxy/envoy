#include "envoy/extensions/filters/udp/dns_filter/v3/dns_filter.pb.h"
#include "envoy/extensions/filters/udp/dns_filter/v3/dns_filter.pb.validate.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/udp/dns_filter/dns_filter.h"
#include "source/extensions/filters/udp/dns_filter/dns_filter_constants.h"
#include "source/extensions/filters/udp/dns_filter/dns_filter_utils.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/listener_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "dns_filter_test_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace {

using ResponseValidator = Utils::DnsResponseValidator;

Api::IoCallUint64Result makeNoError(uint64_t rc) {
  return Api::IoCallUint64Result(rc, Api::IoErrorPtr(nullptr, [](Api::IoError*) {}));
}

// Test access logger that captures filter state in memory
class TestAccessLog : public AccessLog::Instance {
public:
  void log(const Formatter::HttpFormatterContext&,
           const StreamInfo::StreamInfo& stream_info) override {
    log_count_++;
    last_filter_state_ = &stream_info.filterState();
    last_remote_address_ = stream_info.downstreamAddressProvider().remoteAddress()->asString();
    last_local_address_ = stream_info.downstreamAddressProvider().localAddress()->asString();
  }

  size_t logCount() const { return log_count_; }
  const StreamInfo::FilterState* lastFilterState() const { return last_filter_state_; }
  const std::string& lastRemoteAddress() const { return last_remote_address_; }
  const std::string& lastLocalAddress() const { return last_local_address_; }

  void reset() {
    log_count_ = 0;
    last_filter_state_ = nullptr;
    last_remote_address_.clear();
    last_local_address_.clear();
  }

private:
  size_t log_count_ = 0;
  const StreamInfo::FilterState* last_filter_state_ = nullptr;
  std::string last_remote_address_;
  std::string last_local_address_;
};

class DnsFilterAccessLogTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  DnsFilterAccessLogTest()
      : listener_address_(Network::Utility::parseInternetAddressAndPortNoThrow("127.0.0.1:53")),
        api_(Api::createApiForTest(random_)),
        counters_(mock_query_buffer_underflow_, mock_record_name_overflow_, query_parsing_failure_,
                  queries_with_additional_rrs_, queries_with_ans_or_authority_rrs_) {
    udp_response_.addresses_.local_ = listener_address_;
    udp_response_.addresses_.peer_ = listener_address_;
    udp_response_.buffer_ = std::make_unique<Buffer::OwnedImpl>();

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

  ~DnsFilterAccessLogTest() override { EXPECT_CALL(callbacks_.udp_listener_, onDestroy()); }

  void setup(const std::string& yaml) {
    envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig config;
    TestUtility::loadFromYamlAndValidate(yaml, config);
    auto store = stats_store_.createScope("dns_scope");
    ON_CALL(listener_factory_, scope()).WillByDefault(ReturnRef(*store));
    ON_CALL(listener_factory_.server_factory_context_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(random_, random()).WillByDefault(Return(3));
    ON_CALL(listener_factory_.server_factory_context_.api_, randomGenerator())
        .WillByDefault(ReturnRef(random_));

    config_ = std::make_shared<DnsFilterEnvoyConfig>(listener_factory_, config);
    filter_ = std::make_unique<DnsFilter>(callbacks_, config_);
  }

  void setupWithTestAccessLog(const std::string& yaml) {
    envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig config;
    TestUtility::loadFromYamlAndValidate(yaml, config);
    auto store = stats_store_.createScope("dns_scope");
    ON_CALL(listener_factory_, scope()).WillByDefault(ReturnRef(*store));
    ON_CALL(listener_factory_.server_factory_context_, api()).WillByDefault(ReturnRef(*api_));
    ON_CALL(random_, random()).WillByDefault(Return(3));
    ON_CALL(listener_factory_.server_factory_context_.api_, randomGenerator())
        .WillByDefault(ReturnRef(random_));

    config_ = std::make_shared<DnsFilterEnvoyConfig>(listener_factory_, config);

    // Add test access logger
    test_access_log_ = std::make_shared<TestAccessLog>();
    const_cast<AccessLog::InstanceSharedPtrVector&>(config_->accessLogs())
        .push_back(test_access_log_);

    filter_ = std::make_unique<DnsFilter>(callbacks_, config_);
  }

  void sendQueryFromClient(const std::string& peer_address, const std::string& buffer) {
    Network::UdpRecvData data{};
    data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow(peer_address);
    data.addresses_.local_ = listener_address_;
    data.buffer_ = std::make_unique<Buffer::OwnedImpl>(buffer);
    data.receive_time_ = MonotonicTime(std::chrono::seconds(0));
    filter_->onData(data);
  }

  const Network::Address::InstanceConstSharedPtr listener_address_;
  NiceMock<Random::MockRandomGenerator> random_;
  Api::ApiPtr api_;
  DnsFilterEnvoyConfigSharedPtr config_;
  NiceMock<Stats::MockCounter> mock_query_buffer_underflow_;
  NiceMock<Stats::MockCounter> mock_record_name_overflow_;
  NiceMock<Stats::MockCounter> query_parsing_failure_;
  NiceMock<Stats::MockCounter> queries_with_additional_rrs_;
  NiceMock<Stats::MockCounter> queries_with_ans_or_authority_rrs_;
  DnsParserCounters counters_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Network::MockUdpReadFilterCallbacks callbacks_;
  Network::UdpRecvData udp_response_;
  NiceMock<Server::Configuration::MockListenerFactoryContext> listener_factory_;
  Stats::IsolatedStoreImpl stats_store_;
  std::unique_ptr<DnsFilter> filter_;
  std::shared_ptr<TestAccessLog> test_access_log_;
};

// Test that access log is not called when no access loggers are configured
TEST_F(DnsFilterAccessLogTest, NoAccessLogConfigured) {
  const std::string config_yaml = R"EOF(
stat_prefix: "my_prefix"
server_config:
  inline_dns_table:
    virtual_domains:
    - name: "www.example.com"
      endpoint:
        address_list:
          address:
          - "10.0.0.1"
)EOF";

  setup(config_yaml);

  // Verify no access logs are configured
  EXPECT_TRUE(config_->accessLogs().empty());

  // Send a DNS query, should work without access logging
  const std::string domain("www.example.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);
  sendQueryFromClient("127.0.0.1:1000", query);
}

// Test that access log is called with correct filter state
TEST_F(DnsFilterAccessLogTest, AccessLogCalledWithCorrectFilterState) {
  const std::string config_yaml = R"EOF(
stat_prefix: "my_prefix"
server_config:
  inline_dns_table:
    virtual_domains:
    - name: "www.example.com"
      endpoint:
        address_list:
          address:
          - "10.0.0.1"
          - "10.0.0.2"
)EOF";

  setupWithTestAccessLog(config_yaml);

  const std::string domain("www.example.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);

  sendQueryFromClient("192.168.1.100:54321", query);

  // Verify access log was called
  ASSERT_EQ(test_access_log_->logCount(), 1);

  // Verify filter state contains DNS query context
  const auto* filter_state = test_access_log_->lastFilterState();
  ASSERT_NE(filter_state, nullptr);

  const auto* dns_context = filter_state->getDataReadOnly<DnsQueryContext>(DnsQueryContext::key());
  ASSERT_NE(dns_context, nullptr);

  // Verify field access support
  EXPECT_TRUE(dns_context->hasFieldSupport());

  // Verify all fields
  EXPECT_EQ(absl::get<absl::string_view>(dns_context->getField("query_name")), "www.example.com");
  EXPECT_EQ(absl::get<int64_t>(dns_context->getField("query_type")), DNS_RECORD_TYPE_A);
  EXPECT_EQ(absl::get<int64_t>(dns_context->getField("query_class")), DNS_RECORD_CLASS_IN);
  EXPECT_EQ(absl::get<int64_t>(dns_context->getField("answer_count")), 2);
  EXPECT_EQ(absl::get<int64_t>(dns_context->getField("response_code")), DNS_RESPONSE_CODE_NO_ERROR);
  EXPECT_EQ(absl::get<int64_t>(dns_context->getField("parse_status")), 1); // true = 1
}

// Test access logging with AAAA query
TEST_F(DnsFilterAccessLogTest, AccessLogForAAAAQuery) {
  const std::string config_yaml = R"EOF(
stat_prefix: "my_prefix"
server_config:
  inline_dns_table:
    virtual_domains:
    - name: "www.example.com"
      endpoint:
        address_list:
          address:
          - "2001:db8::1"
)EOF";

  setupWithTestAccessLog(config_yaml);

  const std::string domain("www.example.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_AAAA, DNS_RECORD_CLASS_IN);

  sendQueryFromClient("192.168.1.100:54321", query);

  ASSERT_EQ(test_access_log_->logCount(), 1);

  const auto* filter_state = test_access_log_->lastFilterState();
  const auto* dns_context = filter_state->getDataReadOnly<DnsQueryContext>(DnsQueryContext::key());
  ASSERT_NE(dns_context, nullptr);

  EXPECT_EQ(absl::get<int64_t>(dns_context->getField("query_type")), DNS_RECORD_TYPE_AAAA);
  EXPECT_EQ(absl::get<int64_t>(dns_context->getField("answer_count")), 1);
}

// Test access logging for NXDOMAIN (query for non-existent domain)
TEST_F(DnsFilterAccessLogTest, AccessLogForNXDOMAIN) {
  const std::string config_yaml = R"EOF(
stat_prefix: "my_prefix"
server_config:
  inline_dns_table:
    virtual_domains:
    - name: "www.example.com"
      endpoint:
        address_list:
          address:
          - "10.0.0.1"
)EOF";

  setupWithTestAccessLog(config_yaml);

  const std::string domain("nonexistent.example.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);

  sendQueryFromClient("192.168.1.100:54321", query);

  ASSERT_EQ(test_access_log_->logCount(), 1);

  const auto* filter_state = test_access_log_->lastFilterState();
  const auto* dns_context = filter_state->getDataReadOnly<DnsQueryContext>(DnsQueryContext::key());
  ASSERT_NE(dns_context, nullptr);

  EXPECT_EQ(absl::get<absl::string_view>(dns_context->getField("query_name")), "nonexistent.example.com");
  EXPECT_EQ(absl::get<int64_t>(dns_context->getField("response_code")), DNS_RESPONSE_CODE_NAME_ERROR);
  EXPECT_EQ(absl::get<int64_t>(dns_context->getField("answer_count")), 0);
}

// Test that multiple access loggers all get called
TEST_F(DnsFilterAccessLogTest, MultipleAccessLoggers) {
  const std::string config_yaml = R"EOF(
stat_prefix: "my_prefix"
server_config:
  inline_dns_table:
    virtual_domains:
    - name: "www.example.com"
      endpoint:
        address_list:
          address:
          - "10.0.0.1"
)EOF";

  envoy::extensions::filters::udp::dns_filter::v3::DnsFilterConfig config;
  TestUtility::loadFromYamlAndValidate(config_yaml, config);
  auto store = stats_store_.createScope("dns_scope");
  ON_CALL(listener_factory_, scope()).WillByDefault(ReturnRef(*store));
  ON_CALL(listener_factory_.server_factory_context_, api()).WillByDefault(ReturnRef(*api_));
  ON_CALL(random_, random()).WillByDefault(Return(3));
  ON_CALL(listener_factory_.server_factory_context_.api_, randomGenerator())
      .WillByDefault(ReturnRef(random_));

  config_ = std::make_shared<DnsFilterEnvoyConfig>(listener_factory_, config);

  auto test_access_log1 = std::make_shared<TestAccessLog>();
  auto test_access_log2 = std::make_shared<TestAccessLog>();
  const_cast<AccessLog::InstanceSharedPtrVector&>(config_->accessLogs())
      .push_back(test_access_log1);
  const_cast<AccessLog::InstanceSharedPtrVector&>(config_->accessLogs())
      .push_back(test_access_log2);

  filter_ = std::make_unique<DnsFilter>(callbacks_, config_);

  const std::string domain("www.example.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);

  sendQueryFromClient("192.168.1.100:54321", query);

  // Both loggers should have been called
  EXPECT_EQ(test_access_log1->logCount(), 1);
  EXPECT_EQ(test_access_log2->logCount(), 1);
}

// Test that downstream addresses are captured correctly
TEST_F(DnsFilterAccessLogTest, DownstreamAddressesCaptured) {
  const std::string config_yaml = R"EOF(
stat_prefix: "my_prefix"
server_config:
  inline_dns_table:
    virtual_domains:
    - name: "www.example.com"
      endpoint:
        address_list:
          address:
          - "10.0.0.1"
)EOF";

  setupWithTestAccessLog(config_yaml);

  const std::string domain("www.example.com");
  const std::string query =
      Utils::buildQueryForDomain(domain, DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN);

  const std::string client_address = "192.168.1.100:54321";
  sendQueryFromClient(client_address, query);

  ASSERT_EQ(test_access_log_->logCount(), 1);

  // Verify remote (client) address
  EXPECT_EQ(test_access_log_->lastRemoteAddress(), client_address);

  // Verify local (listener) address
  EXPECT_EQ(test_access_log_->lastLocalAddress(), "127.0.0.1:53");
}

// Test access logging with malformed query (empty queries)
TEST_F(DnsFilterAccessLogTest, AccessLogWithMalformedQuery) {
  const std::string config_yaml = R"EOF(
stat_prefix: "my_prefix"
server_config:
  inline_dns_table:
    virtual_domains:
    - name: "www.example.com"
      endpoint:
        address_list:
          address:
          - "10.0.0.1"
)EOF";

  setupWithTestAccessLog(config_yaml);

  // Send malformed DNS query (empty buffer)
  sendQueryFromClient("192.168.1.100:54321", "");

  ASSERT_EQ(test_access_log_->logCount(), 1);

  const auto* filter_state = test_access_log_->lastFilterState();
  const auto* dns_context = filter_state->getDataReadOnly<DnsQueryContext>(DnsQueryContext::key());
  ASSERT_NE(dns_context, nullptr);

  // When queries are empty, query fields should return monostate
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(dns_context->getField("query_name")));
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(dns_context->getField("query_type")));
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(dns_context->getField("query_class")));

  // But other fields should still be accessible
  EXPECT_EQ(absl::get<int64_t>(dns_context->getField("answer_count")), 0);
  EXPECT_EQ(absl::get<int64_t>(dns_context->getField("parse_status")), 0); // false = 0
  EXPECT_TRUE(absl::holds_alternative<int64_t>(dns_context->getField("response_code")));
}

} // namespace
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

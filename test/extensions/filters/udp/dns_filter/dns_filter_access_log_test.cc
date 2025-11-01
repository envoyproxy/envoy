#include "envoy/extensions/filters/udp/dns_filter/v3/dns_filter.pb.h"
#include "envoy/extensions/filters/udp/dns_filter/v3/dns_filter.pb.validate.h"

#include "source/common/common/logger.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/udp/dns_filter/dns_filter.h"
#include "source/extensions/filters/udp/dns_filter/dns_filter_access_log.h"
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

// Test access logger that captures formatted output using DNS custom commands
class TestAccessLog : public AccessLog::Instance {
public:
  TestAccessLog() {
    auto parser = createDnsFilterCommandParser();
    query_name_formatter_ = parser->parse("QUERY_NAME", "", absl::nullopt);
    query_type_formatter_ = parser->parse("QUERY_TYPE", "", absl::nullopt);
    query_class_formatter_ = parser->parse("QUERY_CLASS", "", absl::nullopt);
    answer_count_formatter_ = parser->parse("ANSWER_COUNT", "", absl::nullopt);
    response_code_formatter_ = parser->parse("RESPONSE_CODE", "", absl::nullopt);
    parse_status_formatter_ = parser->parse("PARSE_STATUS", "", absl::nullopt);
  }

  void log(const Formatter::Context& context, const StreamInfo::StreamInfo& stream_info) override {
    log_count_++;

    // Use custom formatters to extract DNS information
    query_name_ = query_name_formatter_->format(context, stream_info);
    query_type_ = query_type_formatter_->format(context, stream_info);
    query_class_ = query_class_formatter_->format(context, stream_info);
    answer_count_ = answer_count_formatter_->format(context, stream_info);
    response_code_ = response_code_formatter_->format(context, stream_info);
    parse_status_ = parse_status_formatter_->format(context, stream_info);

    // Store address information for testing
    remote_address_ = stream_info.downstreamAddressProvider().remoteAddress()->asString();
    local_address_ = stream_info.downstreamAddressProvider().localAddress()->asString();
  }

  void reset() {
    log_count_ = 0;
    query_name_ = absl::nullopt;
    query_type_ = absl::nullopt;
    query_class_ = absl::nullopt;
    answer_count_ = absl::nullopt;
    response_code_ = absl::nullopt;
    parse_status_ = absl::nullopt;
    remote_address_.clear();
    local_address_.clear();
  }

  size_t log_count_ = 0;

  // Formatters using DNS custom commands
  Formatter::FormatterProviderPtr query_name_formatter_;
  Formatter::FormatterProviderPtr query_type_formatter_;
  Formatter::FormatterProviderPtr query_class_formatter_;
  Formatter::FormatterProviderPtr answer_count_formatter_;
  Formatter::FormatterProviderPtr response_code_formatter_;
  Formatter::FormatterProviderPtr parse_status_formatter_;

  // Formatted values
  absl::optional<std::string> query_name_;
  absl::optional<std::string> query_type_;
  absl::optional<std::string> query_class_;
  absl::optional<std::string> answer_count_;
  absl::optional<std::string> response_code_;
  absl::optional<std::string> parse_status_;

  // Address information
  std::string remote_address_;
  std::string local_address_;
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

// Test that access log is called with correct formatted DNS data using custom commands
TEST_F(DnsFilterAccessLogTest, AccessLogCalledWithCorrectMetadata) {
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
  ASSERT_EQ(test_access_log_->log_count_, 1);

  // Verify DNS custom command formatters extracted correct information
  EXPECT_EQ(test_access_log_->query_name_.value(), "www.example.com");
  EXPECT_EQ(test_access_log_->query_type_.value(), "1");  // DNS_RECORD_TYPE_A
  EXPECT_EQ(test_access_log_->query_class_.value(), "1"); // DNS_RECORD_CLASS_IN
  EXPECT_EQ(test_access_log_->answer_count_.value(), "2");
  EXPECT_EQ(test_access_log_->response_code_.value(), "0"); // DNS_RESPONSE_CODE_NO_ERROR
  EXPECT_EQ(test_access_log_->parse_status_.value(), "true");
}

// Test access logging with AAAA query using custom formatters
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

  ASSERT_EQ(test_access_log_->log_count_, 1);

  EXPECT_EQ(test_access_log_->query_type_.value(), "28"); // quad-A record type
  EXPECT_EQ(test_access_log_->answer_count_.value(), "1");
}

// Test access logging for NXDOMAIN using custom formatters
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

  ASSERT_EQ(test_access_log_->log_count_, 1);

  EXPECT_EQ(test_access_log_->query_name_.value(), "nonexistent.example.com");
  EXPECT_EQ(test_access_log_->response_code_.value(), "3"); // DNS_RESPONSE_CODE_NAME_ERROR
  EXPECT_EQ(test_access_log_->answer_count_.value(), "0");
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
  EXPECT_EQ(test_access_log1->log_count_, 1);
  EXPECT_EQ(test_access_log2->log_count_, 1);
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

  ASSERT_EQ(test_access_log_->log_count_, 1);

  EXPECT_EQ(test_access_log_->remote_address_, client_address);
  EXPECT_EQ(test_access_log_->local_address_, "127.0.0.1:53");
}

// Test access logging with malformed query (empty queries) using custom formatters
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

  ASSERT_EQ(test_access_log_->log_count_, 1);

  // When queries are empty, custom formatters for query-specific fields should return nullopt
  EXPECT_FALSE(test_access_log_->query_name_.has_value());
  EXPECT_FALSE(test_access_log_->query_type_.has_value());
  EXPECT_FALSE(test_access_log_->query_class_.has_value());

  EXPECT_EQ(test_access_log_->answer_count_.value(), "0");
  EXPECT_EQ(test_access_log_->parse_status_.value(), "false");
}

// Test custom DNS command parser formatters
TEST(DnsFilterCommandParserTest, QueryNameFormatter) {
  auto parser = createDnsFilterCommandParser();
  auto formatter = parser->parse("QUERY_NAME", "", absl::nullopt);
  ASSERT_NE(formatter, nullptr);

  // Create StreamInfo
  Event::SimulatedTimeSystem test_time;
  auto connection_info = std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr);
  StreamInfo::StreamInfoImpl stream_info(test_time, connection_info,
                                         StreamInfo::FilterState::LifeSpan::Connection);

  // Create DNS query context with a query
  NiceMock<Stats::MockCounter> mock_counter;
  DnsParserCounters counters(mock_counter, mock_counter, mock_counter, mock_counter, mock_counter);
  auto dns_context = std::make_unique<DnsQueryContext>(nullptr, nullptr, counters, 0);
  dns_context->queries_.push_back(
      std::make_unique<DnsQueryRecord>("example.com", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN));

  // Create formatter context with DNS extension
  Formatter::Context formatter_context;
  formatter_context.setExtension(*dns_context);

  // Test format string
  auto result = formatter->format(formatter_context, stream_info);
  EXPECT_EQ(result.value(), "example.com");

  // Test format value
  auto value = formatter->formatValue(formatter_context, stream_info);
  EXPECT_EQ(value.string_value(), "example.com");
}

TEST(DnsFilterCommandParserTest, QueryTypeFormatter) {
  auto parser = createDnsFilterCommandParser();
  auto formatter = parser->parse("QUERY_TYPE", "", absl::nullopt);
  ASSERT_NE(formatter, nullptr);

  Event::SimulatedTimeSystem test_time;
  auto connection_info = std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr);
  StreamInfo::StreamInfoImpl stream_info(test_time, connection_info,
                                         StreamInfo::FilterState::LifeSpan::Connection);

  NiceMock<Stats::MockCounter> mock_counter;
  DnsParserCounters counters(mock_counter, mock_counter, mock_counter, mock_counter, mock_counter);
  auto dns_context = std::make_unique<DnsQueryContext>(nullptr, nullptr, counters, 0);
  dns_context->queries_.push_back(
      std::make_unique<DnsQueryRecord>("test.com", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN));

  Formatter::Context formatter_context;
  formatter_context.setExtension(*dns_context);

  auto result = formatter->format(formatter_context, stream_info);
  EXPECT_EQ(result.value(), "1"); // A record type

  auto value = formatter->formatValue(formatter_context, stream_info);
  EXPECT_EQ(value.string_value(), "1");
}

TEST(DnsFilterCommandParserTest, AnswerCountFormatter) {
  auto parser = createDnsFilterCommandParser();
  auto formatter = parser->parse("ANSWER_COUNT", "", absl::nullopt);
  ASSERT_NE(formatter, nullptr);

  Event::SimulatedTimeSystem test_time;
  auto connection_info = std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr);
  StreamInfo::StreamInfoImpl stream_info(test_time, connection_info,
                                         StreamInfo::FilterState::LifeSpan::Connection);

  NiceMock<Stats::MockCounter> mock_counter;
  DnsParserCounters counters(mock_counter, mock_counter, mock_counter, mock_counter, mock_counter);
  auto dns_context = std::make_unique<DnsQueryContext>(nullptr, nullptr, counters, 0);
  // Add 5 dummy answers
  for (int i = 0; i < 5; i++) {
    dns_context->answers_.emplace("test.com", nullptr);
  }

  Formatter::Context formatter_context;
  formatter_context.setExtension(*dns_context);

  auto result = formatter->format(formatter_context, stream_info);
  EXPECT_EQ(result.value(), "5");

  auto value = formatter->formatValue(formatter_context, stream_info);
  EXPECT_EQ(value.string_value(), "5");
}

TEST(DnsFilterCommandParserTest, ResponseCodeFormatter) {
  auto parser = createDnsFilterCommandParser();
  auto formatter = parser->parse("RESPONSE_CODE", "", absl::nullopt);
  ASSERT_NE(formatter, nullptr);

  Event::SimulatedTimeSystem test_time;
  auto connection_info = std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr);
  StreamInfo::StreamInfoImpl stream_info(test_time, connection_info,
                                         StreamInfo::FilterState::LifeSpan::Connection);

  NiceMock<Stats::MockCounter> mock_counter;
  DnsParserCounters counters(mock_counter, mock_counter, mock_counter, mock_counter, mock_counter);
  auto dns_context = std::make_unique<DnsQueryContext>(nullptr, nullptr, counters, 0);
  dns_context->response_code_ = DNS_RESPONSE_CODE_NO_ERROR;

  Formatter::Context formatter_context;
  formatter_context.setExtension(*dns_context);

  auto result = formatter->format(formatter_context, stream_info);
  EXPECT_EQ(result.value(), "0"); // NO_ERROR

  auto value = formatter->formatValue(formatter_context, stream_info);
  EXPECT_EQ(value.string_value(), "0");
}

TEST(DnsFilterCommandParserTest, ParseStatusFormatter) {
  auto parser = createDnsFilterCommandParser();
  auto formatter = parser->parse("PARSE_STATUS", "", absl::nullopt);
  ASSERT_NE(formatter, nullptr);

  Event::SimulatedTimeSystem test_time;
  auto connection_info = std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr);
  StreamInfo::StreamInfoImpl stream_info(test_time, connection_info,
                                         StreamInfo::FilterState::LifeSpan::Connection);

  NiceMock<Stats::MockCounter> mock_counter;
  DnsParserCounters counters(mock_counter, mock_counter, mock_counter, mock_counter, mock_counter);
  auto dns_context = std::make_unique<DnsQueryContext>(nullptr, nullptr, counters, 0);
  dns_context->parse_status_ = true;

  Formatter::Context formatter_context;
  formatter_context.setExtension(*dns_context);

  auto result = formatter->format(formatter_context, stream_info);
  EXPECT_EQ(result.value(), "true");

  auto value = formatter->formatValue(formatter_context, stream_info);
  EXPECT_EQ(value.string_value(), "true");
}

TEST(DnsFilterCommandParserTest, MissingMetadata) {
  auto parser = createDnsFilterCommandParser();
  auto formatter = parser->parse("QUERY_NAME", "", absl::nullopt);
  ASSERT_NE(formatter, nullptr);

  // StreamInfo without DNS context extension
  Event::SimulatedTimeSystem test_time;
  auto connection_info = std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr);
  StreamInfo::StreamInfoImpl stream_info(test_time, connection_info,
                                         StreamInfo::FilterState::LifeSpan::Connection);

  auto result = formatter->format(Formatter::Context(), stream_info);
  EXPECT_FALSE(result.has_value());
}

TEST(DnsFilterCommandParserTest, QueryClassFormatter) {
  auto parser = createDnsFilterCommandParser();
  auto formatter = parser->parse("QUERY_CLASS", "", absl::nullopt);
  ASSERT_NE(formatter, nullptr);

  Event::SimulatedTimeSystem test_time;
  auto connection_info = std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr);
  StreamInfo::StreamInfoImpl stream_info(test_time, connection_info,
                                         StreamInfo::FilterState::LifeSpan::Connection);

  NiceMock<Stats::MockCounter> mock_counter;
  DnsParserCounters counters(mock_counter, mock_counter, mock_counter, mock_counter, mock_counter);
  auto dns_context = std::make_unique<DnsQueryContext>(nullptr, nullptr, counters, 0);
  dns_context->queries_.push_back(
      std::make_unique<DnsQueryRecord>("test.com", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN));

  Formatter::Context formatter_context;
  formatter_context.setExtension(*dns_context);

  auto result = formatter->format(formatter_context, stream_info);
  EXPECT_EQ(result.value(), "1"); // IN class

  auto value = formatter->formatValue(formatter_context, stream_info);
  EXPECT_EQ(value.string_value(), "1");
}

TEST(DnsFilterCommandParserTest, UnknownCommand) {
  auto parser = createDnsFilterCommandParser();
  auto formatter = parser->parse("UNKNOWN_COMMAND", "", absl::nullopt);
  EXPECT_EQ(formatter, nullptr);
}

TEST(DnsFilterCommandParserTest, EmptyCommandArg) {
  auto parser = createDnsFilterCommandParser();

  // All DNS commands should work without command args
  EXPECT_NE(parser->parse("QUERY_NAME", "", absl::nullopt), nullptr);
  EXPECT_NE(parser->parse("QUERY_TYPE", "", absl::nullopt), nullptr);
  EXPECT_NE(parser->parse("QUERY_CLASS", "", absl::nullopt), nullptr);
  EXPECT_NE(parser->parse("ANSWER_COUNT", "", absl::nullopt), nullptr);
  EXPECT_NE(parser->parse("RESPONSE_CODE", "", absl::nullopt), nullptr);
  EXPECT_NE(parser->parse("PARSE_STATUS", "", absl::nullopt), nullptr);
}

TEST(DnsFilterCommandParserTest, CaseSensitiveCommands) {
  auto parser = createDnsFilterCommandParser();

  // Commands should be case-sensitive
  EXPECT_NE(parser->parse("QUERY_NAME", "", absl::nullopt), nullptr);
  EXPECT_EQ(parser->parse("query_name", "", absl::nullopt), nullptr);
  EXPECT_EQ(parser->parse("Query_Name", "", absl::nullopt), nullptr);
  EXPECT_EQ(parser->parse("QUERYNAME", "", absl::nullopt), nullptr);
}

TEST(DnsFilterCommandParserTest, FormatValueStringType) {
  auto parser = createDnsFilterCommandParser();
  auto formatter = parser->parse("QUERY_NAME", "", absl::nullopt);
  ASSERT_NE(formatter, nullptr);

  Event::SimulatedTimeSystem test_time;
  auto connection_info = std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr);
  StreamInfo::StreamInfoImpl stream_info(test_time, connection_info,
                                         StreamInfo::FilterState::LifeSpan::Connection);

  NiceMock<Stats::MockCounter> mock_counter;
  DnsParserCounters counters(mock_counter, mock_counter, mock_counter, mock_counter, mock_counter);
  auto dns_context = std::make_unique<DnsQueryContext>(nullptr, nullptr, counters, 0);
  dns_context->queries_.push_back(
      std::make_unique<DnsQueryRecord>("format.test.com", DNS_RECORD_TYPE_A, DNS_RECORD_CLASS_IN));

  Formatter::Context formatter_context;
  formatter_context.setExtension(*dns_context);

  // Test formatValue returns correct Protobuf value type
  auto value = formatter->formatValue(formatter_context, stream_info);
  EXPECT_EQ(value.kind_case(), Protobuf::Value::kStringValue);
  EXPECT_EQ(value.string_value(), "format.test.com");
}

TEST(DnsFilterCommandParserTest, FormatValueNullWhenMissing) {
  auto parser = createDnsFilterCommandParser();
  auto formatter = parser->parse("QUERY_NAME", "", absl::nullopt);
  ASSERT_NE(formatter, nullptr);

  Event::SimulatedTimeSystem test_time;
  auto connection_info = std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr);
  StreamInfo::StreamInfoImpl stream_info(test_time, connection_info,
                                         StreamInfo::FilterState::LifeSpan::Connection);

  // No DNS context set

  // Test formatValue returns null value
  auto value = formatter->formatValue(Formatter::Context(), stream_info);
  EXPECT_EQ(value.kind_case(), Protobuf::Value::kNullValue);
}

TEST(DnsFilterCommandParserTest, EmptyQueriesInContext) {
  auto parser = createDnsFilterCommandParser();

  Event::SimulatedTimeSystem test_time;
  auto connection_info = std::make_shared<Network::ConnectionInfoSetterImpl>(nullptr, nullptr);
  StreamInfo::StreamInfoImpl stream_info(test_time, connection_info,
                                         StreamInfo::FilterState::LifeSpan::Connection);

  // Create DNS context but with no queries
  NiceMock<Stats::MockCounter> mock_counter;
  DnsParserCounters counters(mock_counter, mock_counter, mock_counter, mock_counter, mock_counter);
  auto dns_context = std::make_unique<DnsQueryContext>(nullptr, nullptr, counters, 0);

  Formatter::Context formatter_context;
  formatter_context.setExtension(*dns_context);

  // Test all formatters that depend on queries return nullopt when queries are empty
  auto query_name_fmt = parser->parse("QUERY_NAME", "", absl::nullopt);
  EXPECT_FALSE(query_name_fmt->format(formatter_context, stream_info).has_value());

  auto query_type_fmt = parser->parse("QUERY_TYPE", "", absl::nullopt);
  EXPECT_FALSE(query_type_fmt->format(formatter_context, stream_info).has_value());

  auto query_class_fmt = parser->parse("QUERY_CLASS", "", absl::nullopt);
  EXPECT_FALSE(query_class_fmt->format(formatter_context, stream_info).has_value());

  // These should still work even without queries
  auto answer_count_fmt = parser->parse("ANSWER_COUNT", "", absl::nullopt);
  EXPECT_TRUE(answer_count_fmt->format(formatter_context, stream_info).has_value());

  auto response_code_fmt = parser->parse("RESPONSE_CODE", "", absl::nullopt);
  EXPECT_TRUE(response_code_fmt->format(formatter_context, stream_info).has_value());

  auto parse_status_fmt = parser->parse("PARSE_STATUS", "", absl::nullopt);
  EXPECT_TRUE(parse_status_fmt->format(formatter_context, stream_info).has_value());
}

} // namespace
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy

#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/route.pb.validate.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/http/protocol.h"

#include "source/common/config/metadata.h"
#include "source/common/config/utility.h"
#include "source/common/http/header_utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/router/header_parser.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"

#include "test/common/stream_info/test_int_accessor.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using ::testing::ElementsAre;
using ::testing::NiceMock;
using ::testing::Pair;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::ReturnRef;

static envoy::config::route::v3::Route parseRouteFromV3Yaml(const std::string& yaml) {
  envoy::config::route::v3::Route route;
  TestUtility::loadFromYaml(yaml, route);
  return route;
}

TEST(HeaderParserTest, TestParse) {
  struct TestCase {
    std::string input_;
    absl::optional<std::string> expected_output_;
    absl::optional<std::string> expected_exception_;
  };

  static const TestCase test_cases[] = {
      // Valid inputs
      {"", {}, {}},
      {"%PROTOCOL%", {"HTTP/1.1"}, {}},
      {"[%PROTOCOL%", {"[HTTP/1.1"}, {}},
      {"%PROTOCOL%]", {"HTTP/1.1]"}, {}},
      {"[%PROTOCOL%]", {"[HTTP/1.1]"}, {}},
      {"%%%PROTOCOL%", {"%HTTP/1.1"}, {}},
      {"%PROTOCOL%%%", {"HTTP/1.1%"}, {}},
      {"%%%PROTOCOL%%%", {"%HTTP/1.1%"}, {}},
      {"%DOWNSTREAM_REMOTE_ADDRESS%", {"127.0.0.1:0"}, {}},
      {"%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%", {"127.0.0.1"}, {}},
      {"%DOWNSTREAM_REMOTE_PORT%", {"0"}, {}},
      {"%DOWNSTREAM_DIRECT_REMOTE_ADDRESS%", {"127.0.0.3:63443"}, {}},
      {"%DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT%", {"127.0.0.3"}, {}},
      {"%DOWNSTREAM_DIRECT_REMOTE_PORT%", {"63443"}, {}},
      {"%DOWNSTREAM_LOCAL_ADDRESS%", {"127.0.0.2:0"}, {}},
      {"%DOWNSTREAM_LOCAL_PORT%", {"0"}, {}},
      {"%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%", {"127.0.0.2"}, {}},
      {"%UPSTREAM_METADATA([\"ns\", \"key\"])%", {"value"}, {}},
      {"[%UPSTREAM_METADATA([\"ns\", \"key\"])%", {"[value"}, {}},
      {"%UPSTREAM_METADATA([\"ns\", \"key\"])%]", {"value]"}, {}},
      {"[%UPSTREAM_METADATA([\"ns\", \"key\"])%]", {"[value]"}, {}},
      {"%UPSTREAM_METADATA([\"ns\", \t \"key\"])%", {"value"}, {}},
      {"%UPSTREAM_METADATA([\"ns\", \n \"key\"])%", {"value"}, {}},
      {"%UPSTREAM_METADATA( \t [ \t \"ns\" \t , \t \"key\" \t ] \t )%", {"value"}, {}},
      {R"EOF(%UPSTREAM_METADATA(["\"quoted\"", "\"key\""])%)EOF", {"value"}, {}},
      {"%UPSTREAM_REMOTE_ADDRESS%", {"10.0.0.1:443"}, {}},
      {"%UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%", {"10.0.0.1"}, {}},
      {"%UPSTREAM_REMOTE_PORT%", {"443"}, {}},
      {"%UPSTREAM_LOCAL_ADDRESS%", {"127.0.0.3:8443"}, {}},
      {"%UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%", {"127.0.0.3"}, {}},
      {"%UPSTREAM_LOCAL_PORT%", {"8443"}, {}},
      {"%REQUESTED_SERVER_NAME%", {"foo.bar"}, {}},
      {"%VIRTUAL_CLUSTER_NAME%", {"authN"}, {}},
      {"%PER_REQUEST_STATE(testing)%", {"test_value"}, {}},
      {"%REQ(x-request-id)%", {"123"}, {}},
      {"%START_TIME%", {"2018-04-03T23:06:09.123Z"}, {}},
      {"%RESPONSE_FLAGS%", {"LR"}, {}},
      {"%RESPONSE_CODE_DETAILS%", {"via_upstream"}, {}},
      {"STATIC_TEXT", {"STATIC_TEXT"}, {}},

      // Unescaped %
      {"%", {}, {"Invalid header configuration. Un-escaped % at position 0"}},
      {"before %", {}, {"Invalid header configuration. Un-escaped % at position 7"}},
      {"%% infix %", {}, {"Invalid header configuration. Un-escaped % at position 9"}},

      // Unknown variable names
      {"%INVALID%", {}, {"field 'INVALID' not supported as custom header"}},
      {"before %INVALID%", {}, {"field 'INVALID' not supported as custom header"}},
      {"%INVALID% after", {}, {"field 'INVALID' not supported as custom header"}},
      {"before %INVALID% after", {}, {"field 'INVALID' not supported as custom header"}},

      // Un-terminated variable expressions.
      {"%VAR", {}, {"Invalid header configuration. Un-terminated variable expression 'VAR'"}},
      {"%%%VAR", {}, {"Invalid header configuration. Un-terminated variable expression 'VAR'"}},
      {"before %VAR",
       {},
       {"Invalid header configuration. Un-terminated variable expression 'VAR'"}},
      {"before %%%VAR",
       {},
       {"Invalid header configuration. Un-terminated variable expression 'VAR'"}},
      {"before %VAR after",
       {},
       {"Invalid header configuration. Un-terminated variable expression 'VAR after'"}},
      {"before %%%VAR after",
       {},
       {"Invalid header configuration. Un-terminated variable expression 'VAR after'"}},
      {"% ", {}, {"Invalid header configuration. Un-terminated variable expression ' '"}},

      // Parsing errors in variable expressions that take a JSON-array parameter.
      {"%UPSTREAM_METADATA([\"\\",
       {},
       {"Invalid header configuration. Un-terminated backslash in JSON string after "
        "'UPSTREAM_METADATA([\"'"}},
      {"%UPSTREAM_METADATA([\"ns\", \"key\"]x",
       {},
       {"Invalid header configuration. Expecting ')' or whitespace after "
        "'UPSTREAM_METADATA([\"ns\", \"key\"]', but found 'x'"}},
      {"%UPSTREAM_METADATA([\"ns\", \"key\"])% %UPSTREAM_METADATA([\"ns\", \"key\"]x",
       {},
       {"Invalid header configuration. Expecting ')' or whitespace after "
        "'UPSTREAM_METADATA([\"ns\", \"key\"]', but found 'x'"}},
      {"%UPSTREAM_METADATA([\"ns\", \"key\"])x",
       {},
       {"Invalid header configuration. Expecting '%' or whitespace after "
        "'UPSTREAM_METADATA([\"ns\", \"key\"])', but found 'x'"}},

      {"%PER_REQUEST_STATE no parens%",
       {},
       {"Invalid header configuration. Expected format PER_REQUEST_STATE(<data_name>), "
        "actual format PER_REQUEST_STATE no parens"}},

      {"%REQ%",
       {},
       {"Invalid header configuration. Expected format REQ(<header-name>), "
        "actual format REQ"}},
      {"%REQ no parens%",
       {},
       {"Invalid header configuration. Expected format REQ(<header-name>), "
        "actual format REQno parens"}},

      // Invalid arguments
      {"%UPSTREAM_METADATA%",
       {},
       {"Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", \"k\", "
        "...]), actual format UPSTREAM_METADATA"}},
  };

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  const std::string requested_server_name = "foo.bar";
  stream_info.downstream_connection_info_provider_->setRequestedServerName(requested_server_name);
  const std::string virtual_cluster_name = "authN";
  stream_info.setVirtualClusterName(virtual_cluster_name);
  absl::optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(stream_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  stream_info.upstreamInfo()->setUpstreamHost(host);
  auto local_address = Network::Address::InstanceConstSharedPtr{
      new Network::Address::Ipv4Instance("127.0.0.3", 8443)};
  stream_info.upstreamInfo()->setUpstreamLocalAddress(local_address);

  Http::TestRequestHeaderMapImpl request_headers;
  request_headers.addCopy(Http::LowerCaseString(std::string("x-request-id")), 123);
  ON_CALL(stream_info, getRequestHeaders()).WillByDefault(Return(&request_headers));

  // Upstream metadata with percent signs in the key.
  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>(
      TestUtility::parseYaml<envoy::config::core::v3::Metadata>(
          R"EOF(
        filter_metadata:
          ns:
            key: value
          '"quoted"':
            '"key"': value
      )EOF"));
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));

  // "2018-04-03T23:06:09.123Z".
  const SystemTime start_time(std::chrono::milliseconds(1522796769123));
  ON_CALL(stream_info, startTime()).WillByDefault(Return(start_time));

  Envoy::StreamInfo::FilterStateSharedPtr filter_state(
      std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
          Envoy::StreamInfo::FilterState::LifeSpan::FilterChain));
  filter_state->setData("testing", std::make_unique<StringAccessorImpl>("test_value"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(*filter_state));

  stream_info.setResponseFlag(StreamInfo::CoreResponseFlag::LocalReset);

  absl::optional<std::string> rc_details{"via_upstream"};
  ON_CALL(stream_info, responseCodeDetails()).WillByDefault(ReturnRef(rc_details));

  for (const auto& test_case : test_cases) {
    Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption> to_add;
    envoy::config::core::v3::HeaderValueOption* header = to_add.Add();
    header->mutable_header()->set_key("x-header");
    header->mutable_header()->set_value(test_case.input_);

    if (test_case.expected_exception_) {
      EXPECT_FALSE(test_case.expected_output_);
      EXPECT_THROW(THROW_IF_NOT_OK_REF(HeaderParser::configure(to_add).status()), EnvoyException);
      continue;
    }

    HeaderParserPtr req_header_parser = HeaderParser::configure(to_add).value();

    Http::TestRequestHeaderMapImpl header_map{{":method", "POST"}};
    req_header_parser->evaluateHeaders(header_map, stream_info);

    std::string descriptor = fmt::format("for test case input: {}", test_case.input_);

    if (!test_case.expected_output_) {
      EXPECT_FALSE(header_map.has("x-header")) << descriptor;
      continue;
    }

    EXPECT_TRUE(header_map.has("x-header")) << descriptor;
    EXPECT_EQ(test_case.expected_output_.value(), header_map.get_("x-header")) << descriptor;
  }
}

TEST(HeaderParser, TestMetadataTranslator) {
  struct TestCase {
    std::string input_;
    std::string expected_output_;
  };
  static const TestCase test_cases[] = {
      {"%UPSTREAM_METADATA([\"a\", \"b\"])%", "%UPSTREAM_METADATA(a:b)%"},
      {"%UPSTREAM_METADATA([\"a\", \"b\",\"c\"])%", "%UPSTREAM_METADATA(a:b:c)%"},
      {"%UPSTREAM_METADATA([\"a\", \"b\",\"c\"])% %UPSTREAM_METADATA([\"d\", \"e\"])%",
       "%UPSTREAM_METADATA(a:b:c)% %UPSTREAM_METADATA(d:e)%"},
      {"%DYNAMIC_METADATA([\"a\", \"b\",\"c\"])%", "%DYNAMIC_METADATA(a:b:c)%"},
      {"%UPSTREAM_METADATA([\"a\", \"b\",\"c\"])% LEAVE_IT %DYNAMIC_METADATA([\"d\", \"e\"])%",
       "%UPSTREAM_METADATA(a:b:c)% LEAVE_IT %DYNAMIC_METADATA(d:e)%"},
      // The following test cases contain parts which should not be translated.
      {"nothing to translate", "nothing to translate"},
      {"%UPSTREAM_METADATA([\"a\", \"b\")%", "%UPSTREAM_METADATA([\"a\", \"b\")%"},
      {"%UPSTREAM_METADATA([\"a\", \"b\"]])%", "%UPSTREAM_METADATA([\"a\", \"b\"]])%"},
      {"%UPSTREAM_METADATA([\"a\", \"b\",\"c\"])% %DYNAMIC_METADATA([\"d\", \"e\")%",
       "%UPSTREAM_METADATA(a:b:c)% %DYNAMIC_METADATA([\"d\", \"e\")%"},
      {"UPSTREAM_METADATA([\"a\", \"b\"])%", "UPSTREAM_METADATA([\"a\", \"b\"])%"}};

  for (const auto& test_case : test_cases) {
    EXPECT_EQ(test_case.expected_output_, HeaderParser::translateMetadataFormat(test_case.input_));
  }
}

// Test passing incorrect json. translateMetadataFormat should return
// the same value without any modifications.
TEST(HeaderParser, TestMetadataTranslatorExceptions) {
  static const std::string test_cases[] = {
      "%UPSTREAM_METADATA([\"a\" - \"b\"])%",
      "%UPSTREAM_METADATA(\t [ \t\t ] \t)%",
      "%UPSTREAM_METADATA([\"udp{VTA(r%%%%%TA(r%%%%%b\\\\\\rin\\rsE(r%%%%%b\\\\\\rsi",
  };
  for (const auto& test_case : test_cases) {
    EXPECT_EQ(test_case, HeaderParser::translateMetadataFormat(test_case));
  }
}

TEST(HeaderParser, TestPerFilterStateTranslator) {
  struct TestCase {
    std::string input_;
    std::string expected_output_;
  };
  static const TestCase test_cases[] = {
      {"%PER_REQUEST_STATE(some-state)%", "%FILTER_STATE(some-state:PLAIN)%"},
      {"%PER_REQUEST_STATE(some-state:other-state)%",
       "%FILTER_STATE(some-state:other-state:PLAIN)%"},
      {"%PER_REQUEST_STATE(some-state)% %PER_REQUEST_STATE(other-state)%",
       "%FILTER_STATE(some-state:PLAIN)% %FILTER_STATE(other-state:PLAIN)%"},
      {"%PER_REQUEST_STATE(\\0)%", "%FILTER_STATE(\\0:PLAIN)%"},
      {"%PER_REQUEST_STATE(\\1)%", "%FILTER_STATE(\\1:PLAIN)%"},
  };

  for (const auto& test_case : test_cases) {
    EXPECT_EQ(test_case.expected_output_, HeaderParser::translatePerRequestState(test_case.input_));
  }
}

TEST(HeaderParserTest, EvaluateHeaders) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "x-client-ip"
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-client-ip-port"
      value: "%DOWNSTREAM_REMOTE_ADDRESS%"
  - header:
      key: "x-client-port"
      value: "%DOWNSTREAM_REMOTE_PORT%"
    append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV3Yaml(yaml).request_headers_to_add()).value();
  Http::TestRequestHeaderMapImpl header_map{{":method", "POST"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_TRUE(header_map.has("x-client-ip"));
  EXPECT_TRUE(header_map.has("x-client-ip-port"));
  EXPECT_TRUE(header_map.has("x-client-port"));
}

TEST(HeaderParserTest, EvaluateHeadersAppendIfEmpty) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "x-upstream-remote-address"
      value: "%UPSTREAM_REMOTE_ADDRESS%"
    append_action: APPEND_IF_EXISTS_OR_ADD
    keep_empty_value: true
  - header:
      key: "x-upstream-local-port"
      value: "%UPSTREAM_LOCAL_PORT%"
    append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV3Yaml(yaml).request_headers_to_add()).value();
  Http::TestRequestHeaderMapImpl header_map{{":method", "POST"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  stream_info.upstreamInfo()->setUpstreamHost(nullptr);
  stream_info.upstreamInfo()->setUpstreamLocalAddress(nullptr);
  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_FALSE(header_map.has("x-upstream-local-port"));
  EXPECT_TRUE(header_map.has("x-upstream-remote-address"));
}

TEST(HeaderParserTest, EvaluateHeadersWithNullStreamInfo) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "x-client-ip"
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-client-ip-port"
      value: "%DOWNSTREAM_REMOTE_ADDRESS%"
  - header:
      key: "x-client-port"
      value: "%DOWNSTREAM_REMOTE_PORT%"
    append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV3Yaml(yaml).request_headers_to_add()).value();
  Http::TestRequestHeaderMapImpl header_map{{":method", "POST"}};
  req_header_parser->evaluateHeaders(header_map, nullptr);
  EXPECT_TRUE(header_map.has("x-client-ip"));
  EXPECT_TRUE(header_map.has("x-client-ip-port"));
  EXPECT_TRUE(header_map.has("x-client-port"));
  EXPECT_EQ("%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%", header_map.get_("x-client-ip"));
  EXPECT_EQ("%DOWNSTREAM_REMOTE_ADDRESS%", header_map.get_("x-client-ip-port"));
  EXPECT_EQ("%DOWNSTREAM_REMOTE_PORT%", header_map.get_("x-client-port"));
}

TEST(HeaderParserTest, EvaluateHeaderValuesWithNullStreamInfo) {
  Http::TestRequestHeaderMapImpl header_map{{":method", "POST"}};
  Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue> headers_values;

  auto& first_entry = *headers_values.Add();
  first_entry.set_key("key");

  // This tests when we have "StreamInfoHeaderFormatter", but stream info is null.
  first_entry.set_value("%DOWNSTREAM_REMOTE_ADDRESS%");

  HeaderParserPtr req_header_parser_add =
      HeaderParser::configure(headers_values, HeaderValueOption::APPEND_IF_EXISTS_OR_ADD).value();
  req_header_parser_add->evaluateHeaders(header_map, nullptr);
  EXPECT_TRUE(header_map.has("key"));
  EXPECT_EQ("%DOWNSTREAM_REMOTE_ADDRESS%", header_map.get_("key"));

  headers_values.Clear();
  auto& set_entry = *headers_values.Add();
  set_entry.set_key("key");
  set_entry.set_value("great");

  HeaderParserPtr req_header_parser_set =
      HeaderParser::configure(headers_values, HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD)
          .value();
  req_header_parser_set->evaluateHeaders(header_map, nullptr);
  EXPECT_TRUE(header_map.has("key"));
  EXPECT_EQ("great", header_map.get_("key"));

  headers_values.Clear();
  auto& empty_entry = *headers_values.Add();
  empty_entry.set_key("empty");
  empty_entry.set_value("");

  HeaderParserPtr req_header_parser_empty =
      HeaderParser::configure(headers_values, HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD)
          .value();
  req_header_parser_empty->evaluateHeaders(header_map, nullptr);
  EXPECT_FALSE(header_map.has("empty"));
}

TEST(HeaderParserTest, EvaluateEmptyHeaders) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "x-key"
      value: "%UPSTREAM_METADATA([\"namespace\", \"key\"])%"
    append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV3Yaml(yaml).request_headers_to_add()).value();
  Http::TestRequestHeaderMapImpl header_map{{":method", "POST"}};
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  stream_info.upstreamInfo()->setUpstreamHost(host);
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));
  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_FALSE(header_map.has("x-key"));
}

TEST(HeaderParserTest, EvaluateStaticHeaders) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "static-header"
      value: "static-value"
    append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV3Yaml(yaml).request_headers_to_add()).value();
  Http::TestRequestHeaderMapImpl header_map{{":method", "POST"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_TRUE(header_map.has("static-header"));
  EXPECT_EQ("static-value", header_map.get_("static-header"));
}

TEST(HeaderParserTest, EvaluateCompoundHeaders) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
request_headers_to_add:
  - header:
      key: "x-prefix"
      value: "prefix-%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
  - header:
      key: "x-suffix"
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%-suffix"
  - header:
      key: "x-both"
      value: "prefix-%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%-suffix"
  - header:
      key: "x-escaping-1"
      value: "%%%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%%%"
  - header:
      key: "x-escaping-2"
      value: "%%%%%%"
  - header:
      key: "x-multi"
      value: "%PROTOCOL% from %DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
  - header:
      key: "x-multi-back-to-back"
      value: "%PROTOCOL%%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
  - header:
      key: "x-metadata"
      value: "%UPSTREAM_METADATA([\"namespace\", \"%key%\"])%"
  - header:
      key: "x-per-request"
      value: "%PER_REQUEST_STATE(testing)%"
request_headers_to_remove: ["x-nope"]
  )EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr req_header_parser =
      HeaderParser::configure(route.request_headers_to_add(), route.request_headers_to_remove())
          .value();
  Http::TestRequestHeaderMapImpl header_map{
      {":method", "POST"}, {"x-safe", "safe"}, {"x-nope", "nope"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  absl::optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(stream_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  stream_info.upstreamInfo()->setUpstreamHost(host);

  // Metadata with percent signs in the key.
  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>(
      TestUtility::parseYaml<envoy::config::core::v3::Metadata>(
          R"EOF(
        filter_metadata:
          namespace:
            "%key%": value
      )EOF"));
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));

  Envoy::StreamInfo::FilterStateSharedPtr filter_state(
      std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
          Envoy::StreamInfo::FilterState::LifeSpan::FilterChain));
  filter_state->setData("testing", std::make_unique<StringAccessorImpl>("test_value"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(*filter_state));

  req_header_parser->evaluateHeaders(header_map, stream_info);

  EXPECT_TRUE(header_map.has("x-prefix"));
  EXPECT_EQ("prefix-127.0.0.1", header_map.get_("x-prefix"));

  EXPECT_TRUE(header_map.has("x-suffix"));
  EXPECT_EQ("127.0.0.1-suffix", header_map.get_("x-suffix"));

  EXPECT_TRUE(header_map.has("x-both"));
  EXPECT_EQ("prefix-127.0.0.1-suffix", header_map.get_("x-both"));

  EXPECT_TRUE(header_map.has("x-escaping-1"));
  EXPECT_EQ("%127.0.0.1%", header_map.get_("x-escaping-1"));

  EXPECT_TRUE(header_map.has("x-escaping-2"));
  EXPECT_EQ("%%%", header_map.get_("x-escaping-2"));

  EXPECT_TRUE(header_map.has("x-multi"));
  EXPECT_EQ("HTTP/1.1 from 127.0.0.1", header_map.get_("x-multi"));

  EXPECT_TRUE(header_map.has("x-multi-back-to-back"));
  EXPECT_EQ("HTTP/1.1127.0.0.1", header_map.get_("x-multi-back-to-back"));

  EXPECT_TRUE(header_map.has("x-metadata"));
  EXPECT_EQ("value", header_map.get_("x-metadata"));

  EXPECT_TRUE(header_map.has("x-per-request"));
  EXPECT_EQ("test_value", header_map.get_("x-per-request"));

  EXPECT_TRUE(header_map.has("x-safe"));
  EXPECT_FALSE(header_map.has("x-nope"));
}

TEST(HeaderParserTest, EvaluateHeadersWithAppendFalse) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "static-header"
      value: "static-value"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-client-ip"
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start"
      value: "%START_TIME(%s%3f)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start-default"
      value: "%START_TIME%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start-range"
      value: "%START_TIME(%f, %1f, %2f, %3f, %4f, %5f, %6f, %7f, %8f, %9f)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
)EOF";

  // Disable append mode.
  envoy::config::route::v3::Route route = parseRouteFromV3Yaml(yaml);
  route.mutable_request_headers_to_add(0)->set_append_action(
      envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
  route.mutable_request_headers_to_add(1)->set_append_action(
      envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
  route.mutable_request_headers_to_add(2)->set_append_action(
      envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);

  HeaderParserPtr req_header_parser =
      Router::HeaderParser::configure(route.request_headers_to_add()).value();
  Http::TestRequestHeaderMapImpl header_map{
      {":method", "POST"}, {"static-header", "old-value"}, {"x-client-ip", "0.0.0.0"}};

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  const SystemTime start_time(std::chrono::microseconds(1522796769123456));
  EXPECT_CALL(stream_info, startTime()).Times(3).WillRepeatedly(Return(start_time));

  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_TRUE(header_map.has("static-header"));
  EXPECT_EQ("static-value", header_map.get_("static-header"));
  EXPECT_TRUE(header_map.has("x-client-ip"));
  EXPECT_EQ("127.0.0.1", header_map.get_("x-client-ip"));
  EXPECT_TRUE(header_map.has("x-request-start"));
  EXPECT_EQ("1522796769123", header_map.get_("x-request-start"));
  EXPECT_TRUE(header_map.has("x-request-start-default"));
  EXPECT_EQ("2018-04-03T23:06:09.123Z", header_map.get_("x-request-start-default"));
  EXPECT_TRUE(header_map.has("x-request-start-range"));
  EXPECT_EQ("123456000, 1, 12, 123, 1234, 12345, 123456, 1234560, 12345600, 123456000",
            header_map.get_("x-request-start-range"));

  using CountMap = absl::flat_hash_map<std::string, int>;
  CountMap counts;
  header_map.iterate([&counts](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    absl::string_view key = header.key().getStringView();
    CountMap::iterator i = counts.find(key);
    if (i == counts.end()) {
      counts.insert({std::string(key), 1});
    } else {
      i->second++;
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  EXPECT_EQ(1, counts["static-header"]);
  EXPECT_EQ(1, counts["x-client-ip"]);
  EXPECT_EQ(1, counts["x-request-start"]);
}

TEST(HeaderParserTest, EvaluateResponseHeaders) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
response_headers_to_add:
  - header:
      key: "x-client-ip"
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-client-ip-port"
      value: "%DOWNSTREAM_REMOTE_ADDRESS%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start"
      value: "%START_TIME(%s.%3f)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start-multiple"
      value: "%START_TIME(%s.%3f)% %START_TIME% %START_TIME(%s)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start-f"
      value: "%START_TIME(f)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start-range"
      value: "%START_TIME(%f, %1f, %2f, %3f, %4f, %5f, %6f, %7f, %8f, %9f)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-request-start-default"
      value: "%START_TIME%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "set-cookie"
      value: "foo"
  - header:
      key: "set-cookie"
      value: "bar"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-upstream-req-id"
      value: "%RESP(x-resp-id)%"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-downstream-req-id"
      value: "%REQ(x-req-id)%"
    append_action: APPEND_IF_EXISTS_OR_ADD

response_headers_to_remove: ["x-nope"]
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove())
          .value();
  Http::TestRequestHeaderMapImpl request_header_map{{":method", "POST"}, {"x-req-id", "543"}};
  Http::TestResponseHeaderMapImpl response_header_map{
      {"x-safe", "safe"}, {"x-nope", "nope"}, {"x-resp-id", "321"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // Initialize start_time as 2018-04-03T23:06:09.123Z in microseconds.
  const SystemTime start_time(std::chrono::microseconds(1522796769123456));
  EXPECT_CALL(stream_info, startTime()).Times(7).WillRepeatedly(Return(start_time));

  resp_header_parser->evaluateHeaders(response_header_map,
                                      {&request_header_map, &response_header_map}, stream_info);
  EXPECT_TRUE(response_header_map.has("x-client-ip"));
  EXPECT_TRUE(response_header_map.has("x-client-ip-port"));
  EXPECT_TRUE(response_header_map.has("x-request-start-multiple"));
  EXPECT_TRUE(response_header_map.has("x-safe"));
  EXPECT_FALSE(response_header_map.has("x-nope"));
  EXPECT_TRUE(response_header_map.has("x-request-start"));
  EXPECT_EQ("1522796769.123", response_header_map.get_("x-request-start"));
  EXPECT_EQ("1522796769.123 2018-04-03T23:06:09.123Z 1522796769",
            response_header_map.get_("x-request-start-multiple"));
  EXPECT_TRUE(response_header_map.has("x-request-start-f"));
  EXPECT_EQ("f", response_header_map.get_("x-request-start-f"));
  EXPECT_TRUE(response_header_map.has("x-request-start-default"));
  EXPECT_EQ("2018-04-03T23:06:09.123Z", response_header_map.get_("x-request-start-default"));
  EXPECT_TRUE(response_header_map.has("x-request-start-range"));
  EXPECT_EQ("123456000, 1, 12, 123, 1234, 12345, 123456, 1234560, 12345600, 123456000",
            response_header_map.get_("x-request-start-range"));
  EXPECT_EQ("foo", response_header_map.get_("set-cookie"));
  EXPECT_EQ("321", response_header_map.get_("x-upstream-req-id"));
  EXPECT_EQ("543", response_header_map.get_("x-downstream-req-id"));

  // Per https://github.com/envoyproxy/envoy/issues/7488 make sure we don't
  // combine set-cookie headers
  const auto out = response_header_map.get(Http::LowerCaseString("set-cookie"));
  ASSERT_EQ(out.size(), 2);
  ASSERT_EQ(out[0]->value().getStringView(), "foo");
  ASSERT_EQ(out[1]->value().getStringView(), "bar");
}

TEST(HeaderParserTest, EvaluateRequestHeadersRemoveBeforeAdd) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
request_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "bar"
request_headers_to_remove: ["x-foo-header"]
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr req_header_parser =
      HeaderParser::configure(route.request_headers_to_add(), route.request_headers_to_remove())
          .value();
  Http::TestRequestHeaderMapImpl header_map{{"x-foo-header", "foo"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_EQ("bar", header_map.get_("x-foo-header"));
}

TEST(HeaderParserTest, EvaluateRequestHeadersAddIfAbsent) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "foo"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-bar-header"
      value: "bar"
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
  - header:
      key: "x-per-header"
      value: "per"
    append_action: ADD_IF_ABSENT
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add()).value();
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  {
    Http::TestResponseHeaderMapImpl header_map;
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ("foo", header_map.get_("x-foo-header"));
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
    EXPECT_EQ("per", header_map.get_("x-per-header"));
  }

  {
    Http::TestResponseHeaderMapImpl header_map{{"x-foo-header", "exist-foo"}};
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ(2, header_map.get(Http::LowerCaseString("x-foo-header")).size());
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
    EXPECT_EQ("per", header_map.get_("x-per-header"));
  }

  {
    Http::TestResponseHeaderMapImpl header_map{{"x-bar-header", "exist-bar"}};
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ("foo", header_map.get_("x-foo-header"));
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
    EXPECT_EQ(1, header_map.get(Http::LowerCaseString("x-bar-header")).size());
    EXPECT_EQ("per", header_map.get_("x-per-header"));
  }

  {
    Http::TestResponseHeaderMapImpl header_map{{"x-per-header", "exist-per"}};
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ("foo", header_map.get_("x-foo-header"));
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
    EXPECT_EQ("exist-per", header_map.get_("x-per-header"));
  }
}

TEST(HeaderParserTest, EvaluateHeadersOverwriteIfPresent) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "foo"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-bar-header"
      value: "bar"
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
  - header:
      key: "x-per-header"
      value: "per"
    append_action: ADD_IF_ABSENT
  - header:
      key: "x-overwrite"
      value: "overwritten"
    append_action: OVERWRITE_IF_EXISTS
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add()).value();
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  {
    Http::TestResponseHeaderMapImpl header_map;
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ("foo", header_map.get_("x-foo-header"));
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
    EXPECT_EQ("per", header_map.get_("x-per-header"));
    // If the response headers do not contain "x-overwrite" header,
    // the final set should also not contain it.
    EXPECT_FALSE(header_map.has("x-overwrite"));
  }

  {
    Http::TestResponseHeaderMapImpl header_map{{"x-overwrite", "to overwrite"}};
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ("foo", header_map.get_("x-foo-header"));
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
    EXPECT_EQ("per", header_map.get_("x-per-header"));
    // If the response headers contain "x-overwrite" header it should
    // be overwritten with the new value.
    EXPECT_EQ("overwritten", header_map.get_("x-overwrite"));
  }
}

TEST(HeaderParserTest, DEPRECATED_FEATURE_TEST(EvaluateRequestHeadersAddWithDeprecatedAppend)) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "foo"
    append: true
  - header:
      key: "x-bar-header"
      value: "bar"
    append: false
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add()).value();
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  {
    Http::TestResponseHeaderMapImpl header_map;
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ("foo", header_map.get_("x-foo-header"));
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
  }

  {
    Http::TestResponseHeaderMapImpl header_map{{"x-foo-header", "exist-foo"}};
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ(2, header_map.get(Http::LowerCaseString("x-foo-header")).size());
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
  }

  {
    Http::TestResponseHeaderMapImpl header_map{{"x-bar-header", "exist-bar"}};
    resp_header_parser->evaluateHeaders(header_map, stream_info);
    EXPECT_EQ("foo", header_map.get_("x-foo-header"));
    EXPECT_EQ("bar", header_map.get_("x-bar-header"));
    EXPECT_EQ(1, header_map.get(Http::LowerCaseString("x-bar-header")).size());
  }
}

TEST(HeaderParserTest,
     DEPRECATED_FEATURE_TEST(EvaluateRequestHeadersAddWithDeprecatedAppendAndAction)) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "foo"
    append: true
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
  - header:
      key: "x-bar-header"
      value: "bar"
    append: false
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);

  EXPECT_EQ(HeaderParser::configure(route.response_headers_to_add()).status().message(),
            "Both append and append_action are set and it's not allowed");
}

TEST(HeaderParserTest, EvaluateResponseHeadersRemoveBeforeAdd) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "bar"
response_headers_to_remove: ["x-foo-header"]
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove())
          .value();
  Http::TestResponseHeaderMapImpl header_map{{"x-foo-header", "foo"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  resp_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_EQ("bar", header_map.get_("x-foo-header"));
}

TEST(HeaderParserTest, GetHeaderTransformsWithFormatting) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "foo"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-bar-header"
      value: "bar"
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
  - header:
      key: "x-per-request-header"
      value: "%PER_REQUEST_STATE(testing)%"
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
response_headers_to_remove: ["x-baz-header"]
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove())
          .value();
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  Envoy::StreamInfo::FilterStateSharedPtr filter_state(
      std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
          Envoy::StreamInfo::FilterState::LifeSpan::FilterChain));
  filter_state->setData("testing", std::make_unique<StringAccessorImpl>("test_value"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(*filter_state));

  auto transforms = resp_header_parser->getHeaderTransforms(stream_info);
  EXPECT_THAT(transforms.headers_to_append_or_add,
              ElementsAre(Pair(Http::LowerCaseString("x-foo-header"), "foo")));
  EXPECT_THAT(transforms.headers_to_overwrite_or_add,
              ElementsAre(Pair(Http::LowerCaseString("x-bar-header"), "bar"),
                          Pair(Http::LowerCaseString("x-per-request-header"), "test_value")));
  EXPECT_THAT(transforms.headers_to_remove, ElementsAre(Http::LowerCaseString("x-baz-header")));
}

TEST(HeaderParserTest, GetHeaderTransformsOriginalValues) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "foo"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-bar-header"
      value: "bar"
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
  - header:
      key: "x-per-request-header"
      value: "%PER_REQUEST_STATE(testing)%"
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
response_headers_to_remove: ["x-baz-header"]
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr response_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove())
          .value();
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  Envoy::StreamInfo::FilterStateSharedPtr filter_state(
      std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
          Envoy::StreamInfo::FilterState::LifeSpan::FilterChain));
  filter_state->setData("testing", std::make_unique<StringAccessorImpl>("test_value"),
                        StreamInfo::FilterState::StateType::ReadOnly,
                        StreamInfo::FilterState::LifeSpan::FilterChain);
  ON_CALL(stream_info, filterState()).WillByDefault(ReturnRef(filter_state));
  ON_CALL(Const(stream_info), filterState()).WillByDefault(ReturnRef(*filter_state));

  auto transforms =
      response_header_parser->getHeaderTransforms(stream_info, /*do_formatting=*/false);
  EXPECT_THAT(transforms.headers_to_append_or_add,
              ElementsAre(Pair(Http::LowerCaseString("x-foo-header"), "foo")));
  EXPECT_THAT(transforms.headers_to_overwrite_or_add,
              ElementsAre(Pair(Http::LowerCaseString("x-bar-header"), "bar"),
                          Pair(Http::LowerCaseString("x-per-request-header"),
                               "%PER_REQUEST_STATE(testing)%")));
  EXPECT_THAT(transforms.headers_to_remove, ElementsAre(Http::LowerCaseString("x-baz-header")));
}

TEST(HeaderParserTest, GetHeaderTransformsForAllActions) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
response_headers_to_add:
  - header:
      key: "x-foo-header"
      value: "foo"
    append_action: APPEND_IF_EXISTS_OR_ADD
  - header:
      key: "x-bar-header"
      value: "bar"
    append_action: OVERWRITE_IF_EXISTS_OR_ADD
  - header:
      key: "x-per-header"
      value: "per"
    append_action: ADD_IF_ABSENT
response_headers_to_remove: ["x-baz-header"]
)EOF";

  const auto route = parseRouteFromV3Yaml(yaml);
  HeaderParserPtr response_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove())
          .value();
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  auto transforms =
      response_header_parser->getHeaderTransforms(stream_info, /*do_formatting=*/false);
  EXPECT_THAT(transforms.headers_to_append_or_add,
              ElementsAre(Pair(Http::LowerCaseString("x-foo-header"), "foo")));
  EXPECT_THAT(transforms.headers_to_overwrite_or_add,
              ElementsAre(Pair(Http::LowerCaseString("x-bar-header"), "bar")));
  EXPECT_THAT(transforms.headers_to_add_if_absent,
              ElementsAre(Pair(Http::LowerCaseString("x-per-header"), "per")));

  EXPECT_THAT(transforms.headers_to_remove, ElementsAre(Http::LowerCaseString("x-baz-header")));
}

} // namespace
} // namespace Router
} // namespace Envoy

#include <memory>

#include "common/router/header_parser.h"
#include "common/router/string_accessor_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

static envoy::config::route::v3::Route parseRouteFromV2Yaml(const std::string& yaml) {
  envoy::config::route::v3::Route route;
  TestUtility::loadFromYaml(yaml, route);
  return route;
}

TEST(HeaderParserTest, TestParseInternal) {
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
      {"%PER_REQUEST_STATE(testing)%", {"test_value"}, {}},
      {"%REQ(x-request-id)%", {"123"}, {}},
      {"%START_TIME%", {"2018-04-03T23:06:09.123Z"}, {}},
      {"%RESPONSE_FLAGS%", {"LR"}, {}},
      {"%RESPONSE_CODE_DETAILS%", {"via_upstream"}, {}},

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
      {"%UPSTREAM_METADATA(no array)%",
       {},
       {"Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", \"k\", "
        "...]), actual format UPSTREAM_METADATA(no array), because JSON supplied is not valid. "
        "Error(offset 1, line 1): Invalid value.\n"}},
      {"%UPSTREAM_METADATA( no array)%",
       {},
       {"Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", \"k\", "
        "...]), actual format UPSTREAM_METADATA( no array), because JSON supplied is not valid. "
        "Error(offset 2, line 1): Invalid value.\n"}},
      {"%UPSTREAM_METADATA([\"unterminated array\")%",
       {},
       {"Invalid header configuration. Expecting ',', ']', or whitespace after "
        "'UPSTREAM_METADATA([\"unterminated array\"', but found ')'"}},
      {"%UPSTREAM_METADATA([not-a-string])%",
       {},
       {"Invalid header configuration. Expecting '\"' or whitespace after 'UPSTREAM_METADATA([', "
        "but found 'n'"}},
      {"%UPSTREAM_METADATA([\"\\",
       {},
       {"Invalid header configuration. Un-terminated backslash in JSON string after "
        "'UPSTREAM_METADATA([\"'"}},
      {"%UPSTREAM_METADATA([\"ns\", \"key\"]x",
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
      {"%UPSTREAM_METADATA([\"ns\"])%",
       {},
       {"Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", \"k\", "
        "...]), actual format UPSTREAM_METADATA([\"ns\"])"}},
      {"%START_TIME(%85n)%", {}, {"Invalid header configuration. Format string contains newline."}},

  };

  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  absl::optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(stream_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  ON_CALL(stream_info, upstreamHost()).WillByDefault(Return(host));

  Http::RequestHeaderMapImpl request_headers;
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

  ON_CALL(stream_info, hasResponseFlag(StreamInfo::ResponseFlag::LocalReset))
      .WillByDefault(Return(true));

  absl::optional<std::string> rc_details{"via_upstream"};
  ON_CALL(stream_info, responseCodeDetails()).WillByDefault(ReturnRef(rc_details));

  for (const auto& test_case : test_cases) {
    Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption> to_add;
    envoy::config::core::v3::HeaderValueOption* header = to_add.Add();
    header->mutable_header()->set_key("x-header");
    header->mutable_header()->set_value(test_case.input_);

    if (test_case.expected_exception_) {
      EXPECT_FALSE(test_case.expected_output_);
      EXPECT_THROW_WITH_MESSAGE(HeaderParser::configure(to_add), EnvoyException,
                                test_case.expected_exception_.value());
      continue;
    }

    HeaderParserPtr req_header_parser = HeaderParser::configure(to_add);

    Http::TestHeaderMapImpl header_map{{":method", "POST"}};
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

TEST(HeaderParserTest, EvaluateHeaders) {
  const std::string ymal = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "x-client-ip"
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
    append: true
  - header:
      key: "x-client-ip-port"
      value: "%DOWNSTREAM_REMOTE_ADDRESS%"
    append: true
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV2Yaml(ymal).request_headers_to_add());
  Http::TestHeaderMapImpl header_map{{":method", "POST"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_TRUE(header_map.has("x-client-ip"));
  EXPECT_TRUE(header_map.has("x-client-ip-port"));
}

TEST(HeaderParserTest, EvaluateEmptyHeaders) {
  const std::string ymal = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "x-key"
      value: "%UPSTREAM_METADATA([\"namespace\", \"key\"])%"
    append: true
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV2Yaml(ymal).request_headers_to_add());
  Http::TestHeaderMapImpl header_map{{":method", "POST"}};
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  auto metadata = std::make_shared<envoy::config::core::v3::Metadata>();
  ON_CALL(stream_info, upstreamHost()).WillByDefault(Return(host));
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));
  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_FALSE(header_map.has("x-key"));
}

TEST(HeaderParserTest, EvaluateStaticHeaders) {
  const std::string ymal = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "static-header"
      value: "static-value"
    append: true
)EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV2Yaml(ymal).request_headers_to_add());
  Http::TestHeaderMapImpl header_map{{":method", "POST"}};
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

  const auto route = parseRouteFromV2Yaml(yaml);
  HeaderParserPtr req_header_parser =
      HeaderParser::configure(route.request_headers_to_add(), route.request_headers_to_remove());
  Http::TestHeaderMapImpl header_map{{":method", "POST"}, {"x-safe", "safe"}, {"x-nope", "nope"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
  absl::optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(stream_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  ON_CALL(stream_info, upstreamHost()).WillByDefault(Return(host));

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
  const std::string ymal = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: "www2"
  prefix_rewrite: "/api/new_endpoint"
request_headers_to_add:
  - header:
      key: "static-header"
      value: "static-value"
    append: true
  - header:
      key: "x-client-ip"
      value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
    append: true
  - header:
      key: "x-request-start"
      value: "%START_TIME(%s%3f)%"
    append: true
  - header:
      key: "x-request-start-default"
      value: "%START_TIME%"
    append: true
  - header:
      key: "x-request-start-range"
      value: "%START_TIME(%f, %1f, %2f, %3f, %4f, %5f, %6f, %7f, %8f, %9f)%"
    append: true
)EOF";

  // Disable append mode.
  envoy::config::route::v3::Route route = parseRouteFromV2Yaml(ymal);
  route.mutable_request_headers_to_add(0)->mutable_append()->set_value(false);
  route.mutable_request_headers_to_add(1)->mutable_append()->set_value(false);
  route.mutable_request_headers_to_add(2)->mutable_append()->set_value(false);

  HeaderParserPtr req_header_parser =
      Router::HeaderParser::configure(route.request_headers_to_add());
  Http::TestHeaderMapImpl header_map{
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
  header_map.iterate(
      [](const Http::HeaderEntry& header, void* cb_v) -> Http::HeaderMap::Iterate {
        CountMap* m = static_cast<CountMap*>(cb_v);
        absl::string_view key = header.key().getStringView();
        CountMap::iterator i = m->find(key);
        if (i == m->end()) {
          m->insert({std::string(key), 1});
        } else {
          i->second++;
        }
        return Http::HeaderMap::Iterate::Continue;
      },
      &counts);

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
    append: true
  - header:
      key: "x-client-ip-port"
      value: "%DOWNSTREAM_REMOTE_ADDRESS%"
    append: true
  - header:
      key: "x-request-start"
      value: "%START_TIME(%s.%3f)%"
    append: true
  - header:
      key: "x-request-start-multiple"
      value: "%START_TIME(%s.%3f)% %START_TIME% %START_TIME(%s)%"
    append: true
  - header:
      key: "x-request-start-f"
      value: "%START_TIME(f)%"
    append: true
  - header:
      key: "x-request-start-range"
      value: "%START_TIME(%f, %1f, %2f, %3f, %4f, %5f, %6f, %7f, %8f, %9f)%"
    append: true
  - header:
      key: "x-request-start-default"
      value: "%START_TIME%"
    append: true
  - header:
      key: "set-cookie"
      value: "foo"
  - header:
      key: "set-cookie"
      value: "bar"
    append: true

response_headers_to_remove: ["x-nope"]
)EOF";

  const auto route = parseRouteFromV2Yaml(yaml);
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove());
  Http::TestHeaderMapImpl header_map{{":method", "POST"}, {"x-safe", "safe"}, {"x-nope", "nope"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  // Initialize start_time as 2018-04-03T23:06:09.123Z in microseconds.
  const SystemTime start_time(std::chrono::microseconds(1522796769123456));
  EXPECT_CALL(stream_info, startTime()).Times(7).WillRepeatedly(Return(start_time));

  resp_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_TRUE(header_map.has("x-client-ip"));
  EXPECT_TRUE(header_map.has("x-client-ip-port"));
  EXPECT_TRUE(header_map.has("x-request-start-multiple"));
  EXPECT_TRUE(header_map.has("x-safe"));
  EXPECT_FALSE(header_map.has("x-nope"));
  EXPECT_TRUE(header_map.has("x-request-start"));
  EXPECT_EQ("1522796769.123", header_map.get_("x-request-start"));
  EXPECT_EQ("1522796769.123 2018-04-03T23:06:09.123Z 1522796769",
            header_map.get_("x-request-start-multiple"));
  EXPECT_TRUE(header_map.has("x-request-start-f"));
  EXPECT_EQ("f", header_map.get_("x-request-start-f"));
  EXPECT_TRUE(header_map.has("x-request-start-default"));
  EXPECT_EQ("2018-04-03T23:06:09.123Z", header_map.get_("x-request-start-default"));
  EXPECT_TRUE(header_map.has("x-request-start-range"));
  EXPECT_EQ("123456000, 1, 12, 123, 1234, 12345, 123456, 1234560, 12345600, 123456000",
            header_map.get_("x-request-start-range"));
  EXPECT_EQ("foo", header_map.get_("set-cookie"));

  // Per https://github.com/envoyproxy/envoy/issues/7488 make sure we don't
  // combine set-cookie headers
  std::vector<absl::string_view> out;
  Http::HeaderUtility::getAllOfHeader(header_map, "set-cookie", out);
  ASSERT_EQ(out.size(), 2);
  ASSERT_EQ(out[0], "foo");
  ASSERT_EQ(out[1], "bar");
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

  const auto route = parseRouteFromV2Yaml(yaml);
  HeaderParserPtr req_header_parser =
      HeaderParser::configure(route.request_headers_to_add(), route.request_headers_to_remove());
  Http::TestHeaderMapImpl header_map{{"x-foo-header", "foo"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  req_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_EQ("bar", header_map.get_("x-foo-header"));
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

  const auto route = parseRouteFromV2Yaml(yaml);
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove());
  Http::TestHeaderMapImpl header_map{{"x-foo-header", "foo"}};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;

  resp_header_parser->evaluateHeaders(header_map, stream_info);
  EXPECT_EQ("bar", header_map.get_("x-foo-header"));
}

} // namespace
} // namespace Router
} // namespace Envoy

#include <string>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/http/protocol.h"

#include "common/config/metadata.h"
#include "common/config/rds_json.h"
#include "common/request_info/filter_state_impl.h"
#include "common/router/header_formatter.h"
#include "common/router/header_parser.h"
#include "common/router/string_accessor_impl.h"

#include "test/common/request_info/test_int_accessor.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Router {

static envoy::api::v2::route::Route parseRouteFromJson(const std::string& json_string) {
  envoy::api::v2::route::Route route;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Envoy::Config::RdsJson::translateRoute(*json_object_ptr, route);
  return route;
}

static envoy::api::v2::route::Route parseRouteFromV2Yaml(const std::string& yaml) {
  envoy::api::v2::route::Route route;
  MessageUtil::loadFromYaml(yaml, route);
  return route;
}

class RequestInfoHeaderFormatterTest : public testing::Test {
public:
  void testFormatting(const Envoy::RequestInfo::MockRequestInfo& request_info,
                      const std::string& variable, const std::string& expected_output) {
    {
      auto f = RequestInfoHeaderFormatter(variable, false);
      const std::string formatted_string = f.format(request_info);
      EXPECT_EQ(expected_output, formatted_string);
    }
  }

  void testFormatting(const std::string& variable, const std::string& expected_output) {
    NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
    testFormatting(request_info, variable, expected_output);
  }

  void testInvalidFormat(const std::string& variable) {
    EXPECT_THROW_WITH_MESSAGE(RequestInfoHeaderFormatter(variable, false), EnvoyException,
                              fmt::format("field '{}' not supported as custom header", variable));
  }
};

TEST_F(RequestInfoHeaderFormatterTest, TestFormatWithDownstreamRemoteAddressVariable) {
  testFormatting("DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT", "127.0.0.1");
}

TEST_F(RequestInfoHeaderFormatterTest, TestFormatWithDownstreamLocalAddressVariable) {
  testFormatting("DOWNSTREAM_LOCAL_ADDRESS", "127.0.0.2:0");
}

TEST_F(RequestInfoHeaderFormatterTest, TestFormatWithDownstreamLocalAddressWithoutPortVariable) {
  testFormatting("DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT", "127.0.0.2");
}

TEST_F(RequestInfoHeaderFormatterTest, TestFormatWithProtocolVariable) {
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  absl::optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(request_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  testFormatting(request_info, "PROTOCOL", "HTTP/1.1");
}

TEST_F(RequestInfoHeaderFormatterTest, TestFormatWithUpstreamMetadataVariable) {
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());

  auto metadata = std::make_shared<envoy::api::v2::core::Metadata>(
      TestUtility::parseYaml<envoy::api::v2::core::Metadata>(
          R"EOF(
        filter_metadata:
          namespace:
            key: value
            nested:
              str_key: str_value
              "escaped,key": escaped_key_value
              bool_key1: true
              bool_key2: false
              num_key1: 1
              num_key2: 3.14
              null_key: null
              list_key: [ list_element ]
              struct_key:
                deep_key: deep_value
      )EOF"));

  // Prove we're testing the expected types.
  const auto& nested_struct =
      Envoy::Config::Metadata::metadataValue(*metadata, "namespace", "nested").struct_value();
  EXPECT_EQ(nested_struct.fields().at("str_key").kind_case(), ProtobufWkt::Value::kStringValue);
  EXPECT_EQ(nested_struct.fields().at("bool_key1").kind_case(), ProtobufWkt::Value::kBoolValue);
  EXPECT_EQ(nested_struct.fields().at("bool_key2").kind_case(), ProtobufWkt::Value::kBoolValue);
  EXPECT_EQ(nested_struct.fields().at("num_key1").kind_case(), ProtobufWkt::Value::kNumberValue);
  EXPECT_EQ(nested_struct.fields().at("num_key1").kind_case(), ProtobufWkt::Value::kNumberValue);
  EXPECT_EQ(nested_struct.fields().at("null_key").kind_case(), ProtobufWkt::Value::kNullValue);
  EXPECT_EQ(nested_struct.fields().at("list_key").kind_case(), ProtobufWkt::Value::kListValue);
  EXPECT_EQ(nested_struct.fields().at("struct_key").kind_case(), ProtobufWkt::Value::kStructValue);

  ON_CALL(request_info, upstreamHost()).WillByDefault(Return(host));
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));

  // Top-level value.
  testFormatting(request_info, "UPSTREAM_METADATA([\"namespace\", \"key\"])", "value");

  // Nested string value.
  testFormatting(request_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"str_key\"])",
                 "str_value");

  // Boolean values.
  testFormatting(request_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"bool_key1\"])",
                 "true");
  testFormatting(request_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"bool_key2\"])",
                 "false");

  // Number values.
  testFormatting(request_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"num_key1\"])", "1");
  testFormatting(request_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"num_key2\"])",
                 "3.14");

  // Deeply nested value.
  testFormatting(request_info,
                 "UPSTREAM_METADATA([\"namespace\", \"nested\", \"struct_key\", \"deep_key\"])",
                 "deep_value");

  // Initial metadata lookup fails.
  testFormatting(request_info, "UPSTREAM_METADATA([\"wrong_namespace\", \"key\"])", "");
  testFormatting(request_info, "UPSTREAM_METADATA([\"namespace\", \"not_found\"])", "");
  testFormatting(request_info, "UPSTREAM_METADATA([\"namespace\", \"not_found\", \"key\"])", "");

  // Nested metadata lookup fails.
  testFormatting(request_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"not_found\"])", "");

  // Nested metadata lookup returns non-struct intermediate value.
  testFormatting(request_info, "UPSTREAM_METADATA([\"namespace\", \"key\", \"invalid\"])", "");

  // Struct values are not rendered.
  testFormatting(request_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"struct_key\"])",
                 "");

  // List values are not rendered.
  testFormatting(request_info, "UPSTREAM_METADATA([\"namespace\", \"nested\", \"list_key\"])", "");
}

// Breaks tsan/asan builds by trying to allocate a lot of memory.
// Works on debug builds and needs to be fixed. See
// https://github.com/envoyproxy/envoy/issues/4268
TEST_F(RequestInfoHeaderFormatterTest, DISABLED_UserDefinedHeadersConsideredHarmful) {
  // This must be an inline header to get the append-in-place semantics.
  const char* header_name = "connection";
  Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValueOption> to_add;
  const uint32_t num_header_chunks = 10;
  const uint64_t length = std::numeric_limits<uint32_t>::max() / num_header_chunks;
  std::string really_long_string(length + 1, 'a');
  for (uint32_t i = 0; i < num_header_chunks; ++i) {
    envoy::api::v2::core::HeaderValueOption* header = to_add.Add();
    header->mutable_header()->set_key(header_name);
    header->mutable_header()->set_value(really_long_string);
    header->mutable_append()->set_value(true);
  }

  HeaderParserPtr req_header_parser = HeaderParser::configure(to_add);

  Http::TestHeaderMapImpl header_map{{":method", "POST"}};
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  EXPECT_DEATH_LOG_TO_STDERR(req_header_parser->evaluateHeaders(header_map, request_info),
                             "Trying to allocate overly large headers.");
}

TEST_F(RequestInfoHeaderFormatterTest, TestFormatWithUpstreamMetadataVariableMissingHost) {
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host;
  ON_CALL(request_info, upstreamHost()).WillByDefault(Return(host));

  testFormatting(request_info, "UPSTREAM_METADATA([\"namespace\", \"key\"])", "");
}

TEST_F(RequestInfoHeaderFormatterTest, TestFormatWithPerRequestStateVariable) {
  Envoy::RequestInfo::FilterStateImpl per_request_state;
  per_request_state.setData("testing", std::make_unique<StringAccessorImpl>("test_value"));
  EXPECT_EQ("test_value", per_request_state.getData<StringAccessor>("testing").asString());

  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  ON_CALL(request_info, perRequestState()).WillByDefault(ReturnRef(per_request_state));
  ON_CALL(Const(request_info), perRequestState()).WillByDefault(ReturnRef(per_request_state));

  testFormatting(request_info, "PER_REQUEST_STATE(testing)", "test_value");
  testFormatting(request_info, "PER_REQUEST_STATE(testing2)", "");
  EXPECT_EQ("test_value", per_request_state.getData<StringAccessor>("testing").asString());
}

TEST_F(RequestInfoHeaderFormatterTest, TestFormatWithNonStringPerRequestStateVariable) {
  Envoy::RequestInfo::FilterStateImpl per_request_state;
  per_request_state.setData("testing", std::make_unique<RequestInfo::TestIntAccessor>(1));
  EXPECT_EQ(1, per_request_state.getData<RequestInfo::TestIntAccessor>("testing").access());

  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  ON_CALL(request_info, perRequestState()).WillByDefault(ReturnRef(per_request_state));
  ON_CALL(Const(request_info), perRequestState()).WillByDefault(ReturnRef(per_request_state));

  testFormatting(request_info, "PER_REQUEST_STATE(testing)", "");
}

TEST_F(RequestInfoHeaderFormatterTest, WrongFormatOnPerRequestStateVariable) {
  // No parameters
  EXPECT_THROW_WITH_MESSAGE(RequestInfoHeaderFormatter("PER_REQUEST_STATE()", false),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "PER_REQUEST_STATE(<data_name>), actual format "
                            "PER_REQUEST_STATE()");

  // Missing single parens
  EXPECT_THROW_WITH_MESSAGE(RequestInfoHeaderFormatter("PER_REQUEST_STATE(testing", false),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "PER_REQUEST_STATE(<data_name>), actual format "
                            "PER_REQUEST_STATE(testing");
  EXPECT_THROW_WITH_MESSAGE(RequestInfoHeaderFormatter("PER_REQUEST_STATE testing)", false),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "PER_REQUEST_STATE(<data_name>), actual format "
                            "PER_REQUEST_STATE testing)");
}

TEST_F(RequestInfoHeaderFormatterTest, UnknownVariable) { testInvalidFormat("INVALID_VARIABLE"); }

TEST_F(RequestInfoHeaderFormatterTest, WrongFormatOnUpstreamMetadataVariable) {
  // Invalid JSON.
  EXPECT_THROW_WITH_MESSAGE(RequestInfoHeaderFormatter("UPSTREAM_METADATA(abcd)", false),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA(abcd), because JSON supplied is not valid. "
                            "Error(offset 0, line 1): Invalid value.\n");

  // No parameters.
  EXPECT_THROW_WITH_MESSAGE(RequestInfoHeaderFormatter("UPSTREAM_METADATA", false), EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA");

  EXPECT_THROW_WITH_MESSAGE(RequestInfoHeaderFormatter("UPSTREAM_METADATA()", false),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA(), because JSON supplied is not valid. "
                            "Error(offset 0, line 1): The document is empty.\n");

  // One parameter.
  EXPECT_THROW_WITH_MESSAGE(RequestInfoHeaderFormatter("UPSTREAM_METADATA([\"ns\"])", false),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA([\"ns\"])");

  // Missing close paren.
  EXPECT_THROW_WITH_MESSAGE(RequestInfoHeaderFormatter("UPSTREAM_METADATA(", false), EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA(");

  EXPECT_THROW_WITH_MESSAGE(RequestInfoHeaderFormatter("UPSTREAM_METADATA([a,b,c,d]", false),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA([a,b,c,d]");

  EXPECT_THROW_WITH_MESSAGE(RequestInfoHeaderFormatter("UPSTREAM_METADATA([\"a\",\"b\"]", false),
                            EnvoyException,
                            "Invalid header configuration. Expected format "
                            "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
                            "UPSTREAM_METADATA([\"a\",\"b\"]");

  // Non-string elements.
  EXPECT_THROW_WITH_MESSAGE(
      RequestInfoHeaderFormatter("UPSTREAM_METADATA([\"a\", 1])", false), EnvoyException,
      "Invalid header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA([\"a\", 1]), because JSON field from line 1 accessed with type 'String' "
      "does not match actual type 'Integer'.");

  // Invalid string elements.
  EXPECT_THROW_WITH_MESSAGE(
      RequestInfoHeaderFormatter("UPSTREAM_METADATA([\"a\", \"\\unothex\"])", false),
      EnvoyException,
      "Invalid header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA([\"a\", \"\\unothex\"]), because JSON supplied is not valid. "
      "Error(offset 7, line 1): Incorrect hex digit after \\u escape in string.\n");

  // Non-array parameters.
  EXPECT_THROW_WITH_MESSAGE(
      RequestInfoHeaderFormatter("UPSTREAM_METADATA({\"a\":1})", false), EnvoyException,
      "Invalid header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA({\"a\":1}), because JSON field from line 1 accessed with type 'Array' "
      "does not match actual type 'Object'.");
}

TEST(HeaderParserTest, TestParseInternal) {
  struct TestCase {
    std::string input_;
    absl::optional<std::string> expected_output_;
    absl::optional<std::string> expected_exception_;
  };

  static const TestCase test_cases[] = {
      // Valid inputs
      {"%PROTOCOL%", {"HTTP/1.1"}, {}},
      {"[%PROTOCOL%", {"[HTTP/1.1"}, {}},
      {"%PROTOCOL%]", {"HTTP/1.1]"}, {}},
      {"[%PROTOCOL%]", {"[HTTP/1.1]"}, {}},
      {"%%%PROTOCOL%", {"%HTTP/1.1"}, {}},
      {"%PROTOCOL%%%", {"HTTP/1.1%"}, {}},
      {"%%%PROTOCOL%%%", {"%HTTP/1.1%"}, {}},
      {"%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%", {"127.0.0.1"}, {}},
      {"%DOWNSTREAM_LOCAL_ADDRESS%", {"127.0.0.2:0"}, {}},
      {"%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%", {"127.0.0.2"}, {}},
      {"%UPSTREAM_METADATA([\"ns\", \"key\"])%", {"value"}, {}},
      {"[%UPSTREAM_METADATA([\"ns\", \"key\"])%", {"[value"}, {}},
      {"%UPSTREAM_METADATA([\"ns\", \"key\"])%]", {"value]"}, {}},
      {"[%UPSTREAM_METADATA([\"ns\", \"key\"])%]", {"[value]"}, {}},
      {"%UPSTREAM_METADATA([\"ns\", \t \"key\"])%", {"value"}, {}},
      {"%UPSTREAM_METADATA([\"ns\", \n \"key\"])%", {"value"}, {}},
      {"%UPSTREAM_METADATA( \t [ \t \"ns\" \t , \t \"key\" \t ] \t )%", {"value"}, {}},
      {"%PER_REQUEST_STATE(testing)%", {"test_value"}, {}},
      {"%START_TIME%", {"2018-04-03T23:06:09.123Z"}, {}},

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

      // TODO(dio): Un-terminated variable expressions with arguments and argument errors for
      // generic %VAR are not checked anymore. Find a way to get the same granularity as before for
      // following cases.
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

      {"%PER_REQUEST_STATE no parens%",
       {},
       {"Invalid header configuration. Expected format PER_REQUEST_STATE(<data_name>), "
        "actual format PER_REQUEST_STATE no parens"}},

      // Invalid arguments
      {"%UPSTREAM_METADATA%",
       {},
       {"Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", \"k\", "
        "...]), actual format UPSTREAM_METADATA"}},
      {"%UPSTREAM_METADATA([\"ns\"])%",
       {},
       {"Invalid header configuration. Expected format UPSTREAM_METADATA([\"namespace\", \"k\", "
        "...]), actual format UPSTREAM_METADATA([\"ns\"])"}},
  };

  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  absl::optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(request_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  ON_CALL(request_info, upstreamHost()).WillByDefault(Return(host));

  // Upstream metadata with percent signs in the key.
  auto metadata = std::make_shared<envoy::api::v2::core::Metadata>(
      TestUtility::parseYaml<envoy::api::v2::core::Metadata>(
          R"EOF(
        filter_metadata:
          ns:
            key: value
      )EOF"));
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));

  // "2018-04-03T23:06:09.123Z".
  const SystemTime start_time(std::chrono::milliseconds(1522796769123));
  ON_CALL(request_info, startTime()).WillByDefault(Return(start_time));

  Envoy::RequestInfo::FilterStateImpl per_request_state;
  per_request_state.setData("testing", std::make_unique<StringAccessorImpl>("test_value"));
  ON_CALL(request_info, perRequestState()).WillByDefault(ReturnRef(per_request_state));
  ON_CALL(Const(request_info), perRequestState()).WillByDefault(ReturnRef(per_request_state));

  for (const auto& test_case : test_cases) {
    Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValueOption> to_add;
    envoy::api::v2::core::HeaderValueOption* header = to_add.Add();
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
    req_header_parser->evaluateHeaders(header_map, request_info);

    std::string descriptor = fmt::format("for test case input: {}", test_case.input_);

    EXPECT_TRUE(header_map.has("x-header")) << descriptor;
    EXPECT_TRUE(test_case.expected_output_) << descriptor;
    EXPECT_EQ(test_case.expected_output_.value(), header_map.get_("x-header")) << descriptor;
  }
}

TEST(HeaderParserTest, EvaluateHeaders) {
  const std::string json = R"EOF(
  {
    "prefix": "/new_endpoint",
    "prefix_rewrite": "/api/new_endpoint",
    "cluster": "www2",
    "request_headers_to_add": [
      {
        "key": "x-client-ip",
        "value": "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
      }
    ]
  }
  )EOF";
  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromJson(json).route().request_headers_to_add());
  Http::TestHeaderMapImpl header_map{{":method", "POST"}};
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  req_header_parser->evaluateHeaders(header_map, request_info);
  EXPECT_TRUE(header_map.has("x-client-ip"));
}

TEST(HeaderParserTest, EvaluateEmptyHeaders) {
  const std::string json = R"EOF(
  {
    "prefix": "/new_endpoint",
    "prefix_rewrite": "/api/new_endpoint",
    "cluster": "www2",
    "request_headers_to_add": [
      {
        "key": "x-key",
        "value": "%UPSTREAM_METADATA([\"namespace\", \"key\"])%"
      }
    ]
  }
  )EOF";
  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromJson(json).route().request_headers_to_add());
  Http::TestHeaderMapImpl header_map{{":method", "POST"}};
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  auto metadata = std::make_shared<envoy::api::v2::core::Metadata>();
  ON_CALL(request_info, upstreamHost()).WillByDefault(Return(host));
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));
  req_header_parser->evaluateHeaders(header_map, request_info);
  EXPECT_FALSE(header_map.has("x-key"));
}

TEST(HeaderParserTest, EvaluateStaticHeaders) {
  const std::string json = R"EOF(
  {
    "prefix": "/new_endpoint",
    "prefix_rewrite": "/api/new_endpoint",
    "cluster": "www2",
    "request_headers_to_add": [
      {
        "key": "static-header",
        "value": "static-value"
      }
    ]
  }
  )EOF";
  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromJson(json).route().request_headers_to_add());
  Http::TestHeaderMapImpl header_map{{":method", "POST"}};
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  req_header_parser->evaluateHeaders(header_map, request_info);
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
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  absl::optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(request_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  ON_CALL(request_info, upstreamHost()).WillByDefault(Return(host));

  // Metadata with percent signs in the key.
  auto metadata = std::make_shared<envoy::api::v2::core::Metadata>(
      TestUtility::parseYaml<envoy::api::v2::core::Metadata>(
          R"EOF(
        filter_metadata:
          namespace:
            "%key%": value
      )EOF"));
  ON_CALL(*host, metadata()).WillByDefault(Return(metadata));

  Envoy::RequestInfo::FilterStateImpl per_request_state;
  per_request_state.setData("testing", std::make_unique<StringAccessorImpl>("test_value"));
  ON_CALL(request_info, perRequestState()).WillByDefault(ReturnRef(per_request_state));
  ON_CALL(Const(request_info), perRequestState()).WillByDefault(ReturnRef(per_request_state));

  req_header_parser->evaluateHeaders(header_map, request_info);

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
  const std::string json = R"EOF(
  {
    "prefix": "/new_endpoint",
    "prefix_rewrite": "/api/new_endpoint",
    "cluster": "www2",
    "request_headers_to_add": [
      {
        "key": "static-header",
        "value": "static-value"
      },
      {
        "key": "x-client-ip",
        "value": "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
      },
      {
        "key": "x-request-start",
        "value": "%START_TIME(%s%3f)%"
      },
      {
        "key": "x-request-start-default",
        "value": "%START_TIME%"
      },
      {
        "key": "x-request-start-range",
        "value": "%START_TIME(%f, %1f, %2f, %3f, %4f, %5f, %6f, %7f, %8f, %9f)%"
      }
    ]
  }
  )EOF";

  // Disable append mode.
  envoy::api::v2::route::RouteAction route_action = parseRouteFromJson(json).route();
  route_action.mutable_request_headers_to_add(0)->mutable_append()->set_value(false);
  route_action.mutable_request_headers_to_add(1)->mutable_append()->set_value(false);
  route_action.mutable_request_headers_to_add(2)->mutable_append()->set_value(false);

  HeaderParserPtr req_header_parser =
      Router::HeaderParser::configure(route_action.request_headers_to_add());
  Http::TestHeaderMapImpl header_map{
      {":method", "POST"}, {"static-header", "old-value"}, {"x-client-ip", "0.0.0.0"}};

  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  const SystemTime start_time(std::chrono::microseconds(1522796769123456));
  EXPECT_CALL(request_info, startTime()).Times(3).WillRepeatedly(Return(start_time));

  req_header_parser->evaluateHeaders(header_map, request_info);
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

  typedef std::map<std::string, int> CountMap;
  CountMap counts;
  header_map.iterate(
      [](const Http::HeaderEntry& header, void* cb_v) -> Http::HeaderMap::Iterate {
        CountMap* m = static_cast<CountMap*>(cb_v);
        std::string key = std::string{header.key().c_str()};
        CountMap::iterator i = m->find(key);
        if (i == m->end()) {
          m->insert({key, 1});
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
  cluster: www2
  response_headers_to_add:
    - header:
        key: "x-client-ip"
        value: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
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
  response_headers_to_remove: ["x-nope"]
)EOF";

  const auto route = parseRouteFromV2Yaml(yaml).route();
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove());
  Http::TestHeaderMapImpl header_map{{":method", "POST"}, {"x-safe", "safe"}, {"x-nope", "nope"}};
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;

  // Initialize start_time as 2018-04-03T23:06:09.123Z in microseconds.
  const SystemTime start_time(std::chrono::microseconds(1522796769123456));
  EXPECT_CALL(request_info, startTime()).Times(7).WillRepeatedly(Return(start_time));

  resp_header_parser->evaluateHeaders(header_map, request_info);
  EXPECT_TRUE(header_map.has("x-client-ip"));
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
}

} // namespace Router
} // namespace Envoy

#include <string>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/http/protocol.h"

#include "common/config/metadata.h"
#include "common/config/rds_json.h"
#include "common/router/header_formatter.h"
#include "common/router/header_parser.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
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

TEST_F(RequestInfoHeaderFormatterTest, TestFormatWithClientIpVariable) {
  testFormatting("CLIENT_IP", "127.0.0.1");
}

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
  Optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(request_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  testFormatting(request_info, "PROTOCOL", "HTTP/1.1");
}

TEST_F(RequestInfoHeaderFormatterTest, TestFormatWithUpstreamMetadataVariable) {
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());

  envoy::api::v2::core::Metadata metadata = TestUtility::parseYaml<envoy::api::v2::core::Metadata>(
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
      )EOF");

  // Prove we're testing the expected types.
  const auto& nested_struct =
      Envoy::Config::Metadata::metadataValue(metadata, "namespace", "nested").struct_value();
  EXPECT_EQ(nested_struct.fields().at("str_key").kind_case(), ProtobufWkt::Value::kStringValue);
  EXPECT_EQ(nested_struct.fields().at("bool_key1").kind_case(), ProtobufWkt::Value::kBoolValue);
  EXPECT_EQ(nested_struct.fields().at("bool_key2").kind_case(), ProtobufWkt::Value::kBoolValue);
  EXPECT_EQ(nested_struct.fields().at("num_key1").kind_case(), ProtobufWkt::Value::kNumberValue);
  EXPECT_EQ(nested_struct.fields().at("num_key1").kind_case(), ProtobufWkt::Value::kNumberValue);
  EXPECT_EQ(nested_struct.fields().at("null_key").kind_case(), ProtobufWkt::Value::kNullValue);
  EXPECT_EQ(nested_struct.fields().at("list_key").kind_case(), ProtobufWkt::Value::kListValue);
  EXPECT_EQ(nested_struct.fields().at("struct_key").kind_case(), ProtobufWkt::Value::kStructValue);

  ON_CALL(request_info, upstreamHost()).WillByDefault(Return(host));
  ON_CALL(*host, metadata()).WillByDefault(ReturnRef(metadata));

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

TEST_F(RequestInfoHeaderFormatterTest, TestFormatWithUpstreamMetadataVariableMissingHost) {
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host;
  ON_CALL(request_info, upstreamHost()).WillByDefault(Return(host));

  testFormatting(request_info, "UPSTREAM_METADATA([\"namespace\", \"key\"])", "");
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
    Optional<std::string> expected_output_;
    Optional<std::string> expected_exception_;
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

      // Un-terminated variable expressions with arguments.
      {"%VAR(no array)%",
       {},
       {"Invalid header configuration. Expecting JSON array of arguments after "
        "'VAR(', but found 'n'"}},
      {"%VAR( no array)%",
       {},
       {"Invalid header configuration. Expecting JSON array of arguments after "
        "'VAR( ', but found 'n'"}},
      {"%VAR([)",
       {},
       {"Invalid header configuration. Expecting '\"' or whitespace after 'VAR([', but found ')'"}},
      {"%VAR([ )",
       {},
       {"Invalid header configuration. Expecting '\"' or whitespace after 'VAR([ ', but found "
        "')'"}},
      {"%VAR([\"x\")%",
       {},
       {"Invalid header configuration. Expecting ',', ']', or whitespace after "
        "'VAR([\"x\"', but found ')'"}},
      {"%VAR([\"x\" )%",
       {},
       {"Invalid header configuration. Expecting ',', ']', or whitespace after "
        "'VAR([\"x\" ', but found ')'"}},
      {"%VAR([\"x\\",
       {},
       {"Invalid header configuration. Un-terminated backslash in JSON string after 'VAR([\"x'"}},
      {"%VAR([\"x\"]!",
       {},
       {"Invalid header configuration. Expecting ')' or whitespace after "
        "'VAR([\"x\"]', but found '!'"}},
      {"%VAR([\"x\"] !",
       {},
       {"Invalid header configuration. Expecting ')' or whitespace after "
        "'VAR([\"x\"] ', but found '!'"}},
      {"%VAR([\"x\"])!",
       {},
       {"Invalid header configuration. Expecting '%' or whitespace after "
        "'VAR([\"x\"])', but found '!'"}},
      {"%VAR([\"x\"]) !",
       {},
       {"Invalid header configuration. Expecting '%' or whitespace after "
        "'VAR([\"x\"]) ', but found '!'"}},

      // Argument errors
      {"%VAR()%",
       {},
       {"Invalid header configuration. Expecting JSON array of arguments after 'VAR(', but found "
        "')'"}},
      {"%VAR( )%",
       {},
       {"Invalid header configuration. Expecting JSON array of arguments after 'VAR( ', but found "
        "')'"}},
      {"%VAR([])%",
       {},
       {"Invalid header configuration. Expecting '\"' or whitespace after 'VAR([', but found ']'"}},
      {"%VAR( [ ] )%",
       {},
       {"Invalid header configuration. Expecting '\"' or whitespace after 'VAR( [ ', but found "
        "']'"}},
      {"%VAR([\"ns\",])%",
       {},
       {"Invalid header configuration. Expecting '\"' or whitespace after 'VAR([\"ns\",', but "
        "found ']'"}},
      {"%VAR( [ \"ns\" , ] )%",
       {},
       {"Invalid header configuration. Expecting '\"' or whitespace after 'VAR( [ \"ns\" , ', but "
        "found ']'"}},
      {"%VAR({\"ns\": \"key\"})%",
       {},
       {"Invalid header configuration. Expecting JSON array of arguments after 'VAR(', but found "
        "'{'"}},
      {"%VAR(\"ns\", \"key\")%",
       {},
       {"Invalid header configuration. Expecting JSON array of arguments after 'VAR(', but found "
        "'\"'"}},

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
  Optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(request_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  ON_CALL(request_info, upstreamHost()).WillByDefault(Return(host));

  // Metadata with percent signs in the key.
  envoy::api::v2::core::Metadata metadata = TestUtility::parseYaml<envoy::api::v2::core::Metadata>(
      R"EOF(
        filter_metadata:
          ns:
            key: value
      )EOF");
  ON_CALL(*host, metadata()).WillByDefault(ReturnRef(metadata));

  for (const auto& test_case : test_cases) {
    Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValueOption> to_add;
    envoy::api::v2::core::HeaderValueOption* header = to_add.Add();
    header->mutable_header()->set_key("x-header");
    header->mutable_header()->set_value(test_case.input_);

    if (test_case.expected_exception_.valid()) {
      EXPECT_FALSE(test_case.expected_output_.valid());
      EXPECT_THROW_WITH_MESSAGE(HeaderParser::configure(to_add), EnvoyException,
                                test_case.expected_exception_.value());
      continue;
    }

    HeaderParserPtr req_header_parser = HeaderParser::configure(to_add);

    Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
    req_header_parser->evaluateHeaders(headerMap, request_info);

    std::string descriptor = fmt::format("for test case input: {}", test_case.input_);

    EXPECT_TRUE(headerMap.has("x-header")) << descriptor;
    EXPECT_TRUE(test_case.expected_output_.valid()) << descriptor;
    EXPECT_EQ(test_case.expected_output_.value(), headerMap.get_("x-header")) << descriptor;
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
        "value": "%CLIENT_IP%"
      }
    ]
  }
  )EOF";
  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromJson(json).route().request_headers_to_add());
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  req_header_parser->evaluateHeaders(headerMap, request_info);
  EXPECT_TRUE(headerMap.has("x-client-ip"));
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
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  envoy::api::v2::core::Metadata metadata;
  ON_CALL(request_info, upstreamHost()).WillByDefault(Return(host));
  ON_CALL(*host, metadata()).WillByDefault(ReturnRef(metadata));
  req_header_parser->evaluateHeaders(headerMap, request_info);
  EXPECT_FALSE(headerMap.has("x-key"));
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
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  req_header_parser->evaluateHeaders(headerMap, request_info);
  EXPECT_TRUE(headerMap.has("static-header"));
  EXPECT_EQ("static-value", headerMap.get_("static-header"));
}

TEST(HeaderParserTest, EvaluateCompoundHeaders) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
  request_headers_to_add:
    - header:
        key: "x-prefix"
        value: "prefix-%CLIENT_IP%"
    - header:
        key: "x-suffix"
        value: "%CLIENT_IP%-suffix"
    - header:
        key: "x-both"
        value: "prefix-%CLIENT_IP%-suffix"
    - header:
        key: "x-escaping-1"
        value: "%%%CLIENT_IP%%%"
    - header:
        key: "x-escaping-2"
        value: "%%%%%%"
    - header:
        key: "x-multi"
        value: "%PROTOCOL% from %CLIENT_IP%"
    - header:
        key: "x-multi-back-to-back"
        value: "%PROTOCOL%%CLIENT_IP%"
    - header:
        key: "x-metadata"
        value: "%UPSTREAM_METADATA([\"namespace\", \"%key%\"])%"
  )EOF";

  HeaderParserPtr req_header_parser =
      HeaderParser::configure(parseRouteFromV2Yaml(yaml).route().request_headers_to_add());
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  Optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(request_info, protocol()).WillByDefault(ReturnPointee(&protocol));

  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  ON_CALL(request_info, upstreamHost()).WillByDefault(Return(host));

  // Metadata with percent signs in the key.
  envoy::api::v2::core::Metadata metadata = TestUtility::parseYaml<envoy::api::v2::core::Metadata>(
      R"EOF(
        filter_metadata:
          namespace:
            "%key%": value
      )EOF");
  ON_CALL(*host, metadata()).WillByDefault(ReturnRef(metadata));

  req_header_parser->evaluateHeaders(headerMap, request_info);

  EXPECT_TRUE(headerMap.has("x-prefix"));
  EXPECT_EQ("prefix-127.0.0.1", headerMap.get_("x-prefix"));

  EXPECT_TRUE(headerMap.has("x-suffix"));
  EXPECT_EQ("127.0.0.1-suffix", headerMap.get_("x-suffix"));

  EXPECT_TRUE(headerMap.has("x-both"));
  EXPECT_EQ("prefix-127.0.0.1-suffix", headerMap.get_("x-both"));

  EXPECT_TRUE(headerMap.has("x-escaping-1"));
  EXPECT_EQ("%127.0.0.1%", headerMap.get_("x-escaping-1"));

  EXPECT_TRUE(headerMap.has("x-escaping-2"));
  EXPECT_EQ("%%%", headerMap.get_("x-escaping-2"));

  EXPECT_TRUE(headerMap.has("x-multi"));
  EXPECT_EQ("HTTP/1.1 from 127.0.0.1", headerMap.get_("x-multi"));

  EXPECT_TRUE(headerMap.has("x-multi-back-to-back"));
  EXPECT_EQ("HTTP/1.1127.0.0.1", headerMap.get_("x-multi-back-to-back"));

  EXPECT_TRUE(headerMap.has("x-metadata"));
  EXPECT_EQ("value", headerMap.get_("x-metadata"));
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
        "value": "%CLIENT_IP%"
      }
    ]
  }
  )EOF";

  // Disable append mode.
  envoy::api::v2::route::RouteAction route_action = parseRouteFromJson(json).route();
  route_action.mutable_request_headers_to_add(0)->mutable_append()->set_value(false);
  route_action.mutable_request_headers_to_add(1)->mutable_append()->set_value(false);

  HeaderParserPtr req_header_parser =
      Router::HeaderParser::configure(route_action.request_headers_to_add());
  Http::TestHeaderMapImpl headerMap{
      {":method", "POST"}, {"static-header", "old-value"}, {"x-client-ip", "0.0.0.0"}};

  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  req_header_parser->evaluateHeaders(headerMap, request_info);
  EXPECT_TRUE(headerMap.has("static-header"));
  EXPECT_EQ("static-value", headerMap.get_("static-header"));
  EXPECT_TRUE(headerMap.has("x-client-ip"));
  EXPECT_EQ("127.0.0.1", headerMap.get_("x-client-ip"));

  typedef std::map<std::string, int> CountMap;
  CountMap counts;
  headerMap.iterate(
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
}

TEST(HeaderParserTest, EvaluateResponseHeaders) {
  const std::string yaml = R"EOF(
match: { prefix: "/new_endpoint" }
route:
  cluster: www2
  response_headers_to_add:
    - header:
        key: "x-client-ip"
        value: "%CLIENT_IP%"
      append: true
  response_headers_to_remove: ["x-nope"]
)EOF";

  const auto route = parseRouteFromV2Yaml(yaml).route();
  HeaderParserPtr resp_header_parser =
      HeaderParser::configure(route.response_headers_to_add(), route.response_headers_to_remove());
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}, {"x-safe", "safe"}, {"x-nope", "nope"}};
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  resp_header_parser->evaluateHeaders(headerMap, request_info);
  EXPECT_TRUE(headerMap.has("x-client-ip"));
  EXPECT_TRUE(headerMap.has("x-safe"));
  EXPECT_FALSE(headerMap.has("x-nope"));
}

} // namespace Router
} // namespace Envoy

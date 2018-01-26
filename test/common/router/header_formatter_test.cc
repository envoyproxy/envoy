#include <string>

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

TEST(RequestInfoHeaderFormatterTest, TestFormatWithClientIpVariable) {
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  const std::string variable = "CLIENT_IP";
  RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
  const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
  EXPECT_EQ("127.0.0.1", formatted_string);
}

TEST(RequestInfoHeaderFormatterTest, TestFormatWithDownstreamRemoteAddressVariable) {
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  const std::string variable = "DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT";
  RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
  const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
  EXPECT_EQ("127.0.0.1", formatted_string);
}

TEST(RequestInfoHeaderFormatterTest, TestFormatWithProtocolVariable) {
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  Optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(request_info, protocol()).WillByDefault(ReturnRef(protocol));
  const std::string variable = "PROTOCOL";
  RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
  const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
  EXPECT_EQ("HTTP/1.1", formatted_string);
}

TEST(RequestInfoFormatterTest, TestFormatWithUpstreamMetadataVariable) {
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());

  envoy::api::v2::Metadata metadata = TestUtility::parseYaml<envoy::api::v2::Metadata>(
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
  {
    const std::string variable = "UPSTREAM_METADATA([\"namespace\", \"key\"])";
    RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
    const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
    EXPECT_EQ("value", formatted_string);
  }

  // Nested string value.
  {
    const std::string variable = "UPSTREAM_METADATA([\"namespace\", \"nested\", \"str_key\"])";
    RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
    const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
    EXPECT_EQ("str_value", formatted_string);
  }

  // Boolean values.
  {
    const std::string variable = "UPSTREAM_METADATA([\"namespace\", \"nested\", \"bool_key1\"])";
    RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
    const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
    EXPECT_EQ("true", formatted_string);
  }

  {
    const std::string variable = "UPSTREAM_METADATA([\"namespace\", \"nested\", \"bool_key2\"])";
    RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
    const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
    EXPECT_EQ("false", formatted_string);
  }

  // Number values.
  {
    const std::string variable = "UPSTREAM_METADATA([\"namespace\", \"nested\", \"num_key1\"])";
    RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
    const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
    EXPECT_EQ("1", formatted_string);
  }

  {
    const std::string variable = "UPSTREAM_METADATA([\"namespace\", \"nested\", \"num_key2\"])";
    RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
    const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
    EXPECT_EQ("3.14", formatted_string);
  }

  // Deeply nested value.
  {
    const std::string variable =
        "UPSTREAM_METADATA([\"namespace\", \"nested\", \"struct_key\", \"deep_key\"])";
    RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
    const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
    EXPECT_EQ("deep_value", formatted_string);
  }

  // Initial metadata lookup fails.
  {
    const std::string variable = "UPSTREAM_METADATA([\"wrong_namespace\", \"key\"])";
    RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
    const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
    EXPECT_EQ("", formatted_string);
  }

  {
    const std::string variable = "UPSTREAM_METADATA([\"namespace\", \"not_found\"])";
    RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
    const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
    EXPECT_EQ("", formatted_string);
  }

  {
    const std::string variable = "UPSTREAM_METADATA([\"namespace\", \"not_found\", \"key\"])";
    RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
    const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
    EXPECT_EQ("", formatted_string);
  }

  // Nested metadata lookup fails.
  {
    const std::string variable = "UPSTREAM_METADATA([\"namespace\", \"nested\", \"not_found\"])";
    RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
    const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
    EXPECT_EQ("", formatted_string);
  }

  // Nested metadata lookup returns non-struct intermediate value.
  {
    const std::string variable = "UPSTREAM_METADATA([\"namespace\", \"key\", \"invalid\"])";
    RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
    const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
    EXPECT_EQ("", formatted_string);
  }

  // Struct values are not rendered.
  {
    const std::string variable = "UPSTREAM_METADATA([\"namespace\", \"nested\", \"struct_key\"])";
    RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
    const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
    EXPECT_EQ("", formatted_string);
  }

  // List values are not rendered.
  {
    const std::string variable = "UPSTREAM_METADATA([\"namespace\", \"nested\", \"list_key\"])";
    RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
    const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
    EXPECT_EQ("", formatted_string);
  }
}

TEST(RequestInfoFormatterTest, TestFormatWithUpstreamMetadataVariableMissingHost) {
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host;
  ON_CALL(request_info, upstreamHost()).WillByDefault(Return(host));

  const std::string variable = "UPSTREAM_METADATA([\"namespace\", \"key\"])";
  RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
  const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
  EXPECT_EQ("", formatted_string);
}

TEST(RequestInfoHeaderFormatterTest, WrongVariableToFormat) {
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  const std::string variable = "INVALID_VARIABLE";
  EXPECT_THROW_WITH_MESSAGE(RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false),
                            EnvoyException,
                            "field 'INVALID_VARIABLE' not supported as custom header");
}

TEST(RequestInfoHeaderFormatterTest, WrongFormatOnVariable) {
  const std::string json = R"EOF(
  {
    "prefix": "/new_endpoint",
    "prefix_rewrite": "/api/new_endpoint",
    "cluster": "www2",
    "request_headers_to_add": [
       {
         "key": "x-client-ip",
         "value": "%CLIENT_IP"
       }
    ]
  }
  )EOF";
  EXPECT_THROW_WITH_MESSAGE(Envoy::Router::HeaderParser::configure(
                                parseRouteFromJson(json).route().request_headers_to_add()),
                            EnvoyException,
                            "Incorrect header configuration. Expected variable format "
                            "%<variable_name>%, actual format %CLIENT_IP");
}

TEST(RequestInfoHeaderFormatterTest, WrongFormatOnUpstreamMetadataVariable) {
  // Invalid JSON.
  EXPECT_THROW_WITH_MESSAGE(
      RequestInfoHeaderFormatter requestInfoHeaderFormatter("UPSTREAM_METADATA(abcd)", false),
      EnvoyException,
      "Incorrect header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA(abcd), because JSON supplied is not valid. Error(offset 0, line 1): "
      "Invalid value.\n");

  // No parameters.
  EXPECT_THROW_WITH_MESSAGE(
      RequestInfoHeaderFormatter requestInfoHeaderFormatter("UPSTREAM_METADATA", false),
      EnvoyException,
      "Incorrect header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA");

  EXPECT_THROW_WITH_MESSAGE(
      RequestInfoHeaderFormatter requestInfoHeaderFormatter("UPSTREAM_METADATA()", false),
      EnvoyException,
      "Incorrect header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA(), because JSON supplied is not valid. Error(offset 0, line 1): "
      "The document is empty.\n");

  // One parameter.
  EXPECT_THROW_WITH_MESSAGE(
      RequestInfoHeaderFormatter requestInfoHeaderFormatter("UPSTREAM_METADATA([\"ns\"])", false),
      EnvoyException,
      "Incorrect header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA([\"ns\"])");

  // Missing close paren.
  EXPECT_THROW_WITH_MESSAGE(
      RequestInfoHeaderFormatter requestInfoHeaderFormatter("UPSTREAM_METADATA(", false),
      EnvoyException,
      "Incorrect header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA(");

  EXPECT_THROW_WITH_MESSAGE(
      RequestInfoHeaderFormatter requestInfoHeaderFormatter("UPSTREAM_METADATA([a,b,c,d]", false),
      EnvoyException,
      "Incorrect header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA([a,b,c,d]");

  // Non-string elements.
  EXPECT_THROW_WITH_MESSAGE(
      RequestInfoHeaderFormatter requestInfoHeaderFormatter("UPSTREAM_METADATA([\"a\", 1])", false),
      EnvoyException,
      "Incorrect header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA([\"a\", 1]), because JSON field from line 1 accessed with type 'String' "
      "does not match actual type 'Integer'.");

  // Non-array parameters.
  EXPECT_THROW_WITH_MESSAGE(
      RequestInfoHeaderFormatter requestInfoHeaderFormatter("UPSTREAM_METADATA({\"a\":1})", false),
      EnvoyException,
      "Incorrect header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA({\"a\":1}), because JSON field from line 1 accessed with type 'Array' "
      "does not match actual type 'Object'.");
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
  HeaderParserPtr req_header_parser = Envoy::Router::HeaderParser::configure(
      parseRouteFromJson(json).route().request_headers_to_add());
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
  HeaderParserPtr req_header_parser = Envoy::Router::HeaderParser::configure(
      parseRouteFromJson(json).route().request_headers_to_add());
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  envoy::api::v2::Metadata metadata;
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
  HeaderParserPtr req_header_parser = Envoy::Router::HeaderParser::configure(
      parseRouteFromJson(json).route().request_headers_to_add());
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  req_header_parser->evaluateHeaders(headerMap, request_info);
  EXPECT_TRUE(headerMap.has("static-header"));
  EXPECT_EQ("static-value", headerMap.get_("static-header"));
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
  HeaderParserPtr resp_header_parser = Envoy::Router::HeaderParser::configure(
      route.response_headers_to_add(), route.response_headers_to_remove());
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}, {"x-safe", "safe"}, {"x-nope", "nope"}};
  NiceMock<Envoy::RequestInfo::MockRequestInfo> request_info;
  resp_header_parser->evaluateHeaders(headerMap, request_info);
  EXPECT_TRUE(headerMap.has("x-client-ip"));
  EXPECT_TRUE(headerMap.has("x-safe"));
  EXPECT_FALSE(headerMap.has("x-nope"));
}

} // namespace Router
} // namespace Envoy

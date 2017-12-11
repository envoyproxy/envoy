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
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Router {

static envoy::api::v2::Route parseRouteFromJson(const std::string& json_string) {
  envoy::api::v2::Route route;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Envoy::Config::RdsJson::translateRoute(*json_object_ptr, route);
  return route;
}

static envoy::api::v2::Route parseRouteFromV2Yaml(const std::string& yaml) {
  envoy::api::v2::Route route;
  MessageUtil::loadFromYaml(yaml, route);
  return route;
}

TEST(RequestInfoHeaderFormatterTest, TestFormatWithClientIpVariable) {
  NiceMock<Envoy::AccessLog::MockRequestInfo> request_info;
  const std::string downstream_addr = "127.0.0.1";
  ON_CALL(request_info, getDownstreamAddress()).WillByDefault(ReturnRef(downstream_addr));
  const std::string variable = "CLIENT_IP";
  RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
  const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
  EXPECT_EQ(downstream_addr, formatted_string);
}

TEST(RequestInfoHeaderFormatterTest, TestFormatWithProtocolVariable) {
  NiceMock<Envoy::AccessLog::MockRequestInfo> request_info;
  Optional<Envoy::Http::Protocol> protocol = Envoy::Http::Protocol::Http11;
  ON_CALL(request_info, protocol()).WillByDefault(ReturnRef(protocol));
  const std::string variable = "PROTOCOL";
  RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
  const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
  EXPECT_EQ("HTTP/1.1", formatted_string);
}

TEST(RequestInfoFormatterTest, TestFormatWithUpstreamMetadataVariable) {
  NiceMock<Envoy::AccessLog::MockRequestInfo> request_info;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host(
      new NiceMock<Envoy::Upstream::MockHostDescription>());

  envoy::api::v2::Metadata metadata;
  Envoy::Config::Metadata::mutableMetadataValue(metadata, "namespace", "key")
      .set_string_value("value");
  auto& struct_fields =
      *(Envoy::Config::Metadata::mutableMetadataValue(metadata, "namespace", "nested")
            .mutable_struct_value()
            ->mutable_fields());
  struct_fields["str_key"].set_string_value("str_value");
  struct_fields["bool_key1"].set_bool_value(true);
  struct_fields["bool_key2"].set_bool_value(false);
  struct_fields["num_key1"].set_number_value(1);
  struct_fields["num_key2"].set_number_value(3.14);
  struct_fields["null_key"].set_null_value(ProtobufWkt::NullValue::NULL_VALUE);
  struct_fields["list_key"].mutable_list_value()->add_values()->set_string_value("list_element");
  struct_fields["escaped,key"].set_string_value("escaped_key_value");
  auto& substruct_fields = *(struct_fields["struct_key"].mutable_struct_value()->mutable_fields());
  substruct_fields["deep_key"].set_string_value("deep_value");

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
  NiceMock<Envoy::AccessLog::MockRequestInfo> request_info;
  std::shared_ptr<NiceMock<Envoy::Upstream::MockHostDescription>> host;
  ON_CALL(request_info, upstreamHost()).WillByDefault(Return(host));

  const std::string variable = "UPSTREAM_METADATA([\"namespace\", \"key\"])";
  RequestInfoHeaderFormatter requestInfoHeaderFormatter(variable, false);
  const std::string formatted_string = requestInfoHeaderFormatter.format(request_info);
  EXPECT_EQ("", formatted_string);
}

TEST(RequestInfoHeaderFormatterTest, WrongVariableToFormat) {
  NiceMock<Envoy::AccessLog::MockRequestInfo> request_info;
  const std::string downstream_addr = "127.0.0.1";
  ON_CALL(request_info, getDownstreamAddress()).WillByDefault(ReturnRef(downstream_addr));
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
  EXPECT_THROW(RequestInfoHeaderFormatter requestInfoHeaderFormatter("UPSTREAM_METADATA", false),
               EnvoyException);

  EXPECT_THROW(RequestInfoHeaderFormatter requestInfoHeaderFormatter("UPSTREAM_METADATA()", false),
               EnvoyException);

  // One parameter.
  EXPECT_THROW_WITH_MESSAGE(
      RequestInfoHeaderFormatter requestInfoHeaderFormatter("UPSTREAM_METADATA([\"ns\"])", false),
      EnvoyException,
      "Incorrect header configuration. Expected format "
      "UPSTREAM_METADATA([\"namespace\", \"k\", ...]), actual format "
      "UPSTREAM_METADATA([\"ns\"])");

  // Missing close paren.
  EXPECT_THROW(RequestInfoHeaderFormatter requestInfoHeaderFormatter("UPSTREAM_METADATA(", false),
               EnvoyException);

  EXPECT_THROW(
      RequestInfoHeaderFormatter requestInfoHeaderFormatter("UPSTREAM_METADATA(a,b,c,d", false),
      EnvoyException);
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
  NiceMock<Envoy::AccessLog::MockRequestInfo> request_info;
  const std::string downstream_addr = "127.0.0.1";
  ON_CALL(request_info, getDownstreamAddress()).WillByDefault(ReturnRef(downstream_addr));
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
  NiceMock<Envoy::AccessLog::MockRequestInfo> request_info;
  envoy::api::v2::Metadata metadata;
  ON_CALL(request_info, upstreamHost()).WillByDefault(Return(host));
  ON_CALL(*host, metadata()).WillByDefault(ReturnRef(metadata));
  req_header_parser->evaluateHeaders(headerMap, request_info);
  EXPECT_FALSE(headerMap.has("x-client-ip"));
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
  NiceMock<Envoy::AccessLog::MockRequestInfo> request_info;
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
  envoy::api::v2::RouteAction route_action = parseRouteFromJson(json).route();
  route_action.mutable_request_headers_to_add(0)->mutable_append()->set_value(false);
  route_action.mutable_request_headers_to_add(1)->mutable_append()->set_value(false);

  HeaderParserPtr req_header_parser =
      Router::HeaderParser::configure(route_action.request_headers_to_add());
  Http::TestHeaderMapImpl headerMap{
      {":method", "POST"}, {"static-header", "old-value"}, {"x-client-ip", "0.0.0.0"}};

  NiceMock<Envoy::AccessLog::MockRequestInfo> request_info;
  const std::string downstream_addr = "127.0.0.1";
  ON_CALL(request_info, getDownstreamAddress()).WillByDefault(ReturnRef(downstream_addr));

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
  NiceMock<Envoy::AccessLog::MockRequestInfo> request_info;
  const std::string downstream_addr = "127.0.0.1";
  ON_CALL(request_info, getDownstreamAddress()).WillByDefault(ReturnRef(downstream_addr));
  resp_header_parser->evaluateHeaders(headerMap, request_info);
  EXPECT_TRUE(headerMap.has("x-client-ip"));
  EXPECT_TRUE(headerMap.has("x-safe"));
  EXPECT_FALSE(headerMap.has("x-nope"));
}

} // namespace Router
} // namespace Envoy

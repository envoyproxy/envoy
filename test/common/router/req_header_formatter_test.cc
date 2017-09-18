#include <string>

#include "envoy/http/protocol.h"

#include "common/config/rds_json.h"
#include "common/router/req_header_formatter.h"

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

TEST(RequestHeaderFormatterTest, TestFormatWithClientIpVariable) {
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  const std::string downstream_addr = "127.0.0.1";
  ON_CALL(request_info, getDownstreamAddress()).WillByDefault(ReturnRef(downstream_addr));
  const std::string variable = "CLIENT_IP";
  RequestHeaderFormatter requestHeaderFormatter(variable);
  const std::string formatted_string = requestHeaderFormatter.format(request_info);
  EXPECT_EQ(downstream_addr, formatted_string);
}

TEST(RequestHeaderFormatterTest, TestFormatWithProtocolVariable) {
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  ON_CALL(request_info, protocol()).WillByDefault(Return(Envoy::Http::Protocol::Http11));
  const std::string variable = "PROTOCOL";
  RequestHeaderFormatter requestHeaderFormatter(variable);
  const std::string formatted_string = requestHeaderFormatter.format(request_info);
  EXPECT_EQ("HTTP/1.1", formatted_string);
}

TEST(RequestHeaderFormatterTest, WrongVariableToFormat) {
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  const std::string downstream_addr = "127.0.0.1";
  ON_CALL(request_info, getDownstreamAddress()).WillByDefault(ReturnRef(downstream_addr));
  const std::string variable = "INVALID_VARIABLE";
  EXPECT_THROW_WITH_MESSAGE(RequestHeaderFormatter requestHeaderFormatter(variable), EnvoyException,
                            "field 'INVALID_VARIABLE' not supported as custom request header");
}

TEST(RequestHeaderFormatterTest, WrongFormatOnVariable) {
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
  EXPECT_THROW_WITH_MESSAGE(Envoy::Router::RequestHeaderParser::parse(
                                parseRouteFromJson(json).route().request_headers_to_add()),
                            EnvoyException,
                            "Incorrect header configuration. Expected variable format "
                            "%<variable_name>%, actual format %CLIENT_IP");
}

TEST(RequestHeaderParserTest, EvaluateHeaders) {
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
  RequestHeaderParserPtr req_header_parser = Envoy::Router::RequestHeaderParser::parse(
      parseRouteFromJson(json).route().request_headers_to_add());
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  const std::string downstream_addr = "127.0.0.1";
  ON_CALL(request_info, getDownstreamAddress()).WillByDefault(ReturnRef(downstream_addr));
  req_header_parser->evaluateRequestHeaders(headerMap, request_info);
  EXPECT_TRUE(headerMap.has("x-client-ip"));
}

TEST(RequestHeaderParserTest, EvaluateStaticHeaders) {
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
  RequestHeaderParserPtr req_header_parser = Envoy::Router::RequestHeaderParser::parse(
      parseRouteFromJson(json).route().request_headers_to_add());
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  req_header_parser->evaluateRequestHeaders(headerMap, request_info);
  EXPECT_TRUE(headerMap.has("static-header"));
  EXPECT_EQ("static-value", headerMap.get_("static-header"));
}

} // namespace Router
} // namespace Envoy

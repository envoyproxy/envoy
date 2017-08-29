#include <string>

#include "envoy/http/protocol.h"

#include "common/config/rds_json.h"
#include "common/http/access_log/access_log_formatter.h"
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

static bool CheckFormatMessage(const std::string error_message,
                               const std::string expected_message) {
  const std::size_t found = error_message.find(expected_message);
  return (found != std::string::npos);
}

TEST(RequestHeaderFormatterTest, TestFormatWithClientIpVariable) {
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  std::string s1 = "127.0.0.1";
  ON_CALL(request_info, getDownstreamAddress()).WillByDefault(ReturnRef(s1));
  std::string variable = "CLIENT_IP";
  RequestHeaderFormatter requestHeaderFormatter(variable);
  std::string formatted_string = requestHeaderFormatter.format(request_info);
  EXPECT_EQ("127.0.0.1", formatted_string);
}

TEST(RequestHeaderFormatterTest, TestFormatWithProtocolVariable) {
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  ON_CALL(request_info, protocol()).WillByDefault(Return(Envoy::Http::Protocol::Http11));
  std::string variable = "PROTOCOL";
  RequestHeaderFormatter requestHeaderFormatter(variable);
  std::string formatted_string = requestHeaderFormatter.format(request_info);
  EXPECT_EQ("HTTP/1.1", formatted_string);
}

TEST(RequestHeaderFormatterTest, WrongVariableToFormat) {
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  std::string s1 = "127.0.0.1";
  ON_CALL(request_info, getDownstreamAddress()).WillByDefault(ReturnRef(s1));
  std::string variable = "INVALID_VARIABLE";
  ASSERT_THROW(RequestHeaderFormatter requestHeaderFormatter(variable), EnvoyException);
  try {
    RequestHeaderFormatter requestHeaderFormatter(variable);
  } catch (EnvoyException& ex) {
    EXPECT_PRED2(CheckFormatMessage, ex.what(), "not supported");
  }
}

TEST(RequestHeaderFormatterTest, WrongFormatOnVariable) {
  std::string json = R"EOF({
          "prefix": "/new_endpoint",
          "prefix_rewrite": "/api/new_endpoint",
          "cluster": "www2",
          "request_headers_to_add": [
            {
              "key": "x-client-ip",
              "value": "%CLIENT_IP"
            }
          ]
        })EOF";
  ASSERT_THROW(Envoy::Router::RequestHeaderParser::parse(
                   parseRouteFromJson(json).route().request_headers_to_add()),
               EnvoyException);
  try {
    Envoy::Router::RequestHeaderParser::parse(
        parseRouteFromJson(json).route().request_headers_to_add());
  } catch (EnvoyException& ex) {
    EXPECT_PRED2(CheckFormatMessage, ex.what(), "Incorrect header configuration");
  }
}

TEST(RequestHeaderParserTest, EvaluateHeaders) {
  std::string json = R"EOF({
          "prefix": "/new_endpoint",
          "prefix_rewrite": "/api/new_endpoint",
          "cluster": "www2",
          "request_headers_to_add": [
            {
              "key": "x-client-ip",
              "value": "%CLIENT_IP%"
            }
          ]
        })EOF";
  RequestHeaderParserPtr req_header_parser = Envoy::Router::RequestHeaderParser::parse(
      parseRouteFromJson(json).route().request_headers_to_add());
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  std::string s1 = "127.0.0.1";
  ON_CALL(request_info, getDownstreamAddress()).WillByDefault(ReturnRef(s1));
  req_header_parser->evaluateRequestHeaders(headerMap, request_info);
  EXPECT_TRUE(headerMap.has("x-client-ip"));
}

TEST(RequestHeaderParserTest, EvaluateStaticHeaders) {
  std::string json = R"EOF({
          "prefix": "/new_endpoint",
          "prefix_rewrite": "/api/new_endpoint",
          "cluster": "www2",
          "request_headers_to_add": [
            {
              "key": "static-header",
              "value": "static-value"
            }
          ]
        })EOF";
  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  RequestHeaderParserPtr req_header_parser = Envoy::Router::RequestHeaderParser::parse(
      parseRouteFromJson(json).route().request_headers_to_add());
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  req_header_parser->evaluateRequestHeaders(headerMap, request_info);
  EXPECT_TRUE(headerMap.has("static-header"));
}

} // namespace Router
} // namespace Envoy

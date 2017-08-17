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

namespace Envoy {
namespace Router {
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

static envoy::api::v2::Route parseRouteFromJson(const std::string& json_string) {
  envoy::api::v2::Route route;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Envoy::Config::RdsJson::translateRoute(*json_object_ptr, route);
  return route;
}

bool CheckFormatMessage(const std::string errorMessage, const std::string expectedMessage) {
  std::size_t found = errorMessage.find(expectedMessage);
  return (found != std::string::npos);
}

TEST(RequestHeaderFormatterTest, TestFormatWithClientIpVariable) {
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> requestInfo;
  std::string s1 = "127.0.0.1";
  ON_CALL(requestInfo, getDownstreamAddress()).WillByDefault(ReturnRef(s1));
  std::string variable = "CLIENT_IP";
  RequestHeaderFormatter requestHeaderFormatter(variable);
  std::string formatted_string = requestHeaderFormatter.format(requestInfo);
  EXPECT_EQ("127.0.0.1", formatted_string);
}

TEST(RequestHeaderFormatterTest, TestFormatWithProtocolVariable) {
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> requestInfo;
  ON_CALL(requestInfo, protocol()).WillByDefault(Return(Envoy::Http::Protocol::Http11));
  std::string variable = "PROTOCOL";
  RequestHeaderFormatter requestHeaderFormatter(variable);
  std::string formatted_string = requestHeaderFormatter.format(requestInfo);
  EXPECT_EQ("HTTP/1.1", formatted_string);
}

TEST(RequestHeaderFormatterTest, WrongVariableToFormat) {
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> requestInfo;
  std::string s1 = "127.0.0.1";
  ON_CALL(requestInfo, getDownstreamAddress()).WillByDefault(ReturnRef(s1));
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
  ASSERT_THROW(Envoy::Router::RequestHeaderParser::parseRoute(parseRouteFromJson(json)),
               EnvoyException);
  try {
    Envoy::Router::RequestHeaderParser::parseRoute(parseRouteFromJson(json));
  } catch (EnvoyException& ex) {
    EXPECT_PRED2(CheckFormatMessage, ex.what(), "Incorrect configuration");
  }
}

TEST(RequestHeaderParserTest, EvaluateHeaders) {
  std::string json = R"EOF(
		{
	     "request_headers_to_add": [
	    	{
	      		"key": "x-client-ip",
	      		"value": "%CLIENT_IP%"
	    	}
	   	]
	  })EOF";
  RequestHeaderParserPtr req_header_parser =
      Envoy::Router::RequestHeaderParser::parseRoute(parseRouteFromJson(json));
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> requestInfo;
  std::string s1 = "127.0.0.1";
  ON_CALL(requestInfo, getDownstreamAddress()).WillByDefault(ReturnRef(s1));
  std::pair<Http::LowerCaseString, std::string> client_ip_req_header(
      Http::LowerCaseString{"x-client-ip"}, "%CLIENT_IP%");
  std::list<std::pair<Http::LowerCaseString, std::string>> requestHeadersToAdd = {
      client_ip_req_header};
  req_header_parser->evaluateRequestHeaders(headerMap, requestInfo, requestHeadersToAdd);
  EXPECT_TRUE(headerMap.has("x-client-ip"));
}

TEST(RequestHeaderParserTest, EvaluateStaticHeaders) {
  std::string json = R"EOF(
		{
	     "request_headers_to_add": [
	    	{
	      		"key": "static-header",
	      		"value": "static-value"
	    	}
	   	]
	  })EOF";
  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  RequestHeaderParserPtr req_header_parser =
      Envoy::Router::RequestHeaderParser::parseRoute(parseRouteFromJson(json));
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> requestInfo;
  std::pair<Http::LowerCaseString, std::string> static_req_header(
      Http::LowerCaseString{"static-header"}, "static-value");
  std::list<std::pair<Http::LowerCaseString, std::string>> requestHeadersToAdd = {
      static_req_header};
  req_header_parser->evaluateRequestHeaders(headerMap, requestInfo, requestHeadersToAdd);
  EXPECT_TRUE(headerMap.has("static-header"));
}
} // namespace Router
} // namespace Envoy

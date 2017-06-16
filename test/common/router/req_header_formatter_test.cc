#include <string>

#include "envoy/http/protocol.h"

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
  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  RequestHeaderParserSharedPtr req_header_parser_shared_ptr =
      Envoy::Router::RequestHeaderParser::parse(*loader.get());
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> requestInfo;
  std::string s1 = "127.0.0.1";
  ON_CALL(requestInfo, getDownstreamAddress()).WillByDefault(ReturnRef(s1));
  std::pair<Http::LowerCaseString, std::string> client_ip_req_header(
      Http::LowerCaseString{"x-client-ip"}, "%CLIENT_IP%");
  std::list<std::pair<Http::LowerCaseString, std::string>> requestHeadersToAdd = {
      client_ip_req_header};
  req_header_parser_shared_ptr->evaluateRequestHeaders(headerMap, requestInfo, requestHeadersToAdd);
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
  RequestHeaderParserSharedPtr req_header_parser_shared_ptr =
      Envoy::Router::RequestHeaderParser::parse(*loader.get());
  Http::TestHeaderMapImpl headerMap{{":method", "POST"}};
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> requestInfo;
  std::pair<Http::LowerCaseString, std::string> static_req_header(
      Http::LowerCaseString{"static-header"}, "static-value");
  std::list<std::pair<Http::LowerCaseString, std::string>> requestHeadersToAdd = {
      static_req_header};
  req_header_parser_shared_ptr->evaluateRequestHeaders(headerMap, requestInfo, requestHeadersToAdd);
  EXPECT_TRUE(headerMap.has("static-header"));
}

} // namespace Router
} // namespace Envoy
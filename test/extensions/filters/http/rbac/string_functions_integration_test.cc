#include "envoy/extensions/filters/http/rbac/v3/rbac.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/protobuf/utility.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

// Test RBAC configuration that uses CEL string extension functions for policy matching.
// Uses proper CEL expression structure with replace() function.
const std::string RBAC_CONFIG_WITH_STRING_FUNCTIONS_REPLACE = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    policies:
      "condition_with_replace":
        permissions:
          - any: true
        principals:
          - any: true
        condition:
          call_expr:
            function: _==_
            args:
            - call_expr:
                target:
                  call_expr:
                    function: _[_]
                    args:
                    - select_expr:
                        operand:
                          ident_expr:
                            name: request
                        field: headers
                    - const_expr:
                        string_value: ":method"
                function: replace
                args:
                - const_expr:
                    string_value: GET
                - const_expr:
                    string_value: ALLOWED
            - const_expr:
                string_value: ALLOWED
)EOF";

// Test RBAC configuration that uses string split function.
const std::string RBAC_CONFIG_WITH_STRING_FUNCTIONS_SPLIT = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    policies:
      "condition_with_split":
        permissions:
          - any: true
        principals:
          - any: true
        condition:
          call_expr:
            function: _>=_
            args:
            - call_expr:
                function: size
                args:
                - call_expr:
                    target:
                      call_expr:
                        function: _[_]
                        args:
                        - select_expr:
                            operand:
                              ident_expr:
                                name: request
                            field: headers
                        - const_expr:
                            string_value: "x-test-values"
                    function: split
                    args:
                    - const_expr:
                        string_value: ","
            - const_expr:
                int64_value: 2
)EOF";

// Additional RBAC configuration for testing more string functions and edge cases.
const std::string RBAC_CONFIG_WITH_STRING_FUNCTIONS_EMPTY_TEST = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    policies:
      "condition_with_empty_replace":
        permissions:
          - any: true
        principals:
          - any: true
        condition:
          call_expr:
            function: _==_
            args:
            - call_expr:
                target:
                  call_expr:
                    function: _[_]
                    args:
                    - select_expr:
                        operand:
                          ident_expr:
                            name: request
                        field: headers
                    - const_expr:
                        string_value: "x-empty-test"
                function: replace
                args:
                - const_expr:
                    string_value: ""
                - const_expr:
                    string_value: replaced
            - const_expr:
                string_value: replaced
)EOF";

// Test RBAC configuration for lowerAscii function.
const std::string RBAC_CONFIG_WITH_LOWER_ASCII = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    policies:
      "condition_with_lower_ascii":
        permissions:
          - any: true
        principals:
          - any: true
        condition:
          call_expr:
            function: _==_
            args:
            - call_expr:
                target:
                  call_expr:
                    function: _[_]
                    args:
                    - select_expr:
                        operand:
                          ident_expr:
                            name: request
                        field: headers
                    - const_expr:
                        string_value: "x-test-case"
                function: lowerAscii
            - const_expr:
                string_value: hello
)EOF";

// Test RBAC configuration for upperAscii function.
const std::string RBAC_CONFIG_WITH_UPPER_ASCII = R"EOF(
name: rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    policies:
      "condition_with_upper_ascii":
        permissions:
          - any: true
        principals:
          - any: true
        condition:
          call_expr:
            function: _==_
            args:
            - call_expr:
                target:
                  call_expr:
                    function: _[_]
                    args:
                    - select_expr:
                        operand:
                          ident_expr:
                            name: request
                        field: headers
                    - const_expr:
                        string_value: "x-test-case"
                function: upperAscii
            - const_expr:
                string_value: WORLD
)EOF";

class StringFunctionsIntegrationTest : public HttpProtocolIntegrationTest {
public:
  ~StringFunctionsIntegrationTest() override = default;
  void setUpWithConfig(const std::string& rbac_config) {
    config_helper_.prependFilter(rbac_config);

    // Add bootstrap extension to enable CEL string functions
    const std::string cel_bootstrap_config = R"EOF(
      name: envoy.bootstrap.cel
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.bootstrap.cel.v3.CelEvaluatorConfig
        default_profile:
          enable_string_conversion: true
          enable_string_concat: true
          enable_string_functions: true
    )EOF";

    config_helper_.addBootstrapExtension(cel_bootstrap_config);
    HttpProtocolIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(
    Protocols, StringFunctionsIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

// Test replace() function with GET method - should be allowed.
TEST_P(StringFunctionsIntegrationTest, ReplaceMethodAllowed) {
  setUpWithConfig(RBAC_CONFIG_WITH_STRING_FUNCTIONS_REPLACE);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  // The request should be allowed because request.headers[":method"].replace("GET", "ALLOWED") ==
  // "ALLOWED" evaluates to true for GET requests.
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test replace() function with POST method - should be denied.
TEST_P(StringFunctionsIntegrationTest, ReplaceMethodDenied) {
  setUpWithConfig(RBAC_CONFIG_WITH_STRING_FUNCTIONS_REPLACE);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers = Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  ASSERT_TRUE(response->waitForEndStream());

  // The request should be denied because request.headers[":method"].replace("GET", "ALLOWED") ==
  // "ALLOWED" evaluates to false for POST requests.
  EXPECT_EQ("403", response->headers().getStatusValue());
}

// Test split() function with comma-separated values - should be allowed.
TEST_P(StringFunctionsIntegrationTest, SplitHeaderAllowed) {
  setUpWithConfig(RBAC_CONFIG_WITH_STRING_FUNCTIONS_SPLIT);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-test-values", "value1,value2,value3"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  // The request should be allowed because size(split result) >= 2 (returns 3 elements).
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test split() function with single value - should be denied.
TEST_P(StringFunctionsIntegrationTest, SplitHeaderDenied) {
  setUpWithConfig(RBAC_CONFIG_WITH_STRING_FUNCTIONS_SPLIT);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-test-values", "single-value"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  ASSERT_TRUE(response->waitForEndStream());

  // The request should be denied because size(split result) < 2 (returns 1 element).
  EXPECT_EQ("403", response->headers().getStatusValue());
}

// Test split() function with empty header - should be denied.
TEST_P(StringFunctionsIntegrationTest, SplitEmptyHeaderDenied) {
  setUpWithConfig(RBAC_CONFIG_WITH_STRING_FUNCTIONS_SPLIT);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-test-values", ""}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  ASSERT_TRUE(response->waitForEndStream());

  // The request should be denied because empty string split returns 1 element.
  EXPECT_EQ("403", response->headers().getStatusValue());
}

// Test replace() function with empty search string. CEL replace() with empty search returns
// the original string, so this should be denied.
TEST_P(StringFunctionsIntegrationTest, ReplaceEmptyStringAllowed) {
  setUpWithConfig(RBAC_CONFIG_WITH_STRING_FUNCTIONS_EMPTY_TEST);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-empty-test", ""}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  ASSERT_TRUE(response->waitForEndStream());

  // The request should be denied because replacing empty search returns original value.
  EXPECT_EQ("403", response->headers().getStatusValue());
}

// Test split() function with exact boundary condition (exactly 2 elements).
TEST_P(StringFunctionsIntegrationTest, SplitBoundaryConditionAllowed) {
  setUpWithConfig(RBAC_CONFIG_WITH_STRING_FUNCTIONS_SPLIT);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-test-values", "value1,value2"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  // The request should be allowed because size(split result) == 2.
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test missing header handling with split function.
TEST_P(StringFunctionsIntegrationTest, SplitMissingHeaderDenied) {
  setUpWithConfig(RBAC_CONFIG_WITH_STRING_FUNCTIONS_SPLIT);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers = Http::TestRequestHeaderMapImpl{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  // Note: x-test-values header is missing
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  ASSERT_TRUE(response->waitForEndStream());

  // The request should be denied because missing header results in evaluation failure.
  EXPECT_EQ("403", response->headers().getStatusValue());
}

// Test lowerAscii() function with mixed case input - should be allowed.
TEST_P(StringFunctionsIntegrationTest, LowerAsciiAllowed) {
  setUpWithConfig(RBAC_CONFIG_WITH_LOWER_ASCII);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-test-case", "HELLO"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  // The request should be allowed because "HELLO".lowerAscii() == "hello".
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test lowerAscii() function with already lowercase input - should be allowed.
TEST_P(StringFunctionsIntegrationTest, LowerAsciiAlreadyLowercase) {
  setUpWithConfig(RBAC_CONFIG_WITH_LOWER_ASCII);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-test-case", "hello"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  // The request should be allowed because "hello".lowerAscii() == "hello".
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test lowerAscii() function with different input - should be denied.
TEST_P(StringFunctionsIntegrationTest, LowerAsciiDenied) {
  setUpWithConfig(RBAC_CONFIG_WITH_LOWER_ASCII);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-test-case", "WORLD"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  ASSERT_TRUE(response->waitForEndStream());

  // The request should be denied because "WORLD".lowerAscii() != "hello".
  EXPECT_EQ("403", response->headers().getStatusValue());
}

// Test upperAscii() function with mixed case input - should be allowed.
TEST_P(StringFunctionsIntegrationTest, UpperAsciiAllowed) {
  setUpWithConfig(RBAC_CONFIG_WITH_UPPER_ASCII);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-test-case", "world"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  // The request should be allowed because "world".upperAscii() == "WORLD".
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test upperAscii() function with already uppercase input - should be allowed.
TEST_P(StringFunctionsIntegrationTest, UpperAsciiAlreadyUppercase) {
  setUpWithConfig(RBAC_CONFIG_WITH_UPPER_ASCII);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-test-case", "WORLD"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  // The request should be allowed because "WORLD".upperAscii() == "WORLD".
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test upperAscii() function with different input - should be denied.
TEST_P(StringFunctionsIntegrationTest, UpperAsciiDenied) {
  setUpWithConfig(RBAC_CONFIG_WITH_UPPER_ASCII);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto request_headers = Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                        {":path", "/"},
                                                        {":scheme", "http"},
                                                        {":authority", "host"},
                                                        {"x-test-case", "hello"}};
  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  ASSERT_TRUE(response->waitForEndStream());

  // The request should be denied because "hello".upperAscii() != "WORLD".
  EXPECT_EQ("403", response->headers().getStatusValue());
}

} // namespace
} // namespace Envoy

#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {
constexpr char kDefaultConfig[] = R"EOF(
  custom_responses:
  - name: 400_response
    status_code: 499
    local:
      inline_string: "not allowed"
    headers_to_add:
    - header:
        key: "foo"
        value: "x-bar"
      append: false
    body_format:
      text_format: "%LOCAL_REPLY_BODY% %RESPONSE_CODE%"
  - name: gateway_error_response
    remote:
      http_uri:
        uri: "https://foo.example/gateway_error"
        cluster: "foo"
        timeout:
          seconds: 2
    body_format:
      text_format: "<h1>%LOCAL_REPLY_BODY% %REQ(:path)%</h1>"
      content_type: "text/html; charset=UTF-8"
  custom_response_matcher:
    matcher_list:
      matchers:
        # Apply the 400_response policy to any 4xx response
      - predicate:
          single_predicate:
            input:
              name: status_code
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeClassMatchInput
            value_match:
              exact: "4xx"
        on_match:
          action:
            name: custom_response
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: 400_response
        # Apply the gateway_error_response policy to status codes 502, 503 and 504.
      - predicate:
          or_matcher:
            predicate:
            - single_predicate:
                input:
                  name: status_code
                  typed_config:
                    "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeMatchInput
                value_match:
                  exact: "502"
            - single_predicate:
                input:
                  name: status_code
                  typed_config:
                    "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeMatchInput
                value_match:
                  exact: "503"
            - single_predicate:
                input:
                  name: status_code
                  typed_config:
                    "@type": type.googleapis.com/envoy.type.matcher.v3.HttpResponseStatusCodeMatchInput
                value_match:
                  exact: "504"
        on_match:
          action:
            name: custom_response
            typed_config:
              "@type": type.googleapis.com/google.protobuf.StringValue
              value: gateway_error_response
  )EOF";

constexpr char kTestHeaderKey[] = "test-header";

} // namespace

class CustomResponseIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    setMaxRequestHeadersKb(60);
    setMaxRequestHeadersCount(100);

    auto some_route = config_helper_.createVirtualHost("some.route");
    config_helper_.addVirtualHost(some_route);

    auto foo = config_helper_.createVirtualHost("foo.example");
    foo.mutable_routes(0)->set_name("foo");
    foo.mutable_routes(0)->mutable_direct_response()->set_status(221);
    foo.mutable_routes(0)->mutable_direct_response()->mutable_body()->set_inline_string("foo");
    foo.mutable_routes(0)->mutable_match()->set_prefix("/");
    config_helper_.addVirtualHost(foo);

    config_helper_.addConfigModifier(
        [this](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* route_config = hcm.mutable_route_config();
          // adding direct response mode to the default route
          auto* default_route =
              hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
          default_route->mutable_match()->set_prefix("/default");
          default_route->mutable_direct_response()->set_status(
              static_cast<uint32_t>(Http::Code::OK));
          // Use inline bytes rather than a filename to avoid using a path that may look illegal
          // to Envoy.
          default_route->mutable_direct_response()->mutable_body()->set_inline_bytes(
              "Response body");
          // adding headers to the default route
          auto* header_value_option = route_config->mutable_response_headers_to_add()->Add();
          header_value_option->mutable_header()->set_value("direct-response-enabled");
          header_value_option->mutable_header()->set_key("x-direct-response-header");

          auto* filter = hcm.mutable_http_filters()->Add();
          filter->set_name("envoy.filters.http.custom_response");
          const auto default_configuration = TestUtility::parseYaml<
              envoy::extensions::filters::http::custom_response::v3::CustomResponse>(
              custom_response_filter_config_);
          filter->mutable_typed_config()->PackFrom(default_configuration);
          hcm.mutable_http_filters()->SwapElements(0, 1);
        });
    HttpProtocolIntegrationTest::initialize();
  }

  CustomResponseIntegrationTest()
      : HttpProtocolIntegrationTest(), custom_response_filter_config_{kDefaultConfig} {}

protected:
  Http::TestResponseHeaderMapImpl unauthorized_response_{{":status", "401"},
                                                         {"content-length", "0"}};
  Http::TestResponseHeaderMapImpl gateway_error_response_{{":status", "502"},
                                                          {"content-length", "0"}};
  Envoy::Http::LowerCaseString test_header_key_{kTestHeaderKey};
  std::string custom_response_filter_config_;
  // std::vector<FakeHttpConnectionPtr> upstream_connections_;
};

TEST_P(CustomResponseIntegrationTest, CustomResponseNotConfigured) {

  // Use base class initialize.
  HttpProtocolIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, unauthorized_response_, 0);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("401", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_4xx")->value());
}

TEST_P(CustomResponseIntegrationTest, LocalReply) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, unauthorized_response_,
                                                0, 0, std::chrono::minutes(15));
  EXPECT_EQ("499", response->headers().getStatusValue());
  // EXPECT_EQ("not allowed", response->body()); //TODO
  // EXPECT_EQ("x-bar",
  // response->headers().get(Http::LowerCaseString("foo"))[0]->value().getStringView());
}

TEST_P(CustomResponseIntegrationTest, RemoteDataSource) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response = sendRequestAndWaitForResponse(
      default_request_headers_, 0, gateway_error_response_, 0, 0, std::chrono::minutes(15));
  EXPECT_EQ("221", response->headers().getStatusValue());
  // TODO: add header and body modifications
}

TEST_P(CustomResponseIntegrationTest, RouteNotFound) {
  custom_response_filter_config_.replace(custom_response_filter_config_.find("foo."), 4, "fo1.");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response = sendRequestAndWaitForResponse(
      default_request_headers_, 0, gateway_error_response_, 0, 0, std::chrono::minutes(15));
  EXPECT_EQ("502", response->headers().getStatusValue());

  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_5xx")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.custom_response_redirect_no_route")->value());
  // TODO: add header and body modifications
}

// TODO: add a test with route-specific filters

INSTANTIATE_TEST_SUITE_P(Protocols, CustomResponseIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);
} // namespace Envoy

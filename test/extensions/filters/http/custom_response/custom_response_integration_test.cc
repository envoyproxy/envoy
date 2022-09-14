#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_protocol_integration.h"

#include "utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

using envoy::config::route::v3::Route;
using envoy::config::route::v3::VirtualHost;
using envoy::extensions::filters::http::custom_response::v3::CustomResponse;
using Envoy::Protobuf::MapPair;
using Envoy::ProtobufWkt::Any;

namespace {

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
          const auto default_configuration =
              TestUtility::parseYaml<CustomResponse>(custom_response_filter_config_);
          filter->mutable_typed_config()->PackFrom(default_configuration);
          hcm.mutable_http_filters()->SwapElements(0, 1);
        });
    HttpProtocolIntegrationTest::initialize();
  }

  CustomResponseIntegrationTest()
      : HttpProtocolIntegrationTest(), custom_response_filter_config_{kDefaultConfig} {}

  void setPerRouteConfig(Route* route, const CustomResponse& cfg) {
    Any cfg_any;
    ASSERT_TRUE(cfg_any.PackFrom(cfg));
    route->mutable_typed_per_filter_config()->insert(
        MapPair<std::string, Any>("envoy.filters.http.custom_response", cfg_any));
  }

  void setPerHostConfig(VirtualHost& vh, const CustomResponse& cfg) {
    Any cfg_any;
    ASSERT_TRUE(cfg_any.PackFrom(cfg));
    vh.mutable_typed_per_filter_config()->insert(
        MapPair<std::string, Any>("envoy.filters.http.custom_response", cfg_any));
  }

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
  EXPECT_EQ("not allowed", response->body()); // TODO
  EXPECT_EQ("x-bar",
            response->headers().get(Http::LowerCaseString("foo"))[0]->value().getStringView());
}

TEST_P(CustomResponseIntegrationTest, RemoteDataSource) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response = sendRequestAndWaitForResponse(
      default_request_headers_, 0, gateway_error_response_, 0, 0, std::chrono::minutes(15));
  EXPECT_EQ("221", response->headers().getStatusValue());
  EXPECT_EQ(0,
            test_server_->counter("http.config_test.custom_response_redirect_no_route")->value());
  EXPECT_EQ(
      0, test_server_->counter("http.config_test.custom_response_redirect_invalid_uri")->value());
  EXPECT_EQ("x-bar2",
            response->headers().get(Http::LowerCaseString("foo2"))[0]->value().getStringView());
  // TODO: add header and body modifications
}

TEST_P(CustomResponseIntegrationTest, RouteNotFound) {
  // Modify custom response route so there is no matching route entry
  custom_response_filter_config_.replace(custom_response_filter_config_.find("foo."), 4, "fo1.");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0);
  EXPECT_EQ("502", response->headers().getStatusValue());

  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_5xx")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.custom_response_redirect_no_route")->value());
  // TODO: add header and body modifications
}

TEST_P(CustomResponseIntegrationTest, RouteSpecificFilter) {
  // Modify custom response route so there is no matching route entry for hcm
  // filter config
  custom_response_filter_config_.replace(custom_response_filter_config_.find("foo."), 4, "fo1.");

  // Add per route filter config
  auto some_other_host = config_helper_.createVirtualHost("some.other.host");
  setPerRouteConfig(some_other_host.mutable_routes(0),
                    TestUtility::parseYaml<CustomResponse>(std::string(kDefaultConfig)));
  config_helper_.addVirtualHost(some_other_host);

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.other.host");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0);
  EXPECT_EQ("221", response->headers().getStatusValue());

  EXPECT_EQ(0, test_server_->counter("http.config_test.downstream_rq_5xx")->value());
  EXPECT_EQ(0,
            test_server_->counter("http.config_test.custom_response_redirect_no_route")->value());
  EXPECT_EQ(
      0, test_server_->counter("http.config_test.custom_response_redirect_invalid_uri")->value());
  //  TODO: add header and body modifications
}

INSTANTIATE_TEST_SUITE_P(Protocols, CustomResponseIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);
} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

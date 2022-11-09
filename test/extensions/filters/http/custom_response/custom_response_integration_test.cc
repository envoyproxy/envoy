#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"
#include "envoy/extensions/filters/http/custom_response/v3/policies.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/policies.pb.validate.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

using envoy::config::route::v3::Route;
using envoy::config::route::v3::VirtualHost;
using envoy::extensions::filters::http::custom_response::v3::CustomResponse;
using LocalResponsePolicyProto =
    envoy::extensions::filters::http::custom_response::v3::LocalResponsePolicy;
using RedirectPolicyProto = envoy::extensions::filters::http::custom_response::v3::RedirectPolicy;
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

    // Add a virtual host for the original request.
    auto some_route = config_helper_.createVirtualHost("some.route");
    config_helper_.addVirtualHost(some_route);

    // Add a virtual host corresponding to redirected route for
    // gateway_error_action in kDefaultConfig. Note that the redirect policy
    // overwrites the response code specified here.
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
          // adding direct response mode to the default route
          auto* default_route =
              hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
          // Restrict default route [*] to paths with /default
          default_route->mutable_match()->set_prefix("/default");
          default_route->mutable_direct_response()->set_status(static_cast<uint32_t>(201));
          // Use inline bytes rather than a filename to avoid using a path that may look illegal
          // to Envoy.
          default_route->mutable_direct_response()->mutable_body()->set_inline_bytes(
              "Response body");

          // Add the custom response filter to the http filter chain.
          auto* filter = hcm.mutable_http_filters()->Add();
          filter->set_name("envoy.filters.http.custom_response");
          filter->mutable_typed_config()->PackFrom(custom_response_filter_config_);
          hcm.mutable_http_filters()->SwapElements(0, 1);
          int cer_position = 0;

          // If a test point populates filter configs in filters_before_cer_ add
          // them to the http filter chain config here.
          for (const auto& config : filters_before_cer_) {
            auto* filter = hcm.mutable_http_filters()->Add();
            TestUtility::loadFromYaml(config, *filter);
            // swap with router filter
            hcm.mutable_http_filters()->SwapElements(hcm.mutable_http_filters()->size() - 2,
                                                     hcm.mutable_http_filters()->size() - 1);
            // swap with custom response filter
            hcm.mutable_http_filters()->SwapElements(hcm.mutable_http_filters()->size() - 2,
                                                     cer_position);
            cer_position = hcm.mutable_http_filters()->size() - 2;
          }
          // If a test point populates filter configs in filters_after_cer_ add
          // them to the http filter chain config here.
          for (const auto& config : filters_after_cer_) {
            auto* filter = hcm.mutable_http_filters()->Add();
            TestUtility::loadFromYaml(config, *filter);
            // swap with router filter
            hcm.mutable_http_filters()->SwapElements(hcm.mutable_http_filters()->size() - 2,
                                                     hcm.mutable_http_filters()->size() - 1);
          }
        });
    HttpProtocolIntegrationTest::initialize();
  }

  CustomResponseIntegrationTest() {
    custom_response_filter_config_ =
        TestUtility::parseYaml<CustomResponse>(std::string(kDefaultConfig));
  }

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

  void setLocalResponseFor5xx() {
    auto& matcher = custom_response_filter_config_.mutable_custom_response_matcher()
                        ->mutable_matcher_list()
                        ->mutable_matchers()
                        ->at(0);
    matcher.mutable_predicate()
        ->mutable_single_predicate()
        ->mutable_value_match()
        ->mutable_exact()
        ->assign("5xx");
  }

protected:
  Http::TestResponseHeaderMapImpl unauthorized_response_{{":status", "401"},
                                                         {"content-length", "0"}};
  Http::TestResponseHeaderMapImpl gateway_error_response_{{":status", "502"},
                                                          {"content-length", "0"}};
  Http::TestResponseHeaderMapImpl okay_response_{{":status", "201"}, {"content-length", "0"}};
  Http::TestResponseHeaderMapImpl internal_server_error_{{":status", "500"},
                                                         {"content-length", "0"}};

  Envoy::Http::LowerCaseString test_header_key_{kTestHeaderKey};
  CustomResponse custom_response_filter_config_;
  std::vector<std::string> filters_before_cer_;
  std::vector<std::string> filters_after_cer_;
};

// Verify that we get expected error response if the custom response is not configured.
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

// Verify we get the correct local custom response.
TEST_P(CustomResponseIntegrationTest, LocalReply) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, unauthorized_response_, 0);
  // Verify that we get the modified status value.
  EXPECT_EQ("499", response->headers().getStatusValue());
  EXPECT_EQ("not allowed", response->body());
  EXPECT_EQ("x-bar",
            response->headers().get(Http::LowerCaseString("foo"))[0]->value().getStringView());
}

// Verify we get the correct remote custom response.
TEST_P(CustomResponseIntegrationTest, RemoteDataSource) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response = sendRequestAndWaitForResponse(
      default_request_headers_, 0, gateway_error_response_, 0, 0, std::chrono::minutes(20));
  // Verify we get the modified status value.
  EXPECT_EQ("299", response->headers().getStatusValue());
  EXPECT_EQ(0,
            test_server_->counter("http.config_test.custom_response_redirect_no_route")->value());
  EXPECT_EQ("x-bar2",
            response->headers().get(Http::LowerCaseString("foo2"))[0]->value().getStringView());
}

// Verify we get the original response if the route is not found for the
// specified custom response.
TEST_P(CustomResponseIntegrationTest, RouteNotFound) {
  modifyPolicy<RedirectPolicyProto>(
      custom_response_filter_config_, "gateway_error_action",
      [](RedirectPolicyProto& policy) { policy.set_host("https://fo1.example"); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0);
  // Verify we get the original error status code.
  EXPECT_EQ("502", response->headers().getStatusValue());

  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_5xx")->value());
  EXPECT_EQ(1,
            test_server_->counter("http.config_test.custom_response_redirect_no_route")->value());
}

// Verify that the route specific filter is picked if specified.
TEST_P(CustomResponseIntegrationTest, RouteSpecificFilter) {
  // Add per route filter config to a new virtual host.
  auto some_other_host = config_helper_.createVirtualHost("some.other.host");
  auto per_route_config = TestUtility::parseYaml<CustomResponse>(std::string(kDefaultConfig));
  modifyPolicy<RedirectPolicyProto>(
      per_route_config, "gateway_error_action", [](RedirectPolicyProto& policy) {
        policy.mutable_status_code()->set_value(291);
        policy.mutable_response_headers_to_add()->at(0).mutable_header()->mutable_value()->assign(
            "y-foo2");
      });

  setPerRouteConfig(some_other_host.mutable_routes(0), per_route_config);
  config_helper_.addVirtualHost(some_other_host);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Send request to host with per route config
  default_request_headers_.setHost("some.other.host");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0, 0);
  // Verify we get the status code of the local config
  EXPECT_EQ("291", response->headers().getStatusValue());
  EXPECT_EQ("y-foo2",
            response->headers().get(Http::LowerCaseString("foo2"))[0]->value().getStringView());

  EXPECT_EQ(0, test_server_->counter("http.config_test.downstream_rq_5xx")->value());
  EXPECT_EQ(0, test_server_->counter("custom_response_redirect_no_route")->value());

  // Send request to host without per route config
  default_request_headers_.setHost("some.route");
  response = sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0);
  // Verify we get the modified status value.
  EXPECT_EQ("299", response->headers().getStatusValue());
  EXPECT_EQ(0,
            test_server_->counter("http.config_test.custom_response_redirect_no_route")->value());
  EXPECT_EQ("x-bar2",
            response->headers().get(Http::LowerCaseString("foo2"))[0]->value().getStringView());
}

// Verify that we don't pick a route specific config in the absence of an hcm
// config
TEST_P(CustomResponseIntegrationTest, OnlyRouteSpecificFilter) {
  // Don't create custom response filter for the hcm.
  custom_response_filter_config_.clear_custom_response_matcher();

  // Add per route filter config
  auto some_other_host = config_helper_.createVirtualHost("some.other.host");
  // std::string per_route_config(kDefaultConfig);
  auto per_route_config = TestUtility::parseYaml<CustomResponse>(std::string(kDefaultConfig));
  modifyPolicy<RedirectPolicyProto>(
      per_route_config, "gateway_error_action", [](RedirectPolicyProto& policy) {
        policy.mutable_status_code()->set_value(291);
        policy.mutable_response_headers_to_add()->at(0).mutable_header()->mutable_value()->assign(
            "y-foo2");
      });

  setPerRouteConfig(some_other_host.mutable_routes(0), per_route_config);
  config_helper_.addVirtualHost(some_other_host);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("some.route");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0);
  // Verify we get the original error status code.
  EXPECT_EQ("502", response->headers().getStatusValue());

  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_5xx")->value());
}

// Verify that we do not provide a custom response for a response that has
// already been redirected by the custom response filter.
// Verify that the route specific filter is picked if specified.
TEST_P(CustomResponseIntegrationTest, NoRecursion) {
  // Make the remote response for gateway_error policy return 401
  auto fo1 = config_helper_.createVirtualHost("fo1.example");
  fo1.mutable_routes(0)->set_name("fo1");
  fo1.mutable_routes(0)->mutable_direct_response()->set_status(401);
  fo1.mutable_routes(0)->mutable_direct_response()->mutable_body()->set_inline_string("fo1");
  fo1.mutable_routes(0)->mutable_match()->set_prefix("/");
  config_helper_.addVirtualHost(fo1);
  modifyPolicy<RedirectPolicyProto>(
      custom_response_filter_config_, "gateway_error_action",
      [](RedirectPolicyProto& policy) { policy.set_host("https://fo1.example"); });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Verify that a 401 response will trigger 400_response policy
  default_request_headers_.setHost("some.route");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, unauthorized_response_, 0);
  // Verify that we get the modified status value.
  EXPECT_EQ("499", response->headers().getStatusValue());
  EXPECT_EQ("not allowed", response->body());
  EXPECT_EQ("x-bar",
            response->headers().get(Http::LowerCaseString("foo"))[0]->value().getStringView());

  // Verify that 400_response policy cannot be triggered by the
  // gateway_error_response policy
  default_request_headers_.setHost("some.route");
  response = sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0);
  // Verify we get status code for fo1.example
  EXPECT_EQ("401", response->headers().getStatusValue());
}

// Verify that we can NOT intercept local replies sent during decode
TEST_P(CustomResponseIntegrationTest, DecodeLocalReplyBeforeCER) {
  // Add filter that sends local reply after.
  filters_before_cer_.emplace_back(R"EOF(
name: local-reply-during-decode
typed_config:
  "@type": type.googleapis.com/google.protobuf.Struct
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  // Verify we don't get a modified status value.
  EXPECT_EQ("500", response->headers().getStatusValue());
}

// Verify that we do intercept local replies sent during decode with the local
// response policy.
TEST_P(CustomResponseIntegrationTest, DecodeLocalReplyBeforeCERLocalReplyPolicy) {
  // Add filter that sends local reply after.
  filters_before_cer_.emplace_back(R"EOF(
name: local-reply-during-decode
typed_config:
  "@type": type.googleapis.com/google.protobuf.Struct
)EOF");

  setLocalResponseFor5xx();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  // Verify we do get a modified status value.
  EXPECT_EQ("499", response->headers().getStatusValue());
  EXPECT_EQ("not allowed", response->body());
}

// Verify that we do intercept local replies sent during decode with the local
// response policy.
TEST_P(CustomResponseIntegrationTest, DecodeLocalReplyAfterCERLocalReplyPolicy) {
  // Add filter that sends local reply after.
  filters_after_cer_.emplace_back(R"EOF(
name: local-reply-during-decode
typed_config:
  "@type": type.googleapis.com/google.protobuf.Struct
)EOF");

  setLocalResponseFor5xx();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  // Verify we do get a modified status value.
  EXPECT_EQ("499", response->headers().getStatusValue());
  EXPECT_EQ("not allowed", response->body());
}

// Verify that we can NOT intercept local replies sent during encode
TEST_P(CustomResponseIntegrationTest, EncodeLocalReplyBeforeCERLocalReplyPolicy) {
  // Add filter that sends local reply after.
  filters_before_cer_.emplace_back(R"EOF(
name: local-reply-during-encode
typed_config:
  "@type": type.googleapis.com/google.protobuf.Struct
)EOF");

  setLocalResponseFor5xx();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, okay_response_, 0);
  // Verify we don't get the modified status value.
  EXPECT_EQ("500", response->headers().getStatusValue());
}

// Verify that we can NOT intercept local replies sent during encode
TEST_P(CustomResponseIntegrationTest, EncodeLocalReplyBeforeCER) {
  // Add filter that sends local reply after.
  filters_before_cer_.emplace_back(R"EOF(
name: local-reply-during-encode
typed_config:
  "@type": type.googleapis.com/google.protobuf.Struct
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, okay_response_, 0);
  // Verify we don't get the modified status value.
  EXPECT_EQ("500", response->headers().getStatusValue());
}

// Verify that we can NOT intercept local replies sent during encode
TEST_P(CustomResponseIntegrationTest, EncodeLocalReplyAfterCER) {
  // Add filter that sends local reply after.
  filters_after_cer_.emplace_back(R"EOF(
name: local-reply-during-encode
typed_config:
  "@type": type.googleapis.com/google.protobuf.Struct
)EOF");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, okay_response_, 0);
  // Verify we don't get the modified status value.
  EXPECT_EQ("500", response->headers().getStatusValue());
}

// Verify that we can't yet intercept local replies sent during decode if they don't
// apply to the redirected route.
// TODO(pradeepcrao): Make this work by modifying the interaction between
// sendLocalReply and recreateStream
TEST_P(CustomResponseIntegrationTest, RouteSpecificDecodeLocalReplyBeforeRedirectedCER) {
  // Add filter that sends local reply before.
  filters_before_cer_.emplace_back(R"EOF(
name: local-reply-during-decode-if-not-cer
typed_config:
  "@type": type.googleapis.com/google.protobuf.Struct
)EOF");
  SimpleFilterConfig<LocalReplyDuringDecodeIfNotCER> factory;
  Envoy::Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registration(
      factory);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  //     Verify we do not get a modified status value. (500 instead of 292)
  EXPECT_EQ("500", response->headers().getStatusValue());
}

TEST_P(CustomResponseIntegrationTest, RouteSpecificDecodeLocalReplyAfterRedirectedCER) {
  // Add filter that sends local reply after.
  filters_after_cer_.emplace_back(R"EOF(
name: local-reply-during-decode-if-not-cer
typed_config:
  "@type": type.googleapis.com/google.protobuf.Struct
)EOF");
  SimpleFilterConfig<LocalReplyDuringDecodeIfNotCER> factory;
  Envoy::Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registration(
      factory);
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("some.route");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  //     Verify we do not get a modified status value. (500 instead of 292)
  EXPECT_EQ("500", response->headers().getStatusValue());
}

// Verify that the route meant for custom response redirection can be routed to with the required
// header.
TEST_P(CustomResponseIntegrationTest, RouteHeaderMatch) {
  // Add route with header matcher
  auto some_other_host = config_helper_.createVirtualHost("some.other.host");
  some_other_host.mutable_routes(0)->mutable_match()->set_prefix("/");
  // "cer-only" header needs to be present for route matching.
  auto header = some_other_host.mutable_routes(0)->mutable_match()->mutable_headers()->Add();
  header->set_name("cer-only");
  some_other_host.mutable_routes(0)->mutable_direct_response()->mutable_body()->set_inline_bytes(
      "cer-only-response");
  some_other_host.mutable_routes(0)->mutable_direct_response()->set_status(202);
  config_helper_.addVirtualHost(some_other_host);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Verify some.other.host is reachable via the redirect policy with the
  // "cer-only" header added.
  default_request_headers_.setHost("some.route");
  auto response2 = sendRequestAndWaitForResponse(
      default_request_headers_, 0, internal_server_error_, 0, 0, std::chrono::minutes(20));
  EXPECT_EQ("292", response2->headers().getStatusValue());
  EXPECT_EQ("cer-only-response", response2->body());
  default_request_headers_.setHost("some.other.host");

  // Verify some.other.host is not directly reachable.
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  // Verify that the route was not found.
  EXPECT_EQ("499", response->headers().getStatusValue());
}

// Test ModifyRequestHeadersAction
TEST_P(CustomResponseIntegrationTest, ModifyRequestHeaders) {
  // Add modify_request_headers_action to the config.
  modifyPolicy<RedirectPolicyProto>(custom_response_filter_config_, "520_action",
                                    [](RedirectPolicyProto& policy) {
                                      auto action = policy.mutable_modify_request_headers_action();
                                      action->set_name("modify-request-headers-action");
                                      action->mutable_typed_config()->mutable_type_url()->assign(
                                          "type.googleapis.com/google.protobuf.Struct");
                                    });

  TestModifyRequestHeadersActionFactory factory;
  Envoy::Registry::InjectFactory<ModifyRequestHeadersActionFactory> registration(factory);

  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes()->Add();
        route->mutable_match()->set_prefix("/internal_server_error");
        auto header = route->mutable_match()->mutable_headers()->Add();
        header->set_name("x-envoy-cer-backend");
        header->mutable_string_match()->set_exact("global/storage");
        route->mutable_direct_response()->set_status(static_cast<uint32_t>(220));
        // Use inline bytes rather than a filename to avoid using a path that may look illegal
        // to Envoy.
        route->mutable_direct_response()->mutable_body()->set_inline_bytes(
            "Modify action response body");
      });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("some.route");
  auto response = sendRequestAndWaitForResponse(
      default_request_headers_, 0,
      Http::TestResponseHeaderMapImpl{{":status", "520"}, {"content-length", "0"}}, 0, 0,
      std::chrono::minutes(20));
  EXPECT_EQ("220", response->headers().getStatusValue());
  EXPECT_EQ("Modify action response body", response->body());
}

INSTANTIATE_TEST_SUITE_P(Protocols, CustomResponseIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);
} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

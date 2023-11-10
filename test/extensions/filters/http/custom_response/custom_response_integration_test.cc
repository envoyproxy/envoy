#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"
#include "test/integration/http_protocol_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

using envoy::config::route::v3::VirtualHost;
using envoy::extensions::filters::http::custom_response::v3::CustomResponse;
using LocalResponsePolicyProto =
    envoy::extensions::http::custom_response::local_response_policy::v3::LocalResponsePolicy;
using RedirectPolicyProto =
    envoy::extensions::http::custom_response::redirect_policy::v3::RedirectPolicy;
using RedirectActionProto = envoy::config::route::v3::RedirectAction;
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
    auto virtual_host = config_helper_.createVirtualHost("original.host");
    config_helper_.addVirtualHost(virtual_host);

    // Add a virtual host corresponding to redirected route for
    // gateway_error_action in kDefaultConfig. Note that the redirect policy
    // overwrites the response code specified here.
    config_helper_.addVirtualHost(TestUtility::parseYaml<VirtualHost>(R"EOF(
    name: foo
    domains: ["foo.example"]
    routes:
    - direct_response:
        status: 221
        body:
          inline_string: foo
      match:
        prefix: "/"
    )EOF"));

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

          // If a test point populates filter configs to be added before the
          // custom response filter, add them to the http filter chain config here.
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
          // If a test point populates filter configs to be added after the
          // custom response filter, add them to the http filter chain config here.
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

  void addVirtualHostWithPerRouteConfig(const char* virtual_host_domain) {
    auto host = config_helper_.createVirtualHost(virtual_host_domain);
    auto per_route_config = TestUtility::parseYaml<CustomResponse>(std::string(kDefaultConfig));
    modifyPolicy<RedirectPolicyProto>(
        per_route_config, "gateway_error_action", [](RedirectPolicyProto& policy) {
          policy.mutable_status_code()->set_value(291);
          policy.mutable_response_headers_to_add()->at(0).mutable_header()->mutable_value()->assign(
              "y-foo2");
        });

    Any cfg_any;
    ASSERT_TRUE(cfg_any.PackFrom(per_route_config));
    host.mutable_routes(0)->mutable_typed_per_filter_config()->insert(
        MapPair<std::string, Any>("envoy.filters.http.custom_response", cfg_any));
    config_helper_.addVirtualHost(host);
  }

  Any createCerSinglePredicateConfig(absl::string_view match_code, int override_code) {
    auto cer_config = TestUtility::parseYaml<CustomResponse>(std::string(kSinglePredicateConfig));

    // Assign the code to match against.
    cer_config.mutable_custom_response_matcher()
        ->mutable_matcher_list()
        ->mutable_matchers()
        ->at(0)
        .mutable_predicate()
        ->mutable_single_predicate()
        ->mutable_value_match()
        ->mutable_exact()
        ->assign(match_code);
    // Assign the override response code.
    modifyPolicy<RedirectPolicyProto>(
        cer_config, "gateway_error_action", [override_code](RedirectPolicyProto& policy) {
          policy.mutable_status_code()->set_value(override_code);
          policy.mutable_response_headers_to_add()->at(0).mutable_header()->mutable_value()->assign(
              "y-foo2");
        });

    Any cfg_any;
    cfg_any.PackFrom(cer_config);
    return cfg_any;
  }

  void setPerHostConfig(VirtualHost& vh, const CustomResponse& cfg) {
    Any cfg_any;
    ASSERT_TRUE(cfg_any.PackFrom(cfg));
    vh.mutable_typed_per_filter_config()->insert(
        MapPair<std::string, Any>("envoy.filters.http.custom_response", cfg_any));
  }

  // Change the matcher to match `5xx` for local response policies.
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
  ::Envoy::Http::TestResponseHeaderMapImpl unauthorized_response_{{":status", "401"},
                                                                  {"content-length", "0"}};
  ::Envoy::Http::TestResponseHeaderMapImpl gateway_error_response_{{":status", "502"},
                                                                   {"content-length", "0"}};
  ::Envoy::Http::TestResponseHeaderMapImpl okay_response_{{":status", "201"},
                                                          {"content-length", "0"}};
  ::Envoy::Http::TestResponseHeaderMapImpl internal_server_error_{{":status", "500"},
                                                                  {"content-length", "0"}};

  ::Envoy::Http::LowerCaseString test_header_key_{kTestHeaderKey};
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
  // Verify that the original response is not modified.
  EXPECT_EQ("401", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_4xx")->value());
}

// Verify we get the correct local custom response.
TEST_P(CustomResponseIntegrationTest, LocalReply) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("original.host");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, unauthorized_response_, 0, 0);
  // Verify that we get the modified status value.
  EXPECT_EQ("499", response->headers().getStatusValue());
  EXPECT_EQ("not allowed", response->body());
  EXPECT_EQ(
      "x-bar",
      response->headers().get(::Envoy::Http::LowerCaseString("foo"))[0]->value().getStringView());
}

// Verify we get the correct local custom response.
TEST_P(CustomResponseIntegrationTest, LocalReplyWithFormatter) {

  modifyPolicy<LocalResponsePolicyProto>(
      custom_response_filter_config_, "4xx_action", [](LocalResponsePolicyProto& policy) {
        *policy.mutable_body_format() =
            TestUtility::parseYaml<envoy::config::core::v3::SubstitutionFormatString>(R"EOF(
json_format:
  message: "%LOCAL_REPLY_BODY%"
          )EOF");
      });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("original.host");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, unauthorized_response_, 0);
  // Verify that we get the modified status value.
  EXPECT_EQ("499", response->headers().getStatusValue());
  EXPECT_EQ("{\"message\":\"not allowed\"}\n", response->body());
  EXPECT_EQ(
      "x-bar",
      response->headers().get(::Envoy::Http::LowerCaseString("foo"))[0]->value().getStringView());
}

// Verify we get the correct custom response using the redirect policy.
// TODO(pradeepcrao): Add a test that returns a redirected response from an
// upstream.
TEST_P(CustomResponseIntegrationTest, RedirectPolicyResponse) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("original.host");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0, 0);
  // Verify we get the modified status value.
  EXPECT_EQ("299", response->headers().getStatusValue());
  EXPECT_EQ(0,
            test_server_->counter("http.config_test.custom_response_redirect_no_route")->value());
  EXPECT_EQ(
      "x-bar2",
      response->headers().get(::Envoy::Http::LowerCaseString("foo2"))[0]->value().getStringView());
}

// Verify we get the original response if the route is not found for the
// specified custom response.
TEST_P(CustomResponseIntegrationTest, RouteNotFound) {

  // Modify the host for the redirect policy so no match is found in the route
  // table for the internal redirect.
  modifyPolicy<RedirectPolicyProto>(
      custom_response_filter_config_, "gateway_error_action",
      [](RedirectPolicyProto& policy) { policy.set_uri("https://fo1.example"); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("original.host");
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
  addVirtualHostWithPerRouteConfig("host.with.route_specific.cer_config");

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Send request to host with per route config
  default_request_headers_.setHost("host.with.route_specific.cer_config");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0, 0);
  // Verify we get the status code of the local config.
  EXPECT_EQ("291", response->headers().getStatusValue());
  EXPECT_EQ(
      "y-foo2",
      response->headers().get(::Envoy::Http::LowerCaseString("foo2"))[0]->value().getStringView());

  EXPECT_EQ(0, test_server_->counter("http.config_test.downstream_rq_5xx")->value());
  EXPECT_EQ(0, test_server_->counter("custom_response_redirect_no_route")->value());

  // Send request to host without per route config
  default_request_headers_.setHost("original.host");
  response = sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0);
  // Verify we get the modified status value of the http connection manager
  // level config.
  EXPECT_EQ("299", response->headers().getStatusValue());
  EXPECT_EQ(0,
            test_server_->counter("http.config_test.custom_response_redirect_no_route")->value());
  EXPECT_EQ(
      "x-bar2",
      response->headers().get(::Envoy::Http::LowerCaseString("foo2"))[0]->value().getStringView());
}

// Verify that we don't pick a route specific config in the absence of an hcm
// config
TEST_P(CustomResponseIntegrationTest, OnlyRouteSpecificFilter) {
  // Don't create custom response filter for the hcm.
  custom_response_filter_config_.clear_custom_response_matcher();

  // Add per route filter config
  addVirtualHostWithPerRouteConfig("host.with.route_specific.cer_config");

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("original.host");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0);
  // Verify we get the original error status code.
  EXPECT_EQ("502", response->headers().getStatusValue());

  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_5xx")->value());

  // Send request to host with per route config.
  default_request_headers_.setHost("host.with.route_specific.cer_config");
  response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0, 0);
  // Verify we get the status code of the local config.
  EXPECT_EQ("291", response->headers().getStatusValue());
  EXPECT_EQ(
      "y-foo2",
      response->headers().get(::Envoy::Http::LowerCaseString("foo2"))[0]->value().getStringView());
}

// Verify that the route specific filter is picked if specified.
TEST_P(CustomResponseIntegrationTest, MatcherHierarchy) {
  // Create Virtual host with per route and per host config.
  auto host = config_helper_.createVirtualHost("host.with.route_specific.cer_config");
  // Add per route typed filter config.
  auto per_route_config = createCerSinglePredicateConfig("503", 293);
  host.mutable_routes(0)->mutable_typed_per_filter_config()->insert(
      MapPair<std::string, Any>("envoy.filters.http.custom_response", per_route_config));
  // Add per vhost typed filter config.
  auto per_virtual_host_config = createCerSinglePredicateConfig("501", 291);
  host.mutable_typed_per_filter_config()->insert(
      MapPair<std::string, Any>("envoy.filters.http.custom_response", per_virtual_host_config));
  config_helper_.addVirtualHost(host);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  {
    // Send request where response matches against route level config.
    default_request_headers_.setHost("host.with.route_specific.cer_config");
    auto response = sendRequestAndWaitForResponse(
        default_request_headers_, 0, {{":status", "503"}, {"content-length", "0"}}, 0, 0);
    // Verify we get the status code of route level config.
    EXPECT_EQ("293", response->headers().getStatusValue());
  }

  {
    // Send request where response matches against vhost level config.
    default_request_headers_.setHost("host.with.route_specific.cer_config");
    auto response = sendRequestAndWaitForResponse(
        default_request_headers_, 0, {{":status", "501"}, {"content-length", "0"}}, 0, 0);
    // Verify we get the status code of vhost level config.
    EXPECT_EQ("291", response->headers().getStatusValue());
  }

  {
    // Send request where response matches against filter level config.
    default_request_headers_.setHost("host.with.route_specific.cer_config");
    auto response = sendRequestAndWaitForResponse(
        default_request_headers_, 0, {{":status", "504"}, {"content-length", "0"}}, 0, 0);
    // Verify we get the status code of filter level config.
    EXPECT_EQ("299", response->headers().getStatusValue());
  }

  EXPECT_EQ(0, test_server_->counter("http.config_test.downstream_rq_5xx")->value());
  EXPECT_EQ(0, test_server_->counter("custom_response_redirect_no_route")->value());
}

TEST_P(CustomResponseIntegrationTest, MostSpecificConfig) {
  // Create Virtual host with per route and per host config.
  auto host = config_helper_.createVirtualHost("host.with.route_specific.cer_config");
  // Add per route typed filter config.
  auto per_route_config = createCerSinglePredicateConfig("503", 293);
  host.mutable_routes(0)->mutable_typed_per_filter_config()->insert(
      MapPair<std::string, Any>("envoy.filters.http.custom_response", per_route_config));
  // Add per vhost typed filter config.
  auto per_virtual_host_config = createCerSinglePredicateConfig("503", 291);
  host.mutable_typed_per_filter_config()->insert(
      MapPair<std::string, Any>("envoy.filters.http.custom_response", per_virtual_host_config));
  config_helper_.addVirtualHost(host);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Send request where response matches against both route level and vhost
  // level config.
  default_request_headers_.setHost("host.with.route_specific.cer_config");
  auto response = sendRequestAndWaitForResponse(
      default_request_headers_, 0, {{":status", "503"}, {"content-length", "0"}}, 0, 0);
  // Verify we get the status code of route level config.
  EXPECT_EQ("293", response->headers().getStatusValue());

  EXPECT_EQ(0, test_server_->counter("http.config_test.downstream_rq_5xx")->value());
  EXPECT_EQ(0, test_server_->counter("custom_response_redirect_no_route")->value());
}

// Verify that we do not provide a custom response for a response that has
// already been redirected by the custom response filter.
// Verify that the route specific filter is picked if specified.
TEST_P(CustomResponseIntegrationTest, NoRecursion) {
  // Make the redirect policy response for gateway_error policy return 401
  config_helper_.addVirtualHost(TestUtility::parseYaml<VirtualHost>(R"EOF(
    name: fo1
    domains: ["fo1.example"]
    routes:
    - direct_response:
        status: 401
        body:
          inline_string: fo1
      match:
        prefix: "/"
    )EOF"));

  modifyPolicy<RedirectPolicyProto>(
      custom_response_filter_config_, "gateway_error_action",
      [](RedirectPolicyProto& policy) { policy.set_uri("https://fo1.example"); });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Verify that a 401 response will trigger 400_response policy
  default_request_headers_.setHost("original.host");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, unauthorized_response_, 0);
  // Verify that we get the modified status value.
  EXPECT_EQ("499", response->headers().getStatusValue());
  EXPECT_EQ("not allowed", response->body());
  EXPECT_EQ(
      "x-bar",
      response->headers().get(::Envoy::Http::LowerCaseString("foo"))[0]->value().getStringView());

  // Verify that 400_response policy cannot be triggered by the
  // gateway_error_response policy
  default_request_headers_.setHost("original.host");
  response = sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0);
  // Verify we get status code 299 for fo1.example as set by
  // gateway_error_action and not 499 or 401 which is the response code of the
  // error service.
  EXPECT_EQ("299", response->headers().getStatusValue());
}

// Verify that we get the response code of the original response if an override
// response code is not specified.
TEST_P(CustomResponseIntegrationTest, OriginalResponseCode) {
  // Make the redirect policy response for gateway_error policy return 401
  config_helper_.addVirtualHost(TestUtility::parseYaml<VirtualHost>(R"EOF(
    name: fo1
    domains: ["fo1.example"]
    routes:
    - direct_response:
        status: 401
        body:
          inline_string: fo1
      match:
        prefix: "/"
    )EOF"));

  modifyPolicy<RedirectPolicyProto>(custom_response_filter_config_, "gateway_error_action",
                                    [](RedirectPolicyProto& policy) {
                                      policy.set_uri("https://fo1.example");
                                      policy.clear_status_code();
                                    });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Verify that 400_response policy cannot be triggered by the
  // gateway_error_response policy
  default_request_headers_.setHost("original.host");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0);
  // Verify that we get the original response code 502.
  EXPECT_EQ("502", response->headers().getStatusValue());
}

// Verify that we get the response code of the original response if an override
// response code is not specified.
TEST_P(CustomResponseIntegrationTest, OriginalResponseCodeOverrides200) {
  // Make the redirect policy response for gateway_error policy return 401
  config_helper_.addVirtualHost(TestUtility::parseYaml<VirtualHost>(R"EOF(
    name: fo1
    domains: ["fo1.example"]
    routes:
    - direct_response:
        status: 200
        body:
          inline_string: fo1
      match:
        prefix: "/"
    )EOF"));

  modifyPolicy<RedirectPolicyProto>(custom_response_filter_config_, "gateway_error_action",
                                    [](RedirectPolicyProto& policy) {
                                      policy.set_uri("https://fo1.example");
                                      policy.clear_status_code();
                                    });

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Verify that 400_response policy cannot be triggered by the
  // gateway_error_response policy
  default_request_headers_.setHost("original.host");
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, gateway_error_response_, 0, 0);
  // Verify that we get the original response code 502.
  EXPECT_EQ("502", response->headers().getStatusValue());
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
  default_request_headers_.setHost("original.host");
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
  default_request_headers_.setHost("original.host");
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
  default_request_headers_.setHost("original.host");
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
  default_request_headers_.setHost("original.host");
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
  default_request_headers_.setHost("original.host");
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
  default_request_headers_.setHost("original.host");
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
  default_request_headers_.setHost("original.host");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  //     Verify we do not get a modified status value. (500 instead of 292)
  EXPECT_EQ("500", response->headers().getStatusValue());
}

// Verify that RedirectPolicy works with local reply sent after decodeHeaders is
// called for custom response filter.
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
  // Add route with header matcher
  config_helper_.addVirtualHost(TestUtility::parseYaml<VirtualHost>(R"EOF(
    name: cer-only-host
    domains: ["host.with.route.with.header.matcher"]
    routes:
    - direct_response:
        status: 202
        body:
          inline_string: cer-only-response
      match:
        prefix: "/"
    )EOF"));
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("original.host");
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  // Verify we DO get a modified status value. (292 instead of 500) as defined
  // in `500_action`
  EXPECT_EQ("292", response->headers().getStatusValue());
}

// Verify that the route meant for custom response redirection can be routed to with the required
// header.
TEST_P(CustomResponseIntegrationTest, RouteHeaderMatch) {
  // Add route with header matcher
  config_helper_.addVirtualHost(TestUtility::parseYaml<VirtualHost>(R"EOF(
    name: cer-only-host
    domains: ["host.with.route.with.header.matcher"]
    routes:
    - direct_response:
        status: 202
        body:
          inline_string: cer-only-response
      match:
        prefix: "/"
        headers:
        - name: cer-only
    )EOF"));

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Verify `host.with.route.with.header.matcher` is reachable via the redirect policy with the
  // "cer-only" header added.
  default_request_headers_.setHost("original.host");
  auto response2 =
      sendRequestAndWaitForResponse(default_request_headers_, 0, internal_server_error_, 0, 0);
  // The `500_action` redirect policy in kDefaultConfig adds the required header
  // to match the route for internal redirection. Verify that this is worked as
  // expected.
  EXPECT_EQ("292", response2->headers().getStatusValue());
  EXPECT_EQ("cer-only-response", response2->body());

  // Verify `host.with.route.with.header.matcher` is not directly reachable.
  default_request_headers_.setHost("host.with.route.with.header.matcher");
  auto response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  // Verify that the route was not found.
  EXPECT_EQ("499", response->headers().getStatusValue());
}

// Test ModifyRequestHeadersAction
TEST_P(CustomResponseIntegrationTest, ModifyRequestHeaders) {
  // Add modify_request_headers_action to the config. This will enable
  // TestModifyRequestHeadersAction to add "x-envoy-cer-backend" header to the
  // request before being redirected.
  modifyPolicy<RedirectPolicyProto>(
      custom_response_filter_config_, "520_action", [](RedirectPolicyProto& policy) {
        auto action = policy.mutable_modify_request_headers_action();
        action->set_name("modify-request-headers-action");
        action->mutable_typed_config()->set_type_url("type.googleapis.com/google.protobuf.Struct");
        *policy.mutable_redirect_action() = TestUtility::parseYaml<RedirectActionProto>(R"EOF(
    host_redirect: "global.storage"
    path_redirect: "/internal_server_error"
    https_redirect: true
    )EOF");
      });

  // Add TestModifyRequestHeaders extension that will add the
  // "x-envoy-cer-backend" header to the redirected request, which is required
  // for route selection for the redirected request.
  TestModifyRequestHeadersActionFactory factory;
  Envoy::Registry::InjectFactory<
      Extensions::Http::CustomResponse::ModifyRequestHeadersActionFactory>
      registration(factory);

  // Add route corresponding to the internal redirect for the redirect policy
  // that requires the presence of the `x-envoy-cer-backend` header for
  // matching.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void {
        auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes()->Add();
        route->mutable_match()->set_prefix("/internal_server_error");
        auto header = route->mutable_match()->mutable_headers()->Add();
        header->set_name("x-envoy-cer-backend");
        header->mutable_string_match()->set_exact("global.storage");
        route->mutable_direct_response()->set_status(static_cast<uint32_t>(220));
        // Use inline bytes rather than a filename to avoid using a path that may look illegal
        // to Envoy.
        route->mutable_direct_response()->mutable_body()->set_inline_bytes(
            "Modify action response body");
      });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("original.host");
  auto response = sendRequestAndWaitForResponse(
      default_request_headers_, 0,
      ::Envoy::Http::TestResponseHeaderMapImpl{{":status", "520"}, {"content-length", "0"}}, 0, 0);
  // Verify that we get the original response code in the absence of an
  // override.
  EXPECT_EQ("520", response->headers().getStatusValue());
  EXPECT_EQ("Modify action response body", response->body());
}

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, CustomResponseIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);
} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

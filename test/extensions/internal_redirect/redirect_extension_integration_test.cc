#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/internal_redirect/allow_listed_routes/v3/allow_listed_routes_config.pb.h"
#include "envoy/extensions/internal_redirect/previous_routes/v3/previous_routes_config.pb.h"
#include "envoy/extensions/internal_redirect/safe_cross_scheme/v3/safe_cross_scheme_config.pb.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {

using ::testing::HasSubstr;

namespace {
constexpr char kTestHeaderKey[] = "test-header";
} // namespace

class RedirectExtensionIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    setMaxRequestHeadersKb(60);
    setMaxRequestHeadersCount(100);
    envoy::config::route::v3::RetryPolicy retry_policy;

    auto pass_through = config_helper_.createVirtualHost("pass.through.internal.redirect");
    config_helper_.addVirtualHost(pass_through);

    auto handle = config_helper_.createVirtualHost("handle.internal.redirect");
    handle.mutable_routes(0)->set_name("redirect");
    handle.mutable_routes(0)->mutable_route()->mutable_internal_redirect_policy();
    config_helper_.addVirtualHost(handle);

    auto handle_max_3_hop =
        config_helper_.createVirtualHost("handle.internal.redirect.max.three.hop");
    handle_max_3_hop.mutable_routes(0)->set_name("max_three_hop");
    handle_max_3_hop.mutable_routes(0)->mutable_route()->mutable_internal_redirect_policy();
    handle_max_3_hop.mutable_routes(0)
        ->mutable_route()
        ->mutable_internal_redirect_policy()
        ->mutable_max_internal_redirects()
        ->set_value(3);
    config_helper_.addVirtualHost(handle_max_3_hop);

    HttpProtocolIntegrationTest::initialize();
  }

protected:
  // Returns the next stream that the fake upstream receives.
  FakeStreamPtr waitForNextStream() {
    FakeStreamPtr new_stream = nullptr;
    auto wait_new_stream_fn = [this,
                               &new_stream](FakeHttpConnectionPtr& connection) -> AssertionResult {
      AssertionResult result =
          connection->waitForNewStream(*dispatcher_, new_stream, std::chrono::milliseconds(50));
      if (result) {
        ASSERT(new_stream);
      }
      return result;
    };

    // Using a while loop to poll for new connections and new streams on all
    // connections because connection reuse may or may not be triggered.
    while (new_stream == nullptr) {
      FakeHttpConnectionPtr new_connection = nullptr;
      AssertionResult result = fake_upstreams_[0]->waitForHttpConnection(
          *dispatcher_, new_connection, std::chrono::milliseconds(50));
      if (result) {
        ASSERT(new_connection);
        upstream_connections_.push_back(std::move(new_connection));
      }

      for (auto& connection : upstream_connections_) {
        result = wait_new_stream_fn(connection);
        if (result) {
          break;
        }
      }
    }

    AssertionResult result = new_stream->waitForEndStream(*dispatcher_);
    ASSERT(result);
    return new_stream;
  }

  Http::TestResponseHeaderMapImpl redirect_response_{{":status", "302"},
                                                     {"content-length", "0"},
                                                     {"location", "http://authority2/new/url"},
                                                     // Test header added to confirm that response
                                                     // headers are populated for internal redirects
                                                     {kTestHeaderKey, "test-header-value"}};
  Envoy::Http::LowerCaseString test_header_key_{kTestHeaderKey};
  std::vector<FakeHttpConnectionPtr> upstream_connections_;
};

TEST_P(RedirectExtensionIntegrationTest, InternalRedirectPreventedByPreviousRoutesPredicate) {
  useAccessLog("%RESPONSE_CODE% %RESPONSE_CODE_DETAILS% %RESP(test-header)%");
  auto handle_prevent_repeated_target =
      config_helper_.createVirtualHost("handle.internal.redirect.no.repeated.target");
  auto* internal_redirect_policy = handle_prevent_repeated_target.mutable_routes(0)
                                       ->mutable_route()
                                       ->mutable_internal_redirect_policy();
  internal_redirect_policy->mutable_max_internal_redirects()->set_value(10);
  envoy::extensions::internal_redirect::previous_routes::v3::PreviousRoutesConfig
      previous_routes_config;
  auto* predicate = internal_redirect_policy->add_predicates();
  predicate->set_name("previous_routes");
  predicate->mutable_typed_config()->PackFrom(previous_routes_config);
  config_helper_.addVirtualHost(handle_prevent_repeated_target);

  // Validate that header sanitization is only called once.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.set_via("via_value"); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect.no.repeated.target");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  auto first_request = waitForNextStream();
  // Redirect to another route
  redirect_response_.setLocation("http://handle.internal.redirect.max.three.hop/random/path");
  first_request->encodeHeaders(redirect_response_, true);
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0),
              HasSubstr("302 internal_redirect test-header-value"));

  auto second_request = waitForNextStream();
  // Redirect back to the original route.
  redirect_response_.setLocation("http://handle.internal.redirect.no.repeated.target/another/path");
  second_request->encodeHeaders(redirect_response_, true);
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1),
              HasSubstr("302 internal_redirect test-header-value"));

  auto third_request = waitForNextStream();
  // Redirect to the same route as the first redirect. This should fail.
  redirect_response_.setLocation("http://handle.internal.redirect.max.three.hop/yet/another/path");
  third_request->encodeHeaders(redirect_response_, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 2),
              HasSubstr("302 via_upstream test-header-value"));
  EXPECT_EQ("302", response->headers().getStatusValue());
  EXPECT_EQ("http://handle.internal.redirect.max.three.hop/yet/another/path",
            response->headers().getLocationValue());
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_internal_redirect_succeeded_total")
                   ->value());
  EXPECT_EQ(
      1,
      test_server_->counter("http.config_test.passthrough_internal_redirect_predicate")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  EXPECT_EQ("test-header-value",
            response->headers().get(test_header_key_)[0]->value().getStringView());
}

TEST_P(RedirectExtensionIntegrationTest, InternalRedirectPreventedByAllowListedRoutesPredicate) {
  useAccessLog("%RESPONSE_CODE% %RESPONSE_CODE_DETAILS% %RESP(test-header)%");
  auto handle_allow_listed_redirect_route =
      config_helper_.createVirtualHost("handle.internal.redirect.only.allow.listed.target");
  auto* internal_redirect_policy = handle_allow_listed_redirect_route.mutable_routes(0)
                                       ->mutable_route()
                                       ->mutable_internal_redirect_policy();

  auto* allow_listed_routes_predicate = internal_redirect_policy->add_predicates();
  allow_listed_routes_predicate->set_name("allow_listed_routes");
  envoy::extensions::internal_redirect::allow_listed_routes::v3::AllowListedRoutesConfig
      allow_listed_routes_config;
  *allow_listed_routes_config.add_allowed_route_names() = "max_three_hop";
  allow_listed_routes_predicate->mutable_typed_config()->PackFrom(allow_listed_routes_config);

  internal_redirect_policy->mutable_max_internal_redirects()->set_value(10);

  config_helper_.addVirtualHost(handle_allow_listed_redirect_route);

  // Validate that header sanitization is only called once.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.set_via("via_value"); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect.only.allow.listed.target");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  auto first_request = waitForNextStream();
  // Redirect to another route
  redirect_response_.setLocation("http://handle.internal.redirect.max.three.hop/random/path");
  first_request->encodeHeaders(redirect_response_, true);
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0),
              HasSubstr("302 internal_redirect test-header-value"));

  auto second_request = waitForNextStream();
  // Redirect back to the original route.
  redirect_response_.setLocation(
      "http://handle.internal.redirect.only.allow.listed.target/another/path");
  second_request->encodeHeaders(redirect_response_, true);
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1),
              HasSubstr("302 internal_redirect test-header-value"));

  auto third_request = waitForNextStream();
  // Redirect to the non-allow-listed route. This should fail.
  redirect_response_.setLocation("http://handle.internal.redirect/yet/another/path");
  third_request->encodeHeaders(redirect_response_, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("302", response->headers().getStatusValue());
  EXPECT_EQ("http://handle.internal.redirect/yet/another/path",
            response->headers().getLocationValue());
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_internal_redirect_succeeded_total")
                   ->value());
  EXPECT_EQ(
      1,
      test_server_->counter("http.config_test.passthrough_internal_redirect_predicate")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 2),
              HasSubstr("302 via_upstream test-header-value"));
  EXPECT_EQ("test-header-value",
            response->headers().get(test_header_key_)[0]->value().getStringView());
}

TEST_P(RedirectExtensionIntegrationTest, InternalRedirectPreventedBySafeCrossSchemePredicate) {
  useAccessLog("%RESPONSE_CODE% %RESPONSE_CODE_DETAILS% %RESP(test-header)%");
  auto handle_safe_cross_scheme_route = config_helper_.createVirtualHost(
      "handle.internal.redirect.only.allow.safe.cross.scheme.redirect");
  auto* internal_redirect_policy = handle_safe_cross_scheme_route.mutable_routes(0)
                                       ->mutable_route()
                                       ->mutable_internal_redirect_policy();

  internal_redirect_policy->set_allow_cross_scheme_redirect(true);

  auto* predicate = internal_redirect_policy->add_predicates();
  predicate->set_name("safe_cross_scheme_predicate");
  envoy::extensions::internal_redirect::safe_cross_scheme::v3::SafeCrossSchemeConfig
      predicate_config;
  predicate->mutable_typed_config()->PackFrom(predicate_config);

  internal_redirect_policy->mutable_max_internal_redirects()->set_value(10);

  config_helper_.addVirtualHost(handle_safe_cross_scheme_route);

  // Validate that header sanitization is only called once.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.set_via("via_value"); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost(
      "handle.internal.redirect.only.allow.safe.cross.scheme.redirect");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  auto first_request = waitForNextStream();
  // Redirect to another route
  redirect_response_.setLocation("http://handle.internal.redirect.max.three.hop/random/path");
  first_request->encodeHeaders(redirect_response_, true);
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0),
              HasSubstr("302 internal_redirect test-header-value"));

  auto second_request = waitForNextStream();
  // Redirect back to the original route.
  redirect_response_.setLocation(
      "http://handle.internal.redirect.only.allow.safe.cross.scheme.redirect/another/path");
  second_request->encodeHeaders(redirect_response_, true);
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1),
              HasSubstr("302 internal_redirect test-header-value"));

  auto third_request = waitForNextStream();
  // Redirect to https target. This should fail.
  redirect_response_.setLocation("https://handle.internal.redirect/yet/another/path");
  third_request->encodeHeaders(redirect_response_, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("302", response->headers().getStatusValue());
  EXPECT_EQ("https://handle.internal.redirect/yet/another/path",
            response->headers().getLocationValue());
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_internal_redirect_succeeded_total")
                   ->value());
  EXPECT_EQ(
      1,
      test_server_->counter("http.config_test.passthrough_internal_redirect_predicate")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 2),
              HasSubstr("302 via_upstream test-header-value"));
  EXPECT_EQ("test-header-value",
            response->headers().get(test_header_key_)[0]->value().getStringView());
}

INSTANTIATE_TEST_SUITE_P(Protocols, RedirectExtensionIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace Envoy

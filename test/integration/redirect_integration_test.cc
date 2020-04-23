#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/internal_redirect/whitelisted_routes/v3/whitelisted_routes_config.pb.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {

namespace {
constexpr char HandleThreeHopLocationFormat[] =
    "http://handle.internal.redirect.max.three.hop/path{}";
}

class RedirectIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    envoy::config::route::v3::RetryPolicy retry_policy;

    auto pass_through = config_helper_.createVirtualHost("pass.through.internal.redirect");
    config_helper_.addVirtualHost(pass_through);

    auto handle = config_helper_.createVirtualHost("handle.internal.redirect");
    handle.mutable_routes(0)->set_name("redirect");
    handle.mutable_routes(0)
        ->mutable_route()
        ->mutable_internal_redirect_policy()
        ->set_internal_redirect_action(
            envoy::config::route::v3::InternalRedirectPolicy::HANDLE_INTERNAL_REDIRECT);
    config_helper_.addVirtualHost(handle);

    auto handle_max_3_hop =
        config_helper_.createVirtualHost("handle.internal.redirect.max.three.hop");
    handle_max_3_hop.mutable_routes(0)->set_name("max_three_hop");
    handle_max_3_hop.mutable_routes(0)
        ->mutable_route()
        ->mutable_internal_redirect_policy()
        ->set_internal_redirect_action(
            envoy::config::route::v3::InternalRedirectPolicy::HANDLE_INTERNAL_REDIRECT);
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
      AssertionResult result = connection->waitForNewStream(*dispatcher_, new_stream, false,
                                                            std::chrono::milliseconds(50));
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
          *dispatcher_, new_connection, std::chrono::milliseconds(50), 60, 100);
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

  Http::TestResponseHeaderMapImpl redirect_response_{
      {":status", "302"}, {"content-length", "0"}, {"location", "http://authority2/new/url"}};

  std::vector<FakeHttpConnectionPtr> upstream_connections_;
};

// By default if internal redirects are not configured, redirects are proxied.
TEST_P(RedirectIntegrationTest, RedirectNotConfigured) {
  // Use base class initialize.
  HttpProtocolIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, redirect_response_, 0);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("302", response->headers().Status()->value().getStringView());
}

// Now test a route with redirects configured on in pass-through mode.
TEST_P(RedirectIntegrationTest, InternalRedirectPassedThrough) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("pass.through.internal.redirect");
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, redirect_response_, 0);
  EXPECT_EQ("302", response->headers().Status()->value().getStringView());
  EXPECT_EQ(
      0,
      test_server_->counter("cluster.cluster_0.upstream_internal_redirect_failed_total")->value());
}

TEST_P(RedirectIntegrationTest, BasicInternalRedirect) {
  // Validate that header sanitization is only called once.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.set_via("via_value"); });
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(redirect_response_, true);

  waitForNextUpstreamRequest();
  ASSERT(upstream_request_->headers().EnvoyOriginalUrl() != nullptr);
  EXPECT_EQ("http://handle.internal.redirect/test/long/url",
            upstream_request_->headers().EnvoyOriginalUrl()->value().getStringView());
  EXPECT_EQ("/new/url", upstream_request_->headers().Path()->value().getStringView());
  EXPECT_EQ("authority2", upstream_request_->headers().Host()->value().getStringView());
  EXPECT_EQ("via_value", upstream_request_->headers().Via()->value().getStringView());

  upstream_request_->encodeHeaders(default_response_headers_, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_internal_redirect_succeeded_total")
                   ->value());
}

TEST_P(RedirectIntegrationTest, InternalRedirectWithThreeHopLimit) {
  // Validate that header sanitization is only called once.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.set_via("via_value"); });
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect.max.three.hop");
  default_request_headers_.setPath("/path0");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  std::vector<FakeStreamPtr> upstream_requests;
  // Four requests to upstream: 1 original request + 3 following redirect
  for (int i = 0; i < 4; i++) {
    upstream_requests.push_back(waitForNextStream());

    EXPECT_EQ(fmt::format("/path{}", i),
              upstream_requests.back()->headers().Path()->value().getStringView());
    EXPECT_EQ("handle.internal.redirect.max.three.hop",
              upstream_requests.back()->headers().Host()->value().getStringView());
    EXPECT_EQ("via_value", upstream_requests.back()->headers().Via()->value().getStringView());

    auto next_location = fmt::format(HandleThreeHopLocationFormat, i + 1);
    redirect_response_.setLocation(next_location);
    upstream_requests.back()->encodeHeaders(redirect_response_, true);
  }

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("302", response->headers().Status()->value().getStringView());
  EXPECT_EQ(
      1,
      test_server_->counter("cluster.cluster_0.upstream_internal_redirect_failed_total")->value());
}

TEST_P(RedirectIntegrationTest, InternalRedirectToDestinationWithBody) {
  // Validate that header sanitization is only called once.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.set_via("via_value"); });
  config_helper_.addFilter(R"EOF(
  name: pause-filter
  typed_config:
    "@type": type.googleapis.com/google.protobuf.Empty
  )EOF");
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(redirect_response_, true);

  waitForNextUpstreamRequest();
  ASSERT(upstream_request_->headers().EnvoyOriginalUrl() != nullptr);
  EXPECT_EQ("http://handle.internal.redirect/test/long/url",
            upstream_request_->headers().EnvoyOriginalUrl()->value().getStringView());
  EXPECT_EQ("/new/url", upstream_request_->headers().Path()->value().getStringView());
  EXPECT_EQ("authority2", upstream_request_->headers().Host()->value().getStringView());
  EXPECT_EQ("via_value", upstream_request_->headers().Via()->value().getStringView());

  Http::TestHeaderMapImpl response_with_big_body(
      {{":status", "200"}, {"content-length", "2000000"}});
  upstream_request_->encodeHeaders(response_with_big_body, false);
  upstream_request_->encodeData(2000000, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_internal_redirect_succeeded_total")
                   ->value());
}

TEST_P(RedirectIntegrationTest, InternalRedirectPreventedByPreviousRoutesPredicate) {
  auto handle_prevent_repeated_target =
      config_helper_.createVirtualHost("handle.internal.redirect.no.repeated.target");
  auto* internal_redirect_policy = handle_prevent_repeated_target.mutable_routes(0)
                                       ->mutable_route()
                                       ->mutable_internal_redirect_policy();
  internal_redirect_policy->set_internal_redirect_action(
      envoy::config::route::v3::InternalRedirectPolicy::HANDLE_INTERNAL_REDIRECT);
  internal_redirect_policy->add_predicates()->set_name(
      "envoy.internal_redirect_predicates.previous_routes");
  internal_redirect_policy->mutable_max_internal_redirects()->set_value(10);
  config_helper_.addVirtualHost(handle_prevent_repeated_target);

  // Validate that header sanitization is only called once.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.set_via("via_value"); });
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect.no.repeated.target");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  auto first_request = waitForNextStream();
  // Redirect to another route
  redirect_response_.setLocation("http://handle.internal.redirect.max.three.hop/random/path");
  first_request->encodeHeaders(redirect_response_, true);

  auto second_request = waitForNextStream();
  // Redirect back to the original route.
  redirect_response_.setLocation("http://handle.internal.redirect.no.repeated.target/another/path");
  second_request->encodeHeaders(redirect_response_, true);

  auto third_request = waitForNextStream();
  // Redirect to the same route as the first redirect. This should fail.
  redirect_response_.setLocation("http://handle.internal.redirect.max.three.hop/yet/another/path");
  third_request->encodeHeaders(redirect_response_, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("302", response->headers().Status()->value().getStringView());
  EXPECT_EQ("http://handle.internal.redirect.max.three.hop/yet/another/path",
            response->headers().Location()->value().getStringView());
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_internal_redirect_succeeded_total")
                   ->value());
}

TEST_P(RedirectIntegrationTest, InternalRedirectPreventedByWhitelistedRoutesPredicate) {
  auto handle_whitelisted_redirect_route =
      config_helper_.createVirtualHost("handle.internal.redirect.only.whitelisted.target");
  auto* internal_redirect_policy = handle_whitelisted_redirect_route.mutable_routes(0)
                                       ->mutable_route()
                                       ->mutable_internal_redirect_policy();
  internal_redirect_policy->set_internal_redirect_action(
      envoy::config::route::v3::InternalRedirectPolicy::HANDLE_INTERNAL_REDIRECT);

  auto* whitelisted_routes_predicate = internal_redirect_policy->add_predicates();
  whitelisted_routes_predicate->set_name("envoy.internal_redirect_predicates.whitelisted_routes");
  envoy::extensions::internal_redirect::whitelisted_routes::v3::WhitelistedRoutesConfig
      whitelisted_routes_config;
  *whitelisted_routes_config.add_whitelisted_route_names() = "max_three_hop";
  whitelisted_routes_predicate->mutable_typed_config()->PackFrom(whitelisted_routes_config);

  internal_redirect_policy->mutable_max_internal_redirects()->set_value(10);

  config_helper_.addVirtualHost(handle_whitelisted_redirect_route);

  // Validate that header sanitization is only called once.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.set_via("via_value"); });
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect.only.whitelisted.target");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  auto first_request = waitForNextStream();
  // Redirect to another route
  redirect_response_.setLocation("http://handle.internal.redirect.max.three.hop/random/path");
  first_request->encodeHeaders(redirect_response_, true);

  auto second_request = waitForNextStream();
  // Redirect back to the original route.
  redirect_response_.setLocation(
      "http://handle.internal.redirect.only.whitelisted.target/another/path");
  second_request->encodeHeaders(redirect_response_, true);

  auto third_request = waitForNextStream();
  // Redirect to the non-whitelisted route. This should fail.
  redirect_response_.setLocation("http://handle.internal.redirect/yet/another/path");
  third_request->encodeHeaders(redirect_response_, true);

  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("302", response->headers().Status()->value().getStringView());
  EXPECT_EQ("http://handle.internal.redirect/yet/another/path",
            response->headers().Location()->value().getStringView());
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_internal_redirect_succeeded_total")
                   ->value());
}

TEST_P(RedirectIntegrationTest, InvalidRedirect) {
  initialize();

  redirect_response_.setLocation("invalid_url");

  // Send the same request as above, only send an invalid URL as the response.
  // The request should not be redirected.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("handle.internal.redirect");
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, redirect_response_, 0);
  EXPECT_EQ("302", response->headers().Status()->value().getStringView());
  EXPECT_EQ(
      1,
      test_server_->counter("cluster.cluster_0.upstream_internal_redirect_failed_total")->value());
}

INSTANTIATE_TEST_SUITE_P(Protocols, RedirectIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace Envoy

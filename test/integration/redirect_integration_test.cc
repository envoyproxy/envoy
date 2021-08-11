#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/internal_redirect/allow_listed_routes/v3/allow_listed_routes_config.pb.h"
#include "envoy/extensions/internal_redirect/previous_routes/v3/previous_routes_config.pb.h"
#include "envoy/extensions/internal_redirect/safe_cross_scheme/v3/safe_cross_scheme_config.pb.h"

#include "test/integration/http_protocol_integration.h"

namespace Envoy {

using ::testing::HasSubstr;

namespace {
constexpr char HandleThreeHopLocationFormat[] =
    "http://handle.internal.redirect.max.three.hop/path{}";
constexpr char kTestHeaderKey[] = "test-header";
} // namespace

class RedirectIntegrationTest : public HttpProtocolIntegrationTest {
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

// By default if internal redirects are not configured, redirects are proxied.
TEST_P(RedirectIntegrationTest, RedirectNotConfigured) {
  useAccessLog("%RESPONSE_CODE% %RESPONSE_CODE_DETAILS% %RESP(test-header)%");

  // Use base class initialize.
  HttpProtocolIntegrationTest::initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, redirect_response_, 0);
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("302", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_),
              HasSubstr("302 via_upstream test-header-value\n"));
  EXPECT_EQ("test-header-value",
            response->headers().get(test_header_key_)[0]->value().getStringView());
}

// Now test a route with redirects configured on in pass-through mode.
TEST_P(RedirectIntegrationTest, InternalRedirectPassedThrough) {
  useAccessLog("%RESPONSE_CODE% %RESPONSE_CODE_DETAILS% %RESP(test-header)%");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("pass.through.internal.redirect");
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, redirect_response_, 0);
  EXPECT_EQ("302", response->headers().getStatusValue());
  EXPECT_EQ(
      0,
      test_server_->counter("cluster.cluster_0.upstream_internal_redirect_failed_total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_),
              HasSubstr("302 via_upstream test-header-value\n"));
  EXPECT_EQ("test-header-value",
            response->headers().get(test_header_key_)[0]->value().getStringView());
}

TEST_P(RedirectIntegrationTest, BasicInternalRedirect) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE% %RESPONSE_CODE_DETAILS% %RESP(test-header)%");
  // Validate that header sanitization is only called once.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.set_via("via_value"); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(redirect_response_, true);

  waitForNextUpstreamRequest();
  ASSERT(upstream_request_->headers().EnvoyOriginalUrl() != nullptr);
  EXPECT_EQ("http://handle.internal.redirect/test/long/url",
            upstream_request_->headers().getEnvoyOriginalUrlValue());
  EXPECT_EQ("/new/url", upstream_request_->headers().getPathValue());
  EXPECT_EQ("authority2", upstream_request_->headers().getHostValue());
  EXPECT_EQ("via_value", upstream_request_->headers().getViaValue());

  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_internal_redirect_succeeded_total")
                   ->value());
  // 302 was never returned downstream
  EXPECT_EQ(0, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_2xx")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0),
              HasSubstr("302 internal_redirect test-header-value\n"));
  // No test header
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1), HasSubstr("200 via_upstream -\n"));
}

TEST_P(RedirectIntegrationTest, InternalRedirectWithRequestBody) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE% %RESPONSE_CODE_DETAILS% %RESP(test-header)%");
  // Validate that header sanitization is only called once.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.set_via("via_value"); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect");
  default_request_headers_.setMethod("POST");
  const std::string& request_body = "foobarbizbaz";

  // First request to original upstream.
  IntegrationStreamDecoderPtr response =
      codec_client_->makeRequestWithBody(default_request_headers_, request_body);
  waitForNextUpstreamRequest();
  EXPECT_EQ(request_body, upstream_request_->body().toString());

  // Respond with a redirect.
  upstream_request_->encodeHeaders(redirect_response_, true);

  // Second request to redirected upstream.
  waitForNextUpstreamRequest();
  EXPECT_EQ(request_body, upstream_request_->body().toString());
  ASSERT(upstream_request_->headers().EnvoyOriginalUrl() != nullptr);
  EXPECT_EQ("http://handle.internal.redirect/test/long/url",
            upstream_request_->headers().getEnvoyOriginalUrlValue());
  EXPECT_EQ("/new/url", upstream_request_->headers().getPathValue());
  EXPECT_EQ("authority2", upstream_request_->headers().getHostValue());
  EXPECT_EQ("via_value", upstream_request_->headers().getViaValue());

  // Return the response from the redirect upstream.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_internal_redirect_succeeded_total")
                   ->value());
  // 302 was never returned downstream
  EXPECT_EQ(0, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_2xx")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0),
              HasSubstr("302 internal_redirect test-header-value\n"));
  // No test header
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1), HasSubstr("200 via_upstream -\n"));
}

TEST_P(RedirectIntegrationTest, InternalRedirectHandlesHttp303) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE% %RESPONSE_CODE_DETAILS% %RESP(test-header)%");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        hcm.set_via("via_value");

        auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(2)->mutable_routes(0);
        route->mutable_route()
            ->mutable_internal_redirect_policy()
            ->mutable_redirect_response_codes()
            ->Add(303);
      });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  const std::string& request_body = "foobarbizbaz";
  default_request_headers_.setHost("handle.internal.redirect");
  default_request_headers_.setMethod("POST");
  default_request_headers_.setContentLength(request_body.length());

  // First request to original upstream.
  IntegrationStreamDecoderPtr response =
      codec_client_->makeRequestWithBody(default_request_headers_, request_body);
  waitForNextUpstreamRequest();
  EXPECT_EQ(request_body, upstream_request_->body().toString());

  // Respond with a redirect.
  redirect_response_.setStatus(303);
  upstream_request_->encodeHeaders(redirect_response_, true);

  // Second request to redirected upstream.
  waitForNextUpstreamRequest();
  EXPECT_EQ("", upstream_request_->body().toString());
  ASSERT(upstream_request_->headers().EnvoyOriginalUrl() != nullptr);
  EXPECT_EQ("http://handle.internal.redirect/test/long/url",
            upstream_request_->headers().getEnvoyOriginalUrlValue());
  EXPECT_EQ("/new/url", upstream_request_->headers().getPathValue());
  EXPECT_EQ("authority2", upstream_request_->headers().getHostValue());
  EXPECT_EQ("via_value", upstream_request_->headers().getViaValue());
  EXPECT_EQ("GET", upstream_request_->headers().getMethodValue());
  EXPECT_EQ("", upstream_request_->headers().getContentLengthValue());

  // Return the response from the redirect upstream.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_internal_redirect_succeeded_total")
                   ->value());
  // 302 was never returned downstream
  EXPECT_EQ(0, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_2xx")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0),
              HasSubstr("303 internal_redirect test-header-value\n"));
  // No test header
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1), HasSubstr("200 via_upstream -\n"));
}

TEST_P(RedirectIntegrationTest, InternalRedirectHttp303PreservesHeadMethod) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE% %RESPONSE_CODE_DETAILS% %RESP(test-header)%");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        hcm.set_via("via_value");

        auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(2)->mutable_routes(0);
        route->mutable_route()
            ->mutable_internal_redirect_policy()
            ->mutable_redirect_response_codes()
            ->Add(303);
      });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect");
  default_request_headers_.setMethod("HEAD");

  // First request to original upstream.
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();

  // Respond with a redirect.
  redirect_response_.setStatus(303);
  upstream_request_->encodeHeaders(redirect_response_, true);

  // Second request to redirected upstream.
  waitForNextUpstreamRequest();
  EXPECT_EQ("", upstream_request_->body().toString());
  ASSERT(upstream_request_->headers().EnvoyOriginalUrl() != nullptr);
  EXPECT_EQ("http://handle.internal.redirect/test/long/url",
            upstream_request_->headers().getEnvoyOriginalUrlValue());
  EXPECT_EQ("/new/url", upstream_request_->headers().getPathValue());
  EXPECT_EQ("authority2", upstream_request_->headers().getHostValue());
  EXPECT_EQ("via_value", upstream_request_->headers().getViaValue());
  EXPECT_EQ("HEAD", upstream_request_->headers().getMethodValue());

  // Return the response from the redirect upstream.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_internal_redirect_succeeded_total")
                   ->value());
  // 302 was never returned downstream
  EXPECT_EQ(0, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_2xx")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0),
              HasSubstr("303 internal_redirect test-header-value\n"));
  // No test header
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1), HasSubstr("200 via_upstream -\n"));
}

TEST_P(RedirectIntegrationTest, InternalRedirectCancelledDueToBufferOverflow) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE% %RESPONSE_CODE_DETAILS% %RESP(test-header)%");
  // Validate that header sanitization is only called once.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(2)->mutable_routes(0);
        route->mutable_per_request_buffer_limit_bytes()->set_value(1024);
      });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect");
  default_request_headers_.setMethod("POST");
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto& encoder = encoder_decoder.first;
  auto& response = encoder_decoder.second;

  // Send more data than what we can buffer.
  std::string data(2048, 'a');
  Buffer::OwnedImpl send1(data);
  encoder.encodeData(send1, true);

  // Wait for a redirect response.
  waitForNextUpstreamRequest();
  EXPECT_EQ(data, upstream_request_->body().toString());
  upstream_request_->encodeHeaders(redirect_response_, true);

  // Ensure the redirect was returned to the client.
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("302", response->headers().getStatusValue());
}

TEST_P(RedirectIntegrationTest, InternalRedirectCancelledDueToEarlyResponse) {
  useAccessLog("%RESPONSE_FLAGS% %RESPONSE_CODE% %RESPONSE_CODE_DETAILS% %RESP(test-header)%");

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect");
  default_request_headers_.setMethod("POST");
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
  auto& response = encoder_decoder.second;

  // Wait for the request headers to be received upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Respond with a redirect before the request is complete.
  upstream_request_->encodeHeaders(redirect_response_, true);
  ASSERT_TRUE(response->waitForEndStream());

  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    codec_client_->close();
  }

  EXPECT_FALSE(upstream_request_->complete());

  // Ensure the redirect was returned to the client and not handled internally.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("302", response->headers().getStatusValue());
}

TEST_P(RedirectIntegrationTest, InternalRedirectWithThreeHopLimit) {
  useAccessLog("%RESPONSE_CODE% %RESPONSE_CODE_DETAILS% %RESP(test-header)%");
  // Validate that header sanitization is only called once.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.set_via("via_value"); });
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect.max.three.hop");
  default_request_headers_.setPath("/path0");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  std::vector<FakeStreamPtr> upstream_requests;
  // Four requests to upstream: 1 original request + 3 following redirect
  for (int i = 0; i < 4; i++) {
    upstream_requests.push_back(waitForNextStream());

    EXPECT_EQ(fmt::format("/path{}", i), upstream_requests.back()->headers().getPathValue());
    EXPECT_EQ("handle.internal.redirect.max.three.hop",
              upstream_requests.back()->headers().getHostValue());
    EXPECT_EQ("via_value", upstream_requests.back()->headers().getViaValue());

    auto next_location = fmt::format(HandleThreeHopLocationFormat, i + 1);
    redirect_response_.setLocation(next_location);
    upstream_requests.back()->encodeHeaders(redirect_response_, true);
  }

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("302", response->headers().getStatusValue());
  EXPECT_EQ(
      1,
      test_server_->counter("cluster.cluster_0.upstream_internal_redirect_failed_total")->value());
  EXPECT_EQ(
      1, test_server_->counter("http.config_test.passthrough_internal_redirect_too_many_redirects")
             ->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0),
              HasSubstr("302 internal_redirect test-header-value\n"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1),
              HasSubstr("302 internal_redirect test-header-value\n"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 2),
              HasSubstr("302 internal_redirect test-header-value\n"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 3),
              HasSubstr("302 via_upstream test-header-value\n"));
  EXPECT_EQ("test-header-value",
            response->headers().get(test_header_key_)[0]->value().getStringView());
}

TEST_P(RedirectIntegrationTest, InternalRedirectToDestinationWithResponseBody) {
  useAccessLog("%RESPONSE_CODE% %RESPONSE_CODE_DETAILS% %RESP(test-header)%");
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

  codec_client_ = makeHttpConnection(lookupPort("http"));

  default_request_headers_.setHost("handle.internal.redirect");
  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(redirect_response_, true);

  waitForNextUpstreamRequest();
  ASSERT(upstream_request_->headers().EnvoyOriginalUrl() != nullptr);
  EXPECT_EQ("http://handle.internal.redirect/test/long/url",
            upstream_request_->headers().getEnvoyOriginalUrlValue());
  EXPECT_EQ("/new/url", upstream_request_->headers().getPathValue());
  EXPECT_EQ("authority2", upstream_request_->headers().getHostValue());
  EXPECT_EQ("via_value", upstream_request_->headers().getViaValue());

  Http::TestResponseHeaderMapImpl response_with_big_body(
      {{":status", "200"}, {"content-length", "2000000"}});
  upstream_request_->encodeHeaders(response_with_big_body, false);
  upstream_request_->encodeData(2000000, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_internal_redirect_succeeded_total")
                   ->value());
  // 302 was never returned downstream
  EXPECT_EQ(0, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_2xx")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0),
              HasSubstr("302 internal_redirect test-header-value\n"));
  // No test header
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1), HasSubstr("200 via_upstream -\n"));
}

TEST_P(RedirectIntegrationTest, InternalRedirectPreventedByPreviousRoutesPredicate) {
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

  auto second_request = waitForNextStream();
  // Redirect back to the original route.
  redirect_response_.setLocation("http://handle.internal.redirect.no.repeated.target/another/path");
  second_request->encodeHeaders(redirect_response_, true);

  auto third_request = waitForNextStream();
  // Redirect to the same route as the first redirect. This should fail.
  redirect_response_.setLocation("http://handle.internal.redirect.max.three.hop/yet/another/path");
  third_request->encodeHeaders(redirect_response_, true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("302", response->headers().getStatusValue());
  EXPECT_EQ("http://handle.internal.redirect.max.three.hop/yet/another/path",
            response->headers().getLocationValue());
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_internal_redirect_succeeded_total")
                   ->value());
  EXPECT_EQ(
      1,
      test_server_->counter("http.config_test.passthrough_internal_redirect_predicate")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0),
              HasSubstr("302 internal_redirect test-header-value\n"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1),
              HasSubstr("302 internal_redirect test-header-value\n"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 2),
              HasSubstr("302 via_upstream test-header-value\n"));
  EXPECT_EQ("test-header-value",
            response->headers().get(test_header_key_)[0]->value().getStringView());
}

TEST_P(RedirectIntegrationTest, InternalRedirectPreventedByAllowListedRoutesPredicate) {
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

  auto second_request = waitForNextStream();
  // Redirect back to the original route.
  redirect_response_.setLocation(
      "http://handle.internal.redirect.only.allow.listed.target/another/path");
  second_request->encodeHeaders(redirect_response_, true);

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
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0),
              HasSubstr("302 internal_redirect test-header-value\n"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1),
              HasSubstr("302 internal_redirect test-header-value\n"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 2),
              HasSubstr("302 via_upstream test-header-value\n"));
  EXPECT_EQ("test-header-value",
            response->headers().get(test_header_key_)[0]->value().getStringView());
}

TEST_P(RedirectIntegrationTest, InternalRedirectPreventedBySafeCrossSchemePredicate) {
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

  auto second_request = waitForNextStream();
  // Redirect back to the original route.
  redirect_response_.setLocation(
      "http://handle.internal.redirect.only.allow.safe.cross.scheme.redirect/another/path");
  second_request->encodeHeaders(redirect_response_, true);

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
  EXPECT_THAT(waitForAccessLog(access_log_name_, 0),
              HasSubstr("302 internal_redirect test-header-value\n"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1),
              HasSubstr("302 internal_redirect test-header-value\n"));
  EXPECT_THAT(waitForAccessLog(access_log_name_, 2),
              HasSubstr("302 via_upstream test-header-value\n"));
  EXPECT_EQ("test-header-value",
            response->headers().get(test_header_key_)[0]->value().getStringView());
}

TEST_P(RedirectIntegrationTest, InvalidRedirect) {
  useAccessLog("%RESPONSE_CODE% %RESPONSE_CODE_DETAILS% %RESP(test-header)%");
  initialize();

  redirect_response_.setLocation("invalid_url");

  // Send the same request as above, only send an invalid URL as the response.
  // The request should not be redirected.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  default_request_headers_.setHost("handle.internal.redirect");
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, redirect_response_, 0);
  EXPECT_EQ("302", response->headers().getStatusValue());
  EXPECT_EQ(
      1,
      test_server_->counter("cluster.cluster_0.upstream_internal_redirect_failed_total")->value());
  EXPECT_EQ(1, test_server_->counter("http.config_test.downstream_rq_3xx")->value());
  EXPECT_THAT(waitForAccessLog(access_log_name_),
              HasSubstr("302 via_upstream test-header-value\n"));
  EXPECT_EQ("test-header-value",
            response->headers().get(test_header_key_)[0]->value().getStringView());
}

INSTANTIATE_TEST_SUITE_P(Protocols, RedirectIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace Envoy
